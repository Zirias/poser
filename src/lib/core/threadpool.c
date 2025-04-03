#define _DEFAULT_SOURCE

#include <poser/core/event.h>
#include <poser/core/log.h>
#include <poser/core/service.h>
#include <poser/core/threadpool.h>
#include <poser/core/util.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__) || defined(__FreeBSD__)
#include <threads.h>
#define THREADLOCAL thread_local
#else
#define THREADLOCAL __thread
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
    const char *panicmsg;
    int hasCompleted;
    int timeoutTicks;
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
    pthread_t handle;
    pthread_mutex_t startlock;
    pthread_mutex_t donelock;
    pthread_cond_t start;
    pthread_cond_t done;
    sem_t cancel;
    int pipefd[2];
    int stoprq;
} Thread;

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
static void checkThreadJobs(void *receiver, void *sender, void *args);
static PSC_ThreadJob *dequeueJob(void);
static int enqueueJob(PSC_ThreadJob *job) ATTR_NONNULL((1));
static void panicHandler(const char *msg) ATTR_NONNULL((1));
static void startThreadJob(Thread *t, PSC_ThreadJob *j)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));
static void stopThreads(int nthr);
static void threadJobDone(void *receiver, void *sender, void *args);
static void *worker(void *arg);
static void workerInterrupt(int signum);

static void workerInterrupt(int signum)
{
    (void) signum;
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

    if (pthread_mutex_lock(&t->startlock) < 0) return 0;
    t->stoprq = 0;
    while (!t->stoprq)
    {
	sem_trywait(&t->cancel);
	pthread_cond_wait(&t->start, &t->startlock);
	if (t->stoprq) break;
	if (!setjmp(panicjmp)) t->job->proc(t->job->arg);
	else t->job->panicmsg = panicmsg;
	if (write(t->pipefd[1], "0", 1) < 1)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: can't notify main thread");
	    return 0;
	}
	pthread_mutex_lock(&t->donelock);
	pthread_cond_signal(&t->done);
	pthread_mutex_unlock(&t->donelock);
    }

    pthread_mutex_unlock(&t->startlock);
    return 0;
}

SOEXPORT PSC_ThreadJob *PSC_ThreadJob_create(
	PSC_ThreadProc proc, void *arg, int timeoutTicks)
{
    PSC_ThreadJob *self = PSC_malloc(sizeof *self);
    self->proc = proc;
    self->arg = arg;
    self->finished = PSC_Event_create(self);
    self->panicmsg = 0;
    self->timeoutTicks = timeoutTicks;
    self->hasCompleted = 1;
    return self;
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
    PSC_Event_destroy(self->finished);
    free(self);
}

SOEXPORT int PSC_ThreadJob_canceled(void)
{
    return sem_trywait(&currentThread->cancel) == 0;
}

static void stopThreads(int nthr)
{
    for (int i = 0; i < nthr; ++i)
    {
	if (pthread_kill(threads[i].handle, 0) >= 0)
	{
	    if (pthread_mutex_trylock(&threads[i].startlock) != 0)
	    {
		threads[i].stoprq = 1;
		pthread_kill(threads[i].handle, SIGUSR1);
		pthread_cond_wait(&threads[i].done, &threads[i].donelock);
		pthread_mutex_unlock(&threads[i].donelock);
	    }
	    else
	    {
		threads[i].stoprq = 1;
		pthread_cond_signal(&threads[i].start);
		pthread_mutex_unlock(&threads[i].startlock);
	    }
	}
	pthread_join(threads[i].handle, 0);
	close(threads[i].pipefd[0]);
	close(threads[i].pipefd[1]);
	sem_destroy(&threads[i].cancel);
	pthread_cond_destroy(&threads[i].done);
	pthread_mutex_destroy(&threads[i].donelock);
	pthread_cond_destroy(&threads[i].start);
	pthread_mutex_destroy(&threads[i].startlock);
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
    for (int i = 0; i < nthreads; ++i)
    {
	if (!threads[i].job) return threads+i;
    }
    return 0;
}

static void startThreadJob(Thread *t, PSC_ThreadJob *j)
{
    if (pthread_kill(t->handle, 0) == ESRCH)
    {
	pthread_join(t->handle, 0);
	PSC_Log_msg(PSC_L_WARNING, "threadpool: restarting failed thread");
	if (pthread_create(&t->handle, 0, worker, t) < 0)
	{
	    PSC_Log_msg(PSC_L_FATAL, "threadpool: error restarting thread");
	    PSC_Service_quit();
	}
	return;
    }
    pthread_mutex_lock(&t->startlock);
    t->job = j;
    PSC_Service_registerRead(t->pipefd[0]);
    pthread_cond_signal(&t->start);
    pthread_mutex_lock(&t->donelock);
    pthread_mutex_unlock(&t->startlock);
}

static void threadJobDone(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Thread *t = receiver;
    PSC_Service_unregisterRead(t->pipefd[0]);
    char buf[2];
    if (read(t->pipefd[0], buf, sizeof buf) <= 0)
    {
	PSC_Service_panic("threadpool: error reading internal pipe");
    }
    pthread_cond_wait(&t->done, &t->donelock);
    if (t->job->panicmsg)
    {
	const char *msg = t->job->panicmsg;
	PSC_ThreadJob_destroy(t->job);
	t->job = 0;
	pthread_mutex_unlock(&t->donelock);
	PSC_Service_panic(msg);
    }
    PSC_Event_raise(t->job->finished, 0, t->job->arg);
    PSC_ThreadJob_destroy(t->job);
    t->job = 0;
    pthread_mutex_unlock(&t->donelock);
    PSC_ThreadJob *next = dequeueJob();
    if (next) startThreadJob(t, next);
}

static void checkThreadJobs(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    for (int i = 0; i < nthreads; ++i)
    {
	if (threads[i].job && threads[i].job->timeoutTicks
		&& !--threads[i].job->timeoutTicks)
	{
	    pthread_kill(threads[i].handle, SIGUSR1);
	    threads[i].job->hasCompleted = 0;
	}
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
	if (pthread_mutex_init(&threads[i].startlock, 0) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: error creating mutex");
	    goto rollback;
	}
	if (pthread_cond_init(&threads[i].start, 0) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "threadpool: error creating condition variable");
	    goto rollback_startlock;
	}
	if (pthread_mutex_init(&threads[i].donelock, 0) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: error creating mutex");
	    goto rollback_start;
	}
	if (pthread_cond_init(&threads[i].done, 0) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "threadpool: error creating condition variable");
	    goto rollback_donelock;
	}
	if (sem_init(&threads[i].cancel, 0, 0) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "threadpool: error creating semaphore");
	    goto rollback_cancel;
	}
	if (pipe(threads[i].pipefd) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: error creating pipe");
	    goto rollback_done;
	}
	PSC_Event_register(PSC_Service_readyRead(), threads+i, threadJobDone,
		threads[i].pipefd[0]);
	if (pthread_create(&threads[i].handle, 0, worker, threads+i) != 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "threadpool: error creating thread");
	    PSC_Event_unregister(PSC_Service_readyRead(), threads+i,
		    threadJobDone, threads[i].pipefd[0]);
	    goto rollback_pipe;
	}
	continue;

rollback_pipe:
	close(threads[i].pipefd[0]);
	close(threads[i].pipefd[1]);
rollback_cancel:
	sem_destroy(&threads[i].cancel);
rollback_done:
	pthread_cond_destroy(&threads[i].done);
rollback_donelock:
	pthread_mutex_destroy(&threads[i].donelock);
rollback_start:
	pthread_cond_destroy(&threads[i].start);
rollback_startlock:
	pthread_mutex_destroy(&threads[i].startlock);
rollback:
	stopThreads(i);
	goto done;
    }
    rc = 0;
    PSC_Event_register(PSC_Service_tick(), 0, checkThreadJobs, 0);
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
    if (mainthread && threads)
    {
	Thread *t = availableThread();
	if (t)
	{
	    startThreadJob(t, job);
	    return 0;
	}
    }
    return enqueueJob(job);
}

SOEXPORT void PSC_ThreadPool_cancel(PSC_ThreadJob *job)
{
    if (threads)
    {
	for (int i = 0; i < nthreads; ++i)
	{
	    if (threads[i].job == job)
	    {
		threads[i].job->hasCompleted = 0;
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
		job->hasCompleted = 0;
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
}
