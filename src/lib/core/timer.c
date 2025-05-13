#define _POSIX_C_SOURCE 200112L

#include "event.h"
#include "timer.h"

#include <poser/core/util.h>

#include <stdlib.h>

#if defined(HAVE_EVPORTS) || defined(HAVE_KQUEUE)
#  undef HAVE_TIMERFD
#  ifdef HAVE_EVPORTS
#    undef HAVE_KQUEUE
#    include <port.h>
#    include <poser/core/log.h>
#    include <signal.h>
#    include <time.h>
#  endif
#  include "service.h"
#elif defined(HAVE_TIMERFD)
#  include <poser/core/event.h>
#  include <poser/core/log.h>
#  include <poser/core/service.h>
#  include <fcntl.h>
#  include <stdint.h>
#  include <sys/timerfd.h>
#  include <time.h>
#  include <unistd.h>
void expired(void *receiver, void *sender, void *args);
#else
#  include <sys/time.h>

C_CLASS_DECL(PSC_TimerJob);

struct PSC_TimerJob
{
    PSC_TimerJob *next;
    PSC_Timer *timer;
    struct timeval remaining;
};

static PSC_TimerJob *jobs;
static int initialized;
static const struct itimerval itvzero;
#endif

struct PSC_Timer
{
    PSC_Event *expired;
#if defined(HAVE_EVPORTS) || defined(HAVE_KQUEUE) || defined(HAVE_TIMERFD)
#  ifdef HAVE_EVPORTS
    timer_t timerid;
#  endif
#  ifdef HAVE_TIMERFD
    int tfd;
#  endif
    int job;
#else
    PSC_TimerJob *job;
#endif
    unsigned ms;
    int periodic;
};

SOEXPORT PSC_Timer *PSC_Timer_create(void)
{
#ifdef HAVE_TIMERFD
#  if defined(TFD_NONBLOCK) && defined(TFD_CLOEXEC)
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);
#  else
    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd >= 0)
    {
	fcntl(tfd, F_SETFD, FD_CLOEXEC);
	fcntl(tfd, F_SETFL, fcntl(tfd, F_GETFL) | O_NONBLOCK);
    }
#  endif
    if (tfd < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "timer: cannot create timerfd");
    }
#endif
#if !defined(HAVE_EVPORTS) && !defined(HAVE_KQUEUE) && !defined(HAVE_TIMERFD)
    if (!initialized)
    {
	setitimer(ITIMER_REAL, &itvzero, 0);
	initialized = 1;
    }
#endif
    PSC_Timer *self = PSC_malloc(sizeof *self);
    self->expired = PSC_Event_create(self);
#ifdef HAVE_TIMERFD
    self->tfd = tfd;
    if (tfd >= 0)
    {
	PSC_Event_register(PSC_Service_readyRead(), self, expired, tfd);
	PSC_Service_registerRead(tfd);
    }
#endif
    self->job = 0;
    self->ms = 1000;
    self->periodic = 0;
#ifdef HAVE_EVPORTS
    port_notify_t pnot = {
	.portnfy_port = PSC_Service_epfd(),
	.portnfy_user = self
    };
    struct sigevent sev = {
	.sigev_notify = SIGEV_PORT,
	.sigev_value = {
	    .sival_ptr = &pnot
	}
    };
    if (timer_create(CLOCK_HIGHRES, &sev, &self->timerid) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "timer: cannot create timer");
	self->job = -1;
    }
#endif
    return self;
}

SOEXPORT PSC_Event *PSC_Timer_expired(PSC_Timer *self)
{
    return self->expired;
}

SOEXPORT void PSC_Timer_setMs(PSC_Timer *self, unsigned ms)
{
    if (self->job)
    {
#if !defined(HAVE_EVPORTS) && !defined(HAVE_KQUEUE) && !defined(HAVE_TIMERFD)
	PSC_Timer_stop(self);
#endif
	self->ms = ms;
	PSC_Timer_start(self, self->periodic);
    }
    else self->ms = ms;
}

#ifdef HAVE_EVPORTS

SOLOCAL void PSC_Timer_doexpire(PSC_Timer *self)
{
    PSC_Event_raise(self->expired, 0, 0);
    if (self->periodic)
    {
	int overruns = timer_getoverrun(self->timerid);
	for (int i = 0; i < overruns; ++i)
	{
	    PSC_Event_raise(self->expired, 0, 0);
	}
    }
    else self->job = 0;
}

SOEXPORT void PSC_Timer_start(PSC_Timer *self, int periodic)
{
    if (self->job == 0 && self->ms)
    {
	struct itimerspec its = { {0, 0}, {0, 0} };
	its.it_value.tv_sec = self->ms / 1000U;
	its.it_value.tv_nsec = 1000000U * (self->ms % 1000U);
	if (periodic) its.it_interval = its.it_value;
	timer_settime(self->timerid, 0, &its, 0);
	self->job = 1;
    }
}

SOEXPORT void PSC_Timer_stop(PSC_Timer *self)
{
    if (self->job == 1)
    {
	struct itimerspec its = { {0, 0}, {0, 0} };
	timer_settime(self->timerid, 0, &its, 0);
	self->job = 0;
    }
}

#elif defined(HAVE_KQUEUE)

SOLOCAL void PSC_Timer_doexpire(PSC_Timer *self)
{
    PSC_Event_raise(self->expired, 0, 0);
    if (!self->periodic) self->job = 0;
}

SOEXPORT void PSC_Timer_start(PSC_Timer *self, int periodic)
{
    if (!self->job && self->ms)
    {
	self->periodic = periodic;
	PSC_Service_armTimer(self, self->ms, periodic);
	self->job = 1;
    }
}

SOEXPORT void PSC_Timer_stop(PSC_Timer *self)
{
    if (self->job)
    {
	PSC_Service_unarmTimer(self, self->ms, self->periodic);
	self->job = 0;
    }
}

#elif defined(HAVE_TIMERFD)

void expired(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Timer *self = receiver;
    uint64_t times;
    if (self->tfd < 0 || read(self->tfd, &times, sizeof times) != sizeof times)
    {
	return;
    }
    for (uint64_t i = 0; i < times; ++i)
    {
	PSC_Event_raise(self->expired, 0, 0);
    }
    if (!self->periodic) self->job = 0;
}

SOEXPORT void PSC_Timer_start(PSC_Timer *self, int periodic)
{
    if (self->tfd >= 0 && !self->job && self->ms)
    {
	struct itimerspec its = { {0, 0}, {0, 0} };
	its.it_value.tv_sec = self->ms / 1000U;
	its.it_value.tv_nsec = 1000000U * (self->ms % 1000U);
	if (periodic) its.it_interval = its.it_value;
	timerfd_settime(self->tfd, 0, &its, 0);
	self->job = 1;
    }
}

SOEXPORT void PSC_Timer_stop(PSC_Timer *self)
{
    if (self->tfd >= 0 && self->job)
    {
	struct itimerspec its = { {0, 0}, {0, 0} };
	timerfd_settime(self->tfd, 0, &its, 0);
	self->job = 0;
    }
}

#else
#  define tvlessthan(lhs,rhs) ((lhs).tv_sec < (rhs).tv_sec || \
	((lhs).tv_sec == (rhs).tv_sec && (lhs).tv_usec < (rhs).tv_usec))

static void tvsuborzero(struct timeval *tv, const struct timeval *sub)
{
    if (tvlessthan(*sub, *tv))
    {
	tv->tv_sec -= sub->tv_sec;
	tv->tv_usec -= sub->tv_usec;
	if (tv->tv_usec < 0)
	{
	    --tv->tv_sec;
	    tv->tv_usec += 1000000;
	}
    }
    else
    {
	tv->tv_sec = 0;
	tv->tv_usec = 0;
    }
}

static void adjust(const struct timeval *elapsed)
{
    if (!jobs || (!elapsed->tv_sec && !elapsed->tv_usec)) return;
    PSC_TimerJob *j = jobs;
    do
    {
	tvsuborzero(&j->remaining, elapsed);
	j = j->next;
    } while (j);
}

static void stopandadjust(void)
{
    if (!jobs) return;
    struct itimerval curr;
    if (setitimer(ITIMER_REAL, &itvzero, &curr) == 0)
    {
	struct timeval elapsed = jobs->remaining;
	tvsuborzero(&elapsed, &curr.it_value);
	adjust(&elapsed);
    }
}

static void start(void)
{
    if (!jobs) return;
    if (jobs->remaining.tv_sec || jobs->remaining.tv_usec)
    {
	struct itimerval itv = {
	    .it_interval = { .tv_sec = 0, .tv_usec = 0 },
	    .it_value = jobs->remaining
	};
	setitimer(ITIMER_REAL, &itv, 0);
    }
    else PSC_Timer_underrun();
}

static void enqueueandstart(PSC_Timer *self)
{
    self->job->remaining = (struct timeval){
	.tv_sec = self->ms / 1000U,
	.tv_usec = 1000U * (self->ms % 1000U)
    };
    PSC_TimerJob *parent = 0;
    PSC_TimerJob *next = jobs;
    while (next && tvlessthan(next->remaining, self->job->remaining))
    {
	parent = next;
	next = next->next;
    }
    if (parent)
    {
	self->job->next = parent->next;
	parent->next = self->job;
    }
    else
    {
	self->job->next = jobs;
	jobs = self->job;
    }
    start();
}

SOEXPORT void PSC_Timer_start(PSC_Timer *self, int periodic)
{
    if (self->job) PSC_Timer_stop(self);
    if (!self->job && self->ms)
    {
	stopandadjust();
	self->periodic = periodic;
	self->job = PSC_malloc(sizeof *self->job);
	self->job->timer = self;
	enqueueandstart(self);
    }
}

SOEXPORT void PSC_Timer_stop(PSC_Timer *self)
{
    if (self->job)
    {
	if (self->job == jobs)
	{
	    stopandadjust();
	    jobs = self->job->next;
	    free(self->job);
	    self->job = 0;
	    start();
	}
	else
	{
	    PSC_TimerJob *parent = jobs;
	    while (parent->next && parent->next != self->job)
	    {
		parent = parent->next;
	    }
	    parent->next = self->job->next;
	    free(self->job);
	    self->job = 0;
	}
    }
}

SOLOCAL void PSC_Timer_underrun(void)
{
    if (!jobs) return;
    PSC_Timer *self = jobs->timer;
    struct timeval elapsed = jobs->remaining;
    jobs = jobs->next;
    adjust(&elapsed);
    if (self->periodic)
    {
	enqueueandstart(self);
    }
    else
    {
	free(self->job);
	self->job = 0;
	start();
    }
    PSC_Event_raise(self->expired, 0, 0);
}
#endif

SOEXPORT void PSC_Timer_destroy(PSC_Timer *self)
{
    if (!self) return;
#ifdef HAVE_EVPORTS
    if (self->job >= 0) timer_delete(self->timerid);
#elif defined(HAVE_TIMERFD)
    if (self->tfd >= 0)
    {
	PSC_Service_unregisterRead(self->tfd);
	PSC_Event_unregister(PSC_Service_readyRead(), self,
		expired, self->tfd);
	close(self->tfd);
    }
#else
    if (self->job) PSC_Timer_stop(self);
#endif
    PSC_Event_destroy(self->expired);
    free(self);
}

