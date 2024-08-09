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
    unsigned ms;
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

static void adjust(unsigned elapsed)
{
    if (!jobs || !elapsed) return;
    PSC_TimerJob *j = jobs;
    do
    {
	j->ms = j->ms > elapsed ? j->ms - elapsed : 0;
	j = j->next;
    } while (j);
}

static void stopandadjust(void)
{
    if (!jobs) return;
    struct itimerval curr;
    if (setitimer(ITIMER_REAL, &itvzero, &curr) == 0)
    {
	unsigned togo = (curr.it_value.tv_usec + 999U) / 1000U
	    + curr.it_value.tv_sec * 1000U;
	unsigned elapsed = togo < jobs->ms ? jobs->ms - togo : 0;
	adjust(elapsed);
    }
}

static void start(void)
{
    if (!jobs) return;
    if (jobs->ms)
    {
	struct itimerval itv = {
	    .it_interval = { .tv_sec = 0, .tv_usec = 0 },
	    .it_value = {
		.tv_sec = jobs->ms / 1000U,
		.tv_usec = 1000U * (jobs->ms % 1000U)
	    }
	};
	setitimer(ITIMER_REAL, &itv, 0);
    }
    else PSC_Timer_underrun();
}

static void enqueueandstart(PSC_Timer *self)
{
    self->job->ms = self->ms;
    PSC_TimerJob *parent = 0;
    PSC_TimerJob *next = jobs;
    while (next && next->ms < self->ms)
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
    unsigned elapsed = jobs->ms;
    jobs = jobs->next;
    adjust(elapsed);
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

