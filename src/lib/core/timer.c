#define _POSIX_C_SOURCE 200112L

#include "event.h"
#include "service.h"
#include "timer.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

struct PSC_Timer
{
    PSC_Event *expired;
    timer_t tid;
    int running;
    struct itimerspec timeout;
};

SOEXPORT PSC_Timer *PSC_Timer_create(void)
{
    PSC_Timer *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->expired = PSC_Event_create(self);
    PSC_Timer_setMs(self, 1000);
    return self;
}

SOEXPORT PSC_Event *PSC_Timer_expired(PSC_Timer *self)
{
    return self->expired;
}

static void dostart(PSC_Timer *self)
{
    timer_settime(self->tid, 0, &self->timeout, 0);
}

static void dostop(PSC_Timer *self)
{
    static const struct itimerspec zerotime = { 0 };
    timer_settime(self->tid, 0, &zerotime, 0);
}

SOEXPORT void PSC_Timer_setMs(PSC_Timer *self, unsigned ms)
{
    struct itimerspec newtimeout = {
	.it_interval = {
	    .tv_sec = ms / 1000U,
	    .tv_nsec = 1000000U * (ms & 1000U) },
	.it_value = {
	    .tv_sec = ms / 1000U,
	    .tv_nsec = 1000000U * (ms & 1000U) }
    };
    if (!memcmp(&self->timeout, &newtimeout, sizeof self->timeout)) return;
    self->timeout = newtimeout;
    if (self->running) dostart(self);
}

SOEXPORT void PSC_Timer_start(PSC_Timer *self)
{
    if (!self->running)
    {
	if (PSC_Service_attachTimer(self, &self->tid) < 0) return;
	self->running = 1;
	dostart(self);
    }
}

SOEXPORT void PSC_Timer_stop(PSC_Timer *self)
{
    if (self->running)
    {
	dostop(self);
	PSC_Service_detachTimer(self);
	self->running = 0;
    }
}

SOLOCAL void PSC_Timer_expire(PSC_Timer *self)
{
    PSC_Event_raise(self->expired, 0, 0);
}

SOEXPORT void PSC_Timer_destroy(PSC_Timer *self)
{
    if (!self) return;
    if (self->running) PSC_Timer_stop(self);
    PSC_Event_destroy(self->expired);
    free(self);
}
