#include "event.h"

#include <poser/core/util.h>

#include <stdlib.h>
#include <string.h>

#define EVCHUNKSIZE 4

struct EvHandler
{
    void *receiver;
    PSC_EventHandler handler;
    int id;
};

SOEXPORT PSC_Event *PSC_Event_create(void *sender)
{
    PSC_Event *self = PSC_malloc(sizeof *self);
    self->sender = sender;
    self->handlers = 0;
    self->size = 0;
    self->capa = 0;
    self->dirty = 0;
    return self;
}

SOLOCAL PSC_Event *PSC_Event_createDummyFire(void *sender, void *arg)
{
    PSC_Event *self = PSC_malloc(sizeof *self);
    self->sender = sender;
    self->handlers = 0;
    self->arg = arg;
    self->dirty = -1;
    return self;
}

SOEXPORT void PSC_Event_register(PSC_Event *self, void *receiver,
	PSC_EventHandler handler, int id)
{
    if (self->dirty < 0)
    {
	handler(receiver, self->sender, self->arg);
	return;
    }
    if (self->dirty)
    {
	for (size_t pos = 0; pos < self->size; ++pos)
	{
	    if (!self->handlers[pos].handler)
	    {
		--self->size;
		if (pos < self->size)
		{
		    memmove(self->handlers + pos, self->handlers + pos + 1,
			    (self->size - pos) * sizeof *self->handlers);
		}
		--pos;
	    }
	}
	self->dirty = 0;
    }
    if (self->size == self->capa)
    {
        self->capa += EVCHUNKSIZE;
        self->handlers = PSC_realloc(self->handlers,
                self->capa * sizeof *self->handlers);
    }
    self->handlers[self->size].receiver = receiver;
    self->handlers[self->size].handler = handler;
    self->handlers[self->size].id = id;
    ++self->size;
}

SOEXPORT void PSC_Event_unregister(
	PSC_Event *self, void *receiver, PSC_EventHandler handler, int id)
{
    if (self->dirty < 0) return;
    size_t pos;
    for (pos = 0; pos < self->size; ++pos)
    {
        if (self->handlers[pos].receiver == receiver
                && self->handlers[pos].handler == handler
		&& self->handlers[pos].id == id)
        {
	    self->handlers[pos].handler = 0;
	    self->dirty = 1;
            break;
        }
    }
}

SOEXPORT void PSC_Event_raise(PSC_Event *self, int id, void *args)
{
    if (self->dirty < 0) return;
    for (size_t i = 0; i < self->size; ++i)
    {
	if (self->handlers[i].id == id && self->handlers[i].handler)
	{
	    if (!args && id) args = &id;
	    self->handlers[i].handler(self->handlers[i].receiver,
		    self->sender, args);
	}
    }
}

SOEXPORT void PSC_Event_destroy(PSC_Event *self)
{
    if (!self) return;
    free(self->handlers);
    free(self);
}

