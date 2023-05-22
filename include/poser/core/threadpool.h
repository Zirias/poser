#ifndef POSER_CORE_THREADPOOL_H
#define POSER_CORE_THREADPOOL_H

#include <poser/decl.h>

C_CLASS_DECL(PSC_Event);
C_CLASS_DECL(PSC_ThreadJob);

typedef void (*PSC_ThreadProc)(void *arg);

DECLEXPORT PSC_ThreadJob *
PSC_ThreadJob_create(PSC_ThreadProc proc, void *arg, int timeoutTicks)
    ATTR_NONNULL((1)) ATTR_RETNONNULL;

DECLEXPORT PSC_Event *
PSC_ThreadJob_finished(PSC_ThreadJob *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT int
PSC_ThreadJob_hasCompleted(const PSC_ThreadJob *self)
    CMETHOD ATTR_PURE;

DECLEXPORT void
PSC_ThreadJob_destroy(PSC_ThreadJob *self);

DECLEXPORT int
PSC_ThreadJob_canceled(void);

DECLEXPORT void
PSC_ThreadOpts_init(int defThreads);

DECLEXPORT void
PSC_ThreadOpts_fixedThreads(int n);

DECLEXPORT void
PSC_ThreadOpts_threadsPerCpu(int n);

DECLEXPORT void
PSC_ThreadOpts_maxThreads(int n);

DECLEXPORT void
PSC_ThreadOpts_fixedQueue(int n);

DECLEXPORT void
PSC_ThreadOpts_queuePerThread(int n);

DECLEXPORT void
PSC_ThreadOpts_maxQueue(int n);

DECLEXPORT void
PSC_ThreadOpts_minQueue(int n);

DECLEXPORT int
PSC_ThreadPool_init(void);

DECLEXPORT int
PSC_ThreadPool_active(void);

DECLEXPORT int
PSC_ThreadPool_enqueue(PSC_ThreadJob *job)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_ThreadPool_cancel(PSC_ThreadJob *job)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_ThreadPool_done(void);

#endif
