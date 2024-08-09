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

SOEXPORT PSC_Timer *PSC_Timer_create(void)
{
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
    self->ms = ms;
}

SOEXPORT void PSC_Timer_start(PSC_Timer *self, int periodic)
{
    if (!self->job)
    {
	self->periodic = periodic;
    }
}

SOEXPORT void PSC_Timer_stop(PSC_Timer *self)
{
    if (self->job)
    {
    }
}

SOLOCAL void PSC_Timer_underrun(void)
{
    if (!jobs) return;
}

SOEXPORT void PSC_Timer_destroy(PSC_Timer *self)
{
    if (!self) return;
    if (self->job) PSC_Timer_stop(self);
    PSC_Event_destroy(self->expired);
    free(self);
}

