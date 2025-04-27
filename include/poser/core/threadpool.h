#ifndef POSER_CORE_THREADPOOL_H
#define POSER_CORE_THREADPOOL_H

/** declarations for the PSC_ThreadPool and related classes
 * @file
 */

#include <poser/decl.h>

/** A job to be executed on a worker thread.
 * @class PSC_ThreadJob threadpool.h <poser/core/threadpool.h>
 */
C_CLASS_DECL(PSC_ThreadJob);

/** An asynchronous task a thread job can wait for.
 * When the library is built on a system supporting user context switching
 * with POSIX-1.2001 getcontext() and friends, the thread is released for
 * other thread jobs while waiting, otherwise the worker thread will be
 * blocked.
 *
 * No destructor is offered, the task destroys itself on completion.
 * @class PSC_AsyncTask threadpool.h <poser/core/threadpool.h>
 */
C_CLASS_DECL(PSC_AsyncTask);

/** Options for PSC_ThreadPool.
 * This class is for configuring the thread pool. If none of its methods are
 * called, compile-time default values are used.
 * @class PSC_ThreadOpts threadpool.h <poser/core/threadpool.h>
 */

/** A thread pool.
 * This class creates a fixed set of worker threads and a queue of
 * PSC_ThreadJob objects for jobs to be executed on a worker thread. An event
 * will be fired when a job completes. Running jobs can also be canceled.
 * @class PSC_ThreadPool threadpool.h <poser/core/threadpool.h>
 */

C_CLASS_DECL(PSC_Event);

/** A function to run on a worker thread.
 * @param arg the data to work on
 */
typedef void (*PSC_ThreadProc)(void *arg);

/** A function to run for completing an asynchronous task.
 * @param task the task to complete
 */
typedef void (*PSC_AsyncTaskJob)(PSC_AsyncTask *task);

/** Create a new thread job.
 * Creates a new job to be executed on a worker thread. Unless the library was
 * built on a system without POSIX user context switching support, the job
 * executes on its own private stack with a default size of 64 kiB. If that
 * is not enough for the job, make sure to configure a suitable size with
 * PSC_ThreadJob_setStackSize() before scheduling the job to avoid stack
 * overflow crashes.
 * @memberof PSC_ThreadJob
 * @param proc the function to run on the worker thread
 * @param arg the data to work on
 * @param timeoutTicks if non-zero, automatically cancel the job after this
 *                     many "ticks" (multiples of 500ms)
 * @returns a newly created thread job
 */
DECLEXPORT PSC_ThreadJob *
PSC_ThreadJob_create(PSC_ThreadProc proc, void *arg, int timeoutTicks)
    ATTR_NONNULL((1)) ATTR_RETNONNULL;

/** Configure the stack size for a thread job.
 * Configures the size of the private stack for executing this thread job.
 * When set to 0, awaiting a PSC_AsyncTask on this job will block the worker
 * thread it is running on. When the library was built on a system without
 * POSIX user context switching support, the stack size is always 0 and this
 * call is silently ignored.
 *
 * The default size is 64 kiB (64 * 1024), unless user context switching is
 * not available.
 * @memberof PSC_ThreadJob
 * @param self the PSC_ThreadJob
 * @param stackSize the desired stack size in bytes
 */
DECLEXPORT void
PSC_ThreadJob_setStackSize(PSC_ThreadJob *self, size_t stackSize)
    ATTR_NONNULL((1));

/** The job finished.
 * This event fires when the thread job finished, either because it completed
 * or because it was canceled.
 * @memberof PSC_ThreadJob
 * @param self the PSC_ThreadJob
 * @returns the finished event
 */
DECLEXPORT PSC_Event *
PSC_ThreadJob_finished(PSC_ThreadJob *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Determine whether the job completed.
 * This can be called when PSC_ThreadJob_finished() fired to know whether the
 * job completed successfully.
 * @memberof PSC_ThreadJob
 * @param self the PSC_ThreadJob
 * @returns 1 if the job completed, 0 otherwise
 */
DECLEXPORT int
PSC_ThreadJob_hasCompleted(const PSC_ThreadJob *self)
    CMETHOD ATTR_PURE;

/** PSC_ThreadJob destructor.
 * Destroys the thread job.
 *
 * Note that once the job was scheduled, PSC_ThreadPool will automatically
 * destroy it when it completed or is canceled.
 * @memberof PSC_ThreadJob
 * @param self the PSC_ThreadJob
 */
DECLEXPORT void
PSC_ThreadJob_destroy(PSC_ThreadJob *self);

/** Check whether a job was canceled.
 * This must only be called from within a PSC_ThreadProc. It signals whether
 * the job was canceled, in that case, the PSC_ThreadProc should exit quickly.
 * @memberof PSC_ThreadJob
 * @static
 * @returns 1 when the job was canceled, 0 otherwise
 */
DECLEXPORT int
PSC_ThreadJob_canceled(void);

/** Check whether PSC_AsyncTask_await() will block.
 * This method tells at runtime whether awaiting a PSC_AsyncTask will always
 * block the worker thread (which is the case on systems without support for
 * POSIX user context switching), or only when the job's stack size is set to
 * zero. It may be used to configure a suitable number of worker threads per
 * CPU.
 * @memberof PSC_AsyncTask
 * @static
 * @returns 1 if PSC_AsyncTask_await() always blocks, 0 if it only blocks when
 *          a stack size of 0 is configured.
 */
DECLEXPORT int
PSC_AsyncTask_awaitIsBlocking(void);

/** PSC_AsyncTask default constructor.
 * Creates a new PSC_AsyncTask that will call the given function on the main
 * thread when awaited.
 * @memberof PSC_AsyncTask
 * @param job the function to call on the main thread
 * @returns a newly created PSC_AsyncTask
 */
DECLEXPORT PSC_AsyncTask *
PSC_AsyncTask_create(PSC_AsyncTaskJob job);

/** Wait for completion of an async task.
 * @memberof PSC_AsyncTask
 * @param self the PSC_AsyncTask
 * @param arg an optional argument to pass to the async task
 * @returns the result given to PSC_AsyncTask_complete()
 */
DECLEXPORT void *
PSC_AsyncTask_await(PSC_AsyncTask *self, void *arg);

/** Get the argument of an async task.
 * This is meant to be called on the main thread from a PSC_AsyncTaskJob
 * to get the optional argument passed to PSC_AsyncTask_await().
 * @memberof PSC_AsyncTask
 * @param self the PSC_AsyncTask
 * @returns the task's argument
 */
DECLEXPORT void *
PSC_AsyncTask_arg(PSC_AsyncTask *self);

/** Complete an async task.
 * This must be called from the main thread to allow the thread job awaiting
 * the async task to continue.
 * @memberof PSC_AsyncTask
 * @param self the PSC_AsyncTask
 * @param result an optional result to pass to the awaiting job
 */
DECLEXPORT void
PSC_AsyncTask_complete(PSC_AsyncTask *self, void *result);

/** Initialize options with default values.
 * @memberof PSC_ThreadOpts
 * @static
 * @param defThreads default number of threads to create when number of CPUs
 *                   can't be determined
 */
DECLEXPORT void
PSC_ThreadOpts_init(int defThreads);

/** Set a fixed numer of threads.
 * @memberof PSC_ThreadOpts
 * @static
 * @param n always create this many threads
 */
DECLEXPORT void
PSC_ThreadOpts_fixedThreads(int n);

/** Set number of threads per CPU.
 * If the number of CPUs can be determined and there's no fixed number of
 * threads configured, the number of threads created will be a multiple of the
 * number of CPUs. Default is 1.
 * @memberof PSC_ThreadOpts
 * @static
 * @param n create n threads per detected CPU
 */
DECLEXPORT void
PSC_ThreadOpts_threadsPerCpu(int n);

/** Set maximum number of threads.
 * @memberof PSC_ThreadOpts
 * @static
 * @param n never create more than n threads
 */
DECLEXPORT void
PSC_ThreadOpts_maxThreads(int n);

/** Set a fixed queue size for waiting thread jobs.
 * @memberof PSC_ThreadOpts
 * @static
 * @param n fixed size of the queue
 */
DECLEXPORT void
PSC_ThreadOpts_fixedQueue(int n);

/** Set queue size for waiting jobs per thread.
 * If no fixed queue size is configured, the queue size will be a multiple of
 * the number of threads created. Default is 2.
 * @memberof PSC_ThreadOpts
 * @static
 * @param n queue size per thread
 */
DECLEXPORT void
PSC_ThreadOpts_queuePerThread(int n);

/** Set maximum queue size for waiting thread jobs.
 * @memberof PSC_ThreadOpts
 * @static
 * @param n maximum size of the queue
 */
DECLEXPORT void
PSC_ThreadOpts_maxQueue(int n);

/** Set minimum queue size for waiting thread jobs.
 * @memberof PSC_ThreadOpts
 * @static
 * @param n minimum size of the queue
 */
DECLEXPORT void
PSC_ThreadOpts_minQueue(int n);

/** Initialize the thread pool.
 * This launches the worker threads, according to the configuration from
 * PSC_ThreadOpts.
 * @memberof PSC_ThreadPool
 * @static
 * @returns -1 on error, 0 on success
 */
DECLEXPORT int
PSC_ThreadPool_init(void);

/** Check whether thread pool is active.
 * @memberof PSC_ThreadPool
 * @static
 * @returns 1 when thread pool is active, 0 otherwise
 */
DECLEXPORT int
PSC_ThreadPool_active(void);

/** Enqueue a thread job.
 * If a worker thread is available, the job is started on it directly,
 * otherwise it is put in the queue of waiting jobs.
 *
 * Note this takes ownership of the PSC_ThreadJob object.
 * @memberof PSC_ThreadPool
 * @static
 * @param job the job to enqueue/start
 * @returns -1 on error (queue overflow), 0 on success
 */
DECLEXPORT int
PSC_ThreadPool_enqueue(PSC_ThreadJob *job)
    ATTR_NONNULL((1));

/** Cancel a thread job.
 * If the job is already running, it is sent an interrupting signal and have a
 * flag set that can be checked with PSC_ThreadJob_canceled(). If it is still
 * waiting in the queue, it is just removed and destroyed. In any case, its
 * finished event will fire.
 * @memberof PSC_ThreadPool
 * @static
 * @param job the job to cancel
 */
DECLEXPORT void
PSC_ThreadPool_cancel(PSC_ThreadJob *job)
    ATTR_NONNULL((1));

/** Stop the thread pool.
 * All worker threads are stopped.
 * @memberof PSC_ThreadPool
 * @static
 */
DECLEXPORT void
PSC_ThreadPool_done(void);

#endif
