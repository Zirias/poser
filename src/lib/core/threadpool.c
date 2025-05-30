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

#ifdef HAVE_UCONTEXT
#  include "stackmgr.h"
#  include <poser/core/list.h>
#  include <poser/core/queue.h>
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

#ifndef DEFJOBSTACKKB
#define DEFJOBSTACKKB 64
#endif

struct PSC_ThreadJob
{
    PSC_ThreadProc proc;
    void *arg;
    PSC_Event *finished;
    PSC_Timer *timeout;
    PSC_AsyncTask *task;
    const char *panicmsg;
    int hasCompleted;
    int timeoutTicks;
    int thrno;
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

typedef struct Thread
{
    PSC_ThreadJob *job;
#ifdef HAVE_UCONTEXT
    PSC_Queue *finishedTasks;
    PSC_List *waitingTasks;
#endif
    pthread_t handle;
    pthread_mutex_t lock;
    sem_t start;
    sem_t cancel;
    int stoprq;
#ifdef HAVE_UCONTEXT
    ucontext_t context;
#endif
} Thread;

struct PSC_AsyncTask
{
    PSC_AsyncTaskJob job;
    PSC_ThreadJob *threadJob;
    Thread *thread;
    void *arg;
    void *result;
#ifdef HAVE_UCONTEXT
    ucontext_t resume;
#endif
};

static PSC_ThreadOpts opts;
static Thread *threads;
static PSC_ThreadJob **jobQueue;
static pthread_mutex_t queuelock;
static int nthreads;
static int queuesize;
static int queueAvail;
static int nextIdx;
static int lastIdx;

static THREADLOCAL int mainthread;
static THREADLOCAL jmp_buf panicjmp;
static THREADLOCAL const char *panicmsg;
static THREADLOCAL Thread *currentThread;

static Thread *availableThread(void);
static PSC_ThreadJob *dequeueJob(void);
static int enqueueJob(PSC_ThreadJob *job) ATTR_NONNULL((1));
static void panicHandler(const char *msg) ATTR_NONNULL((1));
static void startThreadJob(Thread *t, PSC_ThreadJob *j)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));
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
    currentThread->job->proc(currentThread->job->arg);
}
#endif

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

    for (;;)
    {
	sem_wait(&t->start);
	pthread_mutex_lock(&t->lock);
	int iscanceled = 0;
	sem_getvalue(&t->cancel, &iscanceled);
	if (iscanceled) sem_trywait(&t->cancel);
	if (t->stoprq) break;
	if (!setjmp(panicjmp))
	{
#ifdef HAVE_UCONTEXT
	    if (!t->job->async) t->job->proc(t->job->arg);
	    else if (t->job->task)
	    {
		swapcontext(&t->job->caller, &t->job->task->resume);
	    }
	    else
	    {
		getcontext(&t->context);
		t->context.uc_stack.ss_sp = t->job->stack;
		t->context.uc_stack.ss_size = StackMgr_size();
		t->context.uc_link = &t->job->caller;
		makecontext(&t->context, runThreadJob, 0);
		swapcontext(&t->job->caller, &t->context);
	    }
#else
	    t->job->proc(t->job->arg);
#endif
	}
	else t->job->panicmsg = panicmsg;
	PSC_Service_runOnThread(t->job->thrno, threadJobDone, t);
	pthread_mutex_unlock(&t->lock);
    }

    pthread_mutex_unlock(&t->lock);
    return 0;
}

SOEXPORT PSC_ThreadJob *PSC_ThreadJob_create(
	PSC_ThreadProc proc, void *arg, int timeoutTicks)
{
    PSC_ThreadJob *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->proc = proc;
    self->arg = arg;
    self->finished = PSC_Event_create(self);
    self->timeoutTicks = timeoutTicks;
    self->hasCompleted = 1;
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
    PSC_Timer_destroy(self->timeout);
    PSC_Event_destroy(self->finished);
    free(self);
}

SOEXPORT int PSC_ThreadJob_canceled(void)
{
    if (!currentThread->job->hasCompleted) return 1;
    int rc = 0;
    sem_getvalue(&currentThread->cancel, &rc);
    return rc;
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
    self->threadJob = currentThread->job;
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
	PSC_Service_runOnThread(self->threadJob->thrno,
		threadJobDone, self->thread);
	pthread_mutex_unlock(&currentThread->lock);
	sem_wait(&currentThread->start);
	pthread_mutex_lock(&currentThread->lock);
    }
    void *result = self->result;
    self->threadJob->task = 0;
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
#ifdef HAVE_UCONTEXT
    if (self->threadJob->async)
    {
	pthread_mutex_lock(&self->thread->lock);
	if (self->thread->job)
	{
	    PSC_Queue_enqueue(self->thread->finishedTasks, self->threadJob, 0);
	    pthread_mutex_unlock(&self->thread->lock);
	    return;
	}
	PSC_List_remove(self->thread->waitingTasks, self->threadJob);
	startThreadJob(self->thread, self->threadJob);
	pthread_mutex_unlock(&self->thread->lock);
    }
    else
#endif
    {
	if (self->threadJob->timeout)
	{
	    PSC_Timer_setMs(self->threadJob->timeout,
		    500 * self->threadJob->timeoutTicks);
	    PSC_Timer_start(self->threadJob->timeout, 0);
	}
	pthread_mutex_lock(&self->thread->lock);
	sem_post(&self->thread->start);
	pthread_mutex_unlock(&self->thread->lock);
    }
}

static void stopThreads(int nthr)
{
    for (int i = 0; i < nthr; ++i)
    {
	if (pthread_kill(threads[i].handle, 0) >= 0)
	{
	    if (pthread_mutex_trylock(&threads[i].lock) != 0)
	    {
		pthread_kill(threads[i].handle, SIGUSR1);
		pthread_mutex_lock(&threads[i].lock);
	    }
	    threads[i].stoprq = 1;
	    pthread_mutex_unlock(&threads[i].lock);
	    sem_post(&threads[i].start);
	}
	pthread_join(threads[i].handle, 0);
	sem_destroy(&threads[i].cancel);
	sem_destroy(&threads[i].start);
	pthread_mutex_destroy(&threads[i].lock);
#ifdef HAVE_UCONTEXT
	PSC_List_destroy(threads[i].waitingTasks);
	PSC_Queue_destroy(threads[i].finishedTasks);
#endif
    }
}

static int enqueueJob(PSC_ThreadJob *job)
{
    int rc = -1;
    pthread_mutex_lock(&queuelock);
    if (!queueAvail) goto done;
    rc = 0;
    jobQueue[nextIdx++] = job;
    --queueAvail;
    if (nextIdx == queuesize) nextIdx = 0;
done:
    pthread_mutex_unlock(&queuelock);
    return rc;
}

static PSC_ThreadJob *dequeueJob(void)
{
    PSC_ThreadJob *job = 0;
    pthread_mutex_lock(&queuelock);
    while (!job)
    {
	if (queueAvail == queuesize) break;
	job = jobQueue[lastIdx];
	jobQueue[lastIdx++] = 0;
	++queueAvail;
	if (lastIdx == queuesize) lastIdx = 0;
    }
    pthread_mutex_unlock(&queuelock);
    return job;
}

static Thread *availableThread(void)
{
    Thread *fallback = 0;
    for (int i = 0; i < nthreads; ++i)
    {
	if (pthread_mutex_trylock(&threads[i].lock) != 0) continue;
	if (threads[i].job)
	{
	    pthread_mutex_unlock(&threads[i].lock);
	    continue;
	}
#ifdef HAVE_UCONTEXT
	if (PSC_List_size(threads[i].waitingTasks) > 0)
	{
	    if (!fallback) fallback = threads+i;
	    else pthread_mutex_unlock(&threads[i].lock);
	}
	else
	{
	    if (fallback) pthread_mutex_unlock(&fallback->lock);
	    return threads+i;
	}
#else
	return threads+i;
#endif
    }
    return fallback;
}

static void jobTimeout(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_ThreadPool_cancel(receiver);
}

static void setupjobtimeout(void *arg)
{
    PSC_ThreadJob *j = arg;
    if (!j->timeout)
    {
	j->timeout = PSC_Timer_create();
	PSC_Event_register(PSC_Timer_expired(j->timeout), j,
		jobTimeout, 0);
    }
    PSC_Timer_setMs(j->timeout, 500 * j->timeoutTicks);
    PSC_Timer_start(j->timeout, 0);
}

static void startThreadJob(Thread *t, PSC_ThreadJob *j)
{
    if (j->timeoutTicks) PSC_Service_runOnThread(j->thrno, setupjobtimeout, j);
#ifdef HAVE_UCONTEXT
    if (j->async && !j->stack) j->stack = StackMgr_getStack();
#endif
    t->job = j;
    sem_post(&t->start);
    pthread_mutex_unlock(&t->lock);
}

static void threadJobDone(void *arg)
{
    Thread *t = arg;
    pthread_mutex_lock(&t->lock);
#ifdef HAVE_UCONTEXT
    int async = t->job->async;
#endif
    if (t->job->timeout) PSC_Timer_stop(t->job->timeout);
    if (t->job->panicmsg)
    {
	const char *msg = t->job->panicmsg;
	PSC_ThreadJob_destroy(t->job);
	t->job = 0;
	pthread_mutex_unlock(&t->lock);
	PSC_Service_panic(msg);
    }
    PSC_AsyncTask *task = t->job->task;
    if (task)
    {
#ifdef HAVE_UCONTEXT
	if (async)
	{
	    PSC_List_append(t->waitingTasks, t->job, 0);
	}
	else
#endif
	{
	    pthread_mutex_unlock(&t->lock);
	    task->job(task);
	    return;
	}
    }
    else
    {
	PSC_Event_raise(t->job->finished, 0, t->job->arg);
	PSC_ThreadJob_destroy(t->job);
    }
    t->job = 0;
    PSC_ThreadJob *next = 0;
#ifdef HAVE_UCONTEXT
    if (async)
    {
	next = PSC_Queue_dequeue(t->finishedTasks);
	if (next) PSC_List_remove(t->waitingTasks, next);
	else next = dequeueJob();
    }
    else
#endif
    {
	next = dequeueJob();
    }
    if (next) startThreadJob(t, next);
    pthread_mutex_unlock(&t->lock);
    if (task) task->job(task);
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
	PSC_Log_msg(PSC_L_ERROR, "threadpool: cannot set signal mask");
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
    jobQueue = PSC_malloc(queuesize * sizeof *jobQueue);
    memset(jobQueue, 0, queuesize * sizeof *jobQueue);

    for (int i = 0; i < nthreads; ++i)
    {
	if (pthread_mutex_init(&threads[i].lock, 0) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: error creating mutex");
	    goto rollback;
	}
	if (sem_init(&threads[i].start, 0, 0) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "threadpool: error creating semaphore");
	    goto rollback_lock;
	}
	if (sem_init(&threads[i].cancel, 0, 0) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "threadpool: error creating semaphore");
	    goto rollback_start;
	}
	if (pthread_create(&threads[i].handle, 0, worker, threads+i) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: error creating thread");
	    goto rollback_cancel;
	}
	threads[i].stoprq = 0;
#ifdef HAVE_UCONTEXT
	threads[i].waitingTasks = PSC_List_create();
	threads[i].finishedTasks = PSC_Queue_create();
#endif
	continue;

rollback_cancel:
	sem_destroy(&threads[i].cancel);
rollback_start:
	sem_destroy(&threads[i].start);
rollback_lock:
	pthread_mutex_destroy(&threads[i].lock);
rollback:
	stopThreads(i);
	goto done;
    }
    rc = 0;
    queueAvail = queuesize;
    nextIdx = 0;
    lastIdx = 0;
    if (pthread_mutex_init(&queuelock, 0) < 0)
    {
	stopThreads(nthreads);
	rc = -1;
    }

done:
    if (sigprocmask(SIG_SETMASK, &mask, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "threadpool: cannot restore signal mask");
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
	free(jobQueue);
	jobQueue = 0;
	queueAvail = 0;
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
    Thread *t = availableThread();
    if (t)
    {
	startThreadJob(t, job);
	return 0;
    }
    return enqueueJob(job);
}

SOEXPORT void PSC_ThreadPool_cancel(PSC_ThreadJob *job)
{
    job->hasCompleted = 0;
    if (job->task) return;

    if (threads)
    {
	for (int i = 0; i < nthreads; ++i)
	{
	    if (threads[i].job == job)
	    {
		sem_post(&threads[i].cancel);
		pthread_kill(threads[i].handle, SIGUSR1);
		return;
	    }
	}
    }
    if (queueAvail != queuesize)
    {
	int i = lastIdx;
	do
	{
	    if (jobQueue[i] == job)
	    {
		PSC_Event_raise(job->finished, 0, job->arg);
		PSC_ThreadJob_destroy(job);
		jobQueue[i] = 0;
		return;
	    }
	    if (++i == queuesize) i = 0;
	} while ( i != nextIdx);
    }
}

SOEXPORT void PSC_ThreadPool_done(void)
{
    if (!threads) return;
    stopThreads(nthreads);
    free(threads);
    threads = 0;
    pthread_mutex_destroy(&queuelock);
    for (int i = 0; i < queuesize; ++i) PSC_ThreadJob_destroy(jobQueue[i]);
    free(jobQueue);
    jobQueue = 0;
    queueAvail = 0;
    PSC_Service_unregisterPanic(panicHandler);
    mainthread = 0;
#ifdef HAVE_UCONTEXT
    StackMgr_clean();
#endif
}
