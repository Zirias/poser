#define _POSIX_C_SOURCE 200112L

#include "event.h"
#include "timer.h"
#include "util.h"

#include <stdlib.h>
#include <sys/time.h>

C_CLASS_DECL(PSC_TimerJob);

struct PSC_TimerJob
{
    PSC_TimerJob *next;
    PSC_Timer *timer;
    struct timeval remaining;
};

struct PSC_Timer
{
    PSC_Event *expired;
    PSC_TimerJob *job;
    unsigned ms;
    int periodic;
};

static PSC_TimerJob *jobs;
static int initialized;
static const struct itimerval itvzero;

SOEXPORT PSC_Timer *PSC_Timer_create(void)
{
    if (!initialized)
    {
	setitimer(ITIMER_REAL, &itvzero, 0);
	initialized = 1;
    }
    PSC_Timer *self = PSC_malloc(sizeof *self);
    self->expired = PSC_Event_create(self);
    self->job = 0;
    self->ms = 1000;
    self->periodic = 0;
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
	PSC_Timer_stop(self);
	self->ms = ms;
	PSC_Timer_start(self, self->periodic);
    }
    else self->ms = ms;
}

#define tvlessthan(lhs,rhs) ((lhs).tv_sec < (rhs).tv_sec || \
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

SOEXPORT void PSC_Timer_destroy(PSC_Timer *self)
{
    if (!self) return;
    if (self->job) PSC_Timer_stop(self);
    PSC_Event_destroy(self->expired);
    free(self);
}

