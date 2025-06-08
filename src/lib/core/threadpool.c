#define _DEFAULT_SOURCE

#include <poser/core/event.h>
#include <poser/core/log.h>
#include <poser/core/service.h>
#include <poser/core/threadpool.h>
#include <poser/core/timer.h>
#include <poser/core/util.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#undef THRP_NO_ATOMICS
#undef THRP_ATOMIC
#if defined(NO_ATOMICS) || defined(__STDC_NO_ATOMICS__)
#  define THRP_NO_ATOMICS
#  define THRP_ATOMIC
#else
#  include <stdatomic.h>
#  if ATOMIC_POINTER_LOCK_FREE != 2
#    define THRP_NO_ATOMICS
#    define THRP_ATOMIC
#  else
#    define THRP_ATOMIC _Atomic
#  endif
#endif

#ifdef HAVE_UCONTEXT
#  include "stackmgr.h"
#  include <ucontext.h>
#endif

#ifndef DEFTHREADS
#define DEFTHREADS 16
#endif

#ifndef MAXTHREADS
#define MAXTHREADS 128
#endif

#ifndef THREADSPERCPU
#define THREADSPERCPU 1
#endif

#ifndef MAXQUEUELEN
#define MAXQUEUELEN 512
#endif

#ifndef MINQUEUELEN
#define MINQUEUELEN 32
#endif

#ifndef QLENPERTHREAD
#define QLENPERTHREAD 2
#endif

struct PSC_ThreadJob
{
    PSC_ThreadProc proc;
    void *arg;
    PSC_Event *finished;
    PSC_Timer *timeout;
    PSC_AsyncTask *task;
    const char *panicmsg;
#ifdef THRP_NO_ATOMICS
    pthread_mutex_t lock;
    int hasCompleted;
    int pthrno;
#else
    atomic_int hasCompleted;
    atomic_int pthrno;
#endif
    int thrno;
    unsigned timeoutMs;
#ifdef HAVE_UCONTEXT
    ucontext_t caller;
    void *stack;
    int async;
#endif
};

typedef struct PSC_ThreadOpts
{
    int nThreads;
    int maxThreads;
    int nPerCpu;
    int defNThreads;
    int queueLen;
    int maxQueueLen;
    int minQueueLen;
    int qLenPerThread;
} PSC_ThreadOpts;

typedef struct JobQueue
{
    unsigned sz;
#ifdef THRP_NO_ATOMICS
    unsigned avail;
    unsigned enqpos;
    unsigned deqpos;
    pthread_mutex_t lock;
#else
    char _pad0[64 - sizeof(unsigned)];
    atomic_uint avail;
    char _pad1[64 - sizeof(unsigned)];
    atomic_uint enqpos;
    char _pad2[64 - sizeof(unsigned)];
    atomic_uint deqpos;
    char _pad3[64 - sizeof(unsigned)];
#endif
    sem_t count;
    PSC_ThreadJob *THRP_ATOMIC jobs[];
} JobQueue;

typedef struct Thread
{
    pthread_t handle;
    sem_t stop;
#ifdef HAVE_UCONTEXT
    ucontext_t context;
#endif
    int pthrno;
} Thread;

struct PSC_AsyncTask
{
    PSC_AsyncTaskJob job;
    PSC_ThreadJob *threadJob;
    Thread *thread;
    void *arg;
    void *result;
    sem_t complete;
#ifdef HAVE_UCONTEXT
    ucontext_t resume;
#endif
};

static PSC_ThreadOpts opts;
static Thread *threads;
static JobQueue *jobQueue;
static int nthreads;
static int rthreads;

static THREADLOCAL int mainthread;
static THREADLOCAL jmp_buf panicjmp;
static THREADLOCAL const char *panicmsg;
static THREADLOCAL Thread *currentThread;
static THREADLOCAL PSC_ThreadJob *currentJob;

static void panicHandler(const char *msg) ATTR_NONNULL((1));
static void stopThreads(int nthr);
static void threadJobDone(void *arg);
static void *worker(void *arg);
static void workerInterrupt(int signum);

static void workerInterrupt(int signum)
{
    (void) signum;
}

#ifdef HAVE_UCONTEXT
static void runThreadJob(void)
{
    PSC_ThreadJob *job = currentJob;
    job->proc(job->arg);
    setcontext(&job->caller);
}
#endif

static JobQueue *JobQueue_create(unsigned sz)
{
    JobQueue *self = PSC_malloc(sizeof *self + sz * sizeof *self->jobs);
    self->sz = sz;
    self->avail = sz;
    self->enqpos = 0;
    self->deqpos = 0;
    if (sem_init(&self->count, 0, 0) != 0)
    {
	free(self);
	return 0;
    }
#ifdef THRP_NO_ATOMICS
    if (pthread_mutex_init(&self->lock, 0) != 0)
    {
	sem_destroy(&self->count);
	free(self);
	return 0;
    }
#else
    memset(self->jobs, 0, sz * sizeof *self->jobs);
#endif
    return self;
}

#ifdef THRP_NO_ATOMICS
static int JobQueue_enqueue(JobQueue *self, PSC_ThreadJob *job)
{
    pthread_mutex_lock(&self->lock);
    if (!self->avail)
    {
	pthread_mutex_unlock(&self->lock);
	return -1;
    }
    --self->avail;
    self->jobs[self->enqpos++] = job;
    if (self->enqpos == self->sz) self->enqpos = 0;
    sem_post(&self->count);
    pthread_mutex_unlock(&self->lock);
    return 0;
}

static PSC_ThreadJob *JobQueue_dequeue(JobQueue *self)
{
    if (sem_wait(&self->count) < 0) return 0;
    pthread_mutex_lock(&self->lock);
    PSC_ThreadJob *job = self->jobs[self->deqpos++];
    if (self->deqpos == self->sz) self->deqpos = 0;
    ++self->avail;
    pthread_mutex_unlock(&self->lock);
    return job;
}
#else
static int JobQueue_enqueue(JobQueue *self, PSC_ThreadJob *job)
{
    unsigned avail = atomic_load_explicit(&self->avail, memory_order_consume);
    do
    {
	if (!avail) return -1;
    } while (!atomic_compare_exchange_strong_explicit(&self->avail, &avail,
		avail - 1, memory_order_release, memory_order_consume));

    unsigned next;
    unsigned enqpos = atomic_load_explicit(&self->enqpos,
	    memory_order_consume);
    do
    {
	next = enqpos + 1;
	if (next == self->sz) next = 0;
    } while (!atomic_compare_exchange_strong_explicit(&self->enqpos, &enqpos,
		next, memory_order_release, memory_order_consume));
    atomic_store_explicit(self->jobs + enqpos, job, memory_order_release);
    sem_post(&self->count);
    return 0;
}

static PSC_ThreadJob *JobQueue_dequeue(JobQueue *self)
{
    if (sem_wait(&self->count) < 0) return 0;
    unsigned next;
    unsigned deqpos = atomic_load_explicit(&self->deqpos,
	    memory_order_consume);
    do
    {
	next = deqpos + 1;
	if (next == self->sz) next = 0;
    } while (!atomic_compare_exchange_strong_explicit(&self->deqpos, &deqpos,
		next, memory_order_release, memory_order_consume));
    PSC_ThreadJob *job;
    while (!(job = atomic_exchange_explicit(self->jobs + deqpos, 0,
		    memory_order_acq_rel))) ;
    atomic_fetch_add_explicit(&self->avail, 1, memory_order_release);
    return job;
}
#endif

static void JobQueue_destroy(JobQueue *self)
{
    if (!self) return;
#ifdef THRP_NO_ATOMICS
    pthread_mutex_destroy(&self->lock);
#endif
    sem_destroy(&self->count);
    free(self);
}

static void workerDone(void *arg)
{
    Thread *t = arg;
    t->pthrno = -1;
    pthread_join(t->handle, 0);
    sem_destroy(&t->stop);
    if (--rthreads) return;
    free(threads);
    threads = 0;
    JobQueue_destroy(jobQueue);
    PSC_Service_unregisterPanic(panicHandler);
    mainthread = 0;
#ifdef HAVE_UCONTEXT
    StackMgr_clean();
#endif
    PSC_Service_shutdownUnlock();
}

static int checkpanic(void)
{
    if (setjmp(panicjmp))
    {
	if (currentJob)
	{
	    currentJob->panicmsg = panicmsg;
	    PSC_Service_runOnThread(currentJob->thrno,
		    threadJobDone, currentJob);
	}
	return 1;
    }
    return 0;
}

static void *worker(void *arg)
{
    Thread *t = arg;
    currentThread = t;

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = workerInterrupt;
    sigemptyset(&handler.sa_mask);
    sigaddset(&handler.sa_mask, SIGUSR1);
    if (sigaction(SIGUSR1, &handler, 0) < 0) return 0;
    if (pthread_sigmask(SIG_UNBLOCK, &handler.sa_mask, 0) < 0) return 0;

    if (!checkpanic()) for (;;)
    {
	currentJob = JobQueue_dequeue(jobQueue);
	int stopped = 0;
	sem_getvalue(&t->stop, &stopped);
	if (stopped) break;
	if (!currentJob) continue;
#ifdef THRP_NO_ATOMICS
	pthread_mutex_lock(&currentJob->lock);
	if (currentJob->hasCompleted)
	{
	    currentJob->pthrno = t->pthrno;
	    pthread_mutex_unlock(&currentJob->lock);
#else
	if (atomic_load_explicit(&currentJob->hasCompleted,
		    memory_order_consume))
	{
	    atomic_store_explicit(&currentJob->pthrno, t->pthrno,
		    memory_order_release);
#endif
#ifdef HAVE_UCONTEXT
	    if (!currentJob->async) currentJob->proc(currentJob->arg);
	    else if (currentJob->task)
	    {
		swapcontext(&currentJob->caller, &currentJob->task->resume);
	    }
	    else
	    {
		getcontext(&t->context);
		if (!currentJob->stack) currentJob->stack = StackMgr_getStack();
		t->context.uc_stack.ss_sp = currentJob->stack;
		t->context.uc_stack.ss_size = StackMgr_size();
		t->context.uc_link = 0;
		makecontext(&t->context, runThreadJob, 0);
		swapcontext(&currentJob->caller, &t->context);
	    }
#else
	    currentJob->proc(currentJob->arg);
#endif
#ifdef THRP_NO_ATOMICS
	    pthread_mutex_lock(&currentJob->lock);
	    currentJob->pthrno = -1;
	    pthread_mutex_unlock(&currentJob->lock);
#else
	    atomic_store_explicit(&currentJob->pthrno, -1,
		    memory_order_release);
#endif
	}
#ifdef THRP_NO_ATOMICS
	else pthread_mutex_unlock(&currentJob->lock);
#endif
	PSC_Service_runOnThread(currentJob->thrno, threadJobDone, currentJob);
	currentJob = 0;
    }

    PSC_Service_runOnThread(-1, workerDone, t);
    return 0;
}

SOEXPORT PSC_ThreadJob *PSC_ThreadJob_create(
	PSC_ThreadProc proc, void *arg, int timeoutMs)
{
    PSC_ThreadJob *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
#ifdef THRP_NO_ATOMICS
    if (pthread_mutex_init(&self->lock, 0) != 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "threadpool: cannot create thread job lock");
	free(self);
	return 0;
    }
#endif
    self->proc = proc;
    self->arg = arg;
    self->finished = PSC_Event_create(self);
    self->timeoutMs = timeoutMs;
    self->hasCompleted = 1;
    self->pthrno = -1;
    return self;
}

SOEXPORT void PSC_ThreadJob_setAsync(PSC_ThreadJob *self)
{
#ifdef HAVE_UCONTEXT
    self->async = 1;
#else
    (void)self;
#endif
}

SOEXPORT PSC_Event *PSC_ThreadJob_finished(PSC_ThreadJob *self)
{
    return self->finished;
}

SOEXPORT int PSC_ThreadJob_hasCompleted(const PSC_ThreadJob *self)
{
    return self->hasCompleted;
}

SOEXPORT void PSC_ThreadJob_destroy(PSC_ThreadJob *self)
{
    if (!self) return;
#ifdef HAVE_UCONTEXT
    StackMgr_returnStack(self->stack);
#endif
#ifdef THRP_NO_ATOMICS
    pthread_mutex_destroy(&self->lock);
#endif
    PSC_Timer_destroy(self->timeout);
    PSC_Event_destroy(self->finished);
    free(self);
}

SOEXPORT int PSC_ThreadJob_canceled(void)
{
    if (!currentJob) return 1;
    return !PSC_ThreadJob_hasCompleted(currentJob);
}

SOEXPORT int PSC_AsyncTask_awaitIsBlocking(void)
{
#ifdef HAVE_UCONTEXT
    return 0;
#else
    return 1;
#endif
}

SOEXPORT PSC_AsyncTask *PSC_AsyncTask_create(PSC_AsyncTaskJob job)
{
    PSC_AsyncTask *self = PSC_malloc(sizeof *self);
    self->job = job;
    self->threadJob = 0;
    self->thread = 0;
    self->arg = 0;
    self->result = 0;
    return self;
}

SOEXPORT void *PSC_AsyncTask_await(PSC_AsyncTask *self, void *arg)
{
    self->thread = currentThread;
    self->threadJob = currentJob;
    self->threadJob->task = self;
    self->arg = arg;

#ifdef HAVE_UCONTEXT
    if (self->threadJob->async)
    {
	swapcontext(&self->resume, &self->threadJob->caller);
    }
    else
#endif
    {
	if (sem_init(&self->complete, 0, 0) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: cannot create semaphore "
		    "for async task, skipping execution!");
	    free(self);
	    return 0;
	}
	PSC_Service_runOnThread(self->threadJob->thrno,
		threadJobDone, self->threadJob);
	sem_wait(&self->complete);
    }
    void *result = self->result;
    self->threadJob->task = 0;
#ifdef HAVE_UCONTEXT
    if (!self->threadJob->async)
#endif
    {
	sem_destroy(&self->complete);
    }
    free(self);
    return result;
}

SOEXPORT void *PSC_AsyncTask_arg(PSC_AsyncTask *self)
{
    return self->arg;
}

SOEXPORT void PSC_AsyncTask_complete(PSC_AsyncTask *self, void *result)
{
    self->result = result;
    if (self->threadJob->timeout)
    {
	PSC_Timer_start(self->threadJob->timeout, 0);
    }
#ifdef HAVE_UCONTEXT
    if (self->threadJob->async) JobQueue_enqueue(jobQueue, self->threadJob);
    else
#endif
    {
	sem_post(&self->complete);
    }
}

static void stopThreads(int nthr)
{
    for (int i = 0; i < nthr; ++i)
    {
	if (threads[i].pthrno >= 0)
	{
	    sem_post(&threads[i].stop);
	    pthread_kill(threads[i].handle, SIGUSR1);
	}
    }
}

static void jobTimeout(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_ThreadPool_cancel(receiver);
}

static void threadJobDone(void *arg)
{
    PSC_ThreadJob *job = arg;
    if (job->timeout) PSC_Timer_stop(job->timeout);
    if (job->panicmsg)
    {
	const char *msg = job->panicmsg;
	PSC_ThreadJob_destroy(job);
	PSC_Service_panic(msg);
    }
    if (job->task)
    {
	job->task->job(job->task);
    }
    else
    {
	PSC_Event_raise(job->finished, 0, job->arg);
	PSC_ThreadJob_destroy(job);
    }
}

static void panicHandler(const char *msg)
{
    if (!threads || mainthread) return;
    panicmsg = msg;
    longjmp(panicjmp, -1);
}

SOEXPORT void PSC_ThreadOpts_init(int defThreads)
{
    memset(&opts, 0, sizeof opts);
    opts.defNThreads = defThreads;
    opts.maxThreads = MAXTHREADS;
    opts.nPerCpu = THREADSPERCPU;
    opts.maxQueueLen = MAXQUEUELEN;
    opts.minQueueLen = MINQUEUELEN;
    opts.qLenPerThread = QLENPERTHREAD;
}

SOEXPORT void PSC_ThreadOpts_fixedThreads(int n)
{
    opts.nThreads = n;
}

SOEXPORT void PSC_ThreadOpts_threadsPerCpu(int n)
{
    opts.nPerCpu = n;
}

SOEXPORT void PSC_ThreadOpts_maxThreads(int n)
{
    opts.maxThreads = n;
}

SOEXPORT void PSC_ThreadOpts_fixedQueue(int n)
{
    opts.queueLen = n;
}

SOEXPORT void PSC_ThreadOpts_queuePerThread(int n)
{
    opts.qLenPerThread = n;
}

SOEXPORT void PSC_ThreadOpts_maxQueue(int n)
{
    opts.maxQueueLen = n;
}

SOEXPORT void PSC_ThreadOpts_minQueue(int n)
{
    opts.minQueueLen = n;
}

SOEXPORT int PSC_ThreadPool_init(void)
{
    sigset_t blockmask;
    sigset_t mask;
    sigfillset(&blockmask);
    int rc = -1;
    
    if (threads) return rc;
    if (!opts.defNThreads) PSC_ThreadOpts_init(DEFTHREADS);

    if (sigprocmask(SIG_BLOCK, &blockmask, &mask) < 0)
    {
	PSC_Log_err(PSC_L_ERROR, "threadpool: cannot set signal mask");
	return rc;
    }

    if (opts.nThreads)
    {
	nthreads = opts.nThreads;
    }
    else
    {
#ifdef _SC_NPROCESSORS_CONF
	long ncpu = sysconf(_SC_NPROCESSORS_CONF);
	if (ncpu >= 1)
	{
	    if (ncpu <= (opts.maxThreads / opts.nPerCpu))
	    {
		nthreads = opts.nPerCpu * ncpu;
	    }
	    else nthreads = opts.maxThreads;
	}
	else nthreads = opts.defNThreads;
#else
	nthreads = opts.defNThreads;
#endif
    }
    int queuesize;
    if (opts.queueLen)
    {
	queuesize = opts.queueLen;
    }
    else if (nthreads <= (opts.maxQueueLen / opts.qLenPerThread))
    {
	queuesize = opts.qLenPerThread * nthreads;
	if (queuesize < opts.minQueueLen) queuesize = opts.minQueueLen;
    }
    else queuesize = opts.maxQueueLen;

    PSC_Log_fmt(PSC_L_DEBUG, "threadpool: starting with %d threads and a "
	    "queue for %d jobs", nthreads, queuesize);

    threads = PSC_malloc(nthreads * sizeof *threads);
    memset(threads, 0, nthreads * sizeof *threads);
    jobQueue = JobQueue_create(queuesize);

    rthreads = 0;
    for (int i = 0; i < nthreads; ++i)
    {
	if (sem_init(&threads[i].stop, 0, 0) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "threadpool: error creating semaphore");
	    goto rollback;
	}
	if (pthread_create(&threads[i].handle, 0, worker, threads+i) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: error creating thread");
	    goto rollback_cancel;
	}
	threads[i].pthrno = i;
	++rthreads;
	continue;

rollback_cancel:
	sem_destroy(&threads[i].stop);
rollback:
	stopThreads(i);
	goto done;
    }
    rc = 0;

done:
    if (sigprocmask(SIG_SETMASK, &mask, 0) < 0)
    {
	PSC_Log_err(PSC_L_ERROR, "threadpool: cannot restore signal mask");
	if (rc == 0) stopThreads(nthreads);
	rc = -1;
    }

    if (rc == 0)
    {
	mainthread = 1;
	PSC_Service_registerPanic(panicHandler);
    }
    else
    {
	free(threads);
	threads = 0;
	JobQueue_destroy(jobQueue);
	jobQueue = 0;
    }

    return rc;
}

SOEXPORT int PSC_ThreadPool_active(void)
{
    return !!threads;
}

SOEXPORT int PSC_ThreadPool_enqueue(PSC_ThreadJob *job)
{
    job->thrno = PSC_Service_threadNo();
    if (job->timeoutMs && !job->timeout)
    {
	job->timeout = PSC_Timer_create();
	if (!job->timeout) return -1;
	PSC_Event_register(PSC_Timer_expired(job->timeout), job,
		jobTimeout, 0);
    }
    if (JobQueue_enqueue(jobQueue, job) < 0) return -1;
    if (job->timeout)
    {
	PSC_Timer_setMs(job->timeout, job->timeoutMs);
	PSC_Timer_start(job->timeout, 0);
    }
    return 0;
}

SOEXPORT void PSC_ThreadPool_cancel(PSC_ThreadJob *job)
{
#ifdef THRP_NO_ATOMICS
    pthread_mutex_lock(&job->lock);
    job->hasCompleted = 0;
    if (job->pthrno >= 0) pthread_kill(threads[job->pthrno].handle, SIGUSR1);
    pthread_mutex_unlock(&job->lock);
#else
    atomic_store_explicit(&job->hasCompleted, 0, memory_order_release);
    int pthrno;
    if ((pthrno = atomic_load_explicit(
		    &job->pthrno, memory_order_consume)) >= 0)
    {
	pthread_kill(threads[pthrno].handle, SIGUSR1);
    }
#endif
}

SOEXPORT void PSC_ThreadPool_done(void)
{
    if (!threads) return;
    stopThreads(nthreads);
    PSC_Service_shutdownLock();
}
