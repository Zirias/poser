#include "event.h"

#include <poser/core/dictionary.h>
#include <poser/core/util.h>

#include <stdlib.h>
#include <string.h>

typedef struct EvHandler
{
    PSC_EventHandler cb;
    void *receiver;
    int id;
} EvHandler;

/* Explicit initializer, use to avoid non-zero padding, so the struct
 * can be used as the key for the index dictionary */
#define EVHDL_INIT(x, c, r, i) do { \
    memset((x), 0, sizeof *(x)); \
    (x)->cb = (c); \
    (x)->receiver = (r); \
    (x)->id = (i); \
} while (0);

struct EvHandlerEntry
{
    EvHandlerEntry *next;
    EvHandlerEntry *prev;
    EvHandler handler;
};

SOEXPORT PSC_Event *PSC_Event_create(void *sender)
{
    PSC_Event *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->sender = sender;
    return self;
}

SOEXPORT void PSC_Event_register(PSC_Event *self, void *receiver,
	PSC_EventHandler handler, int id)
{
    EvHandler hdl;
    EVHDL_INIT(&hdl, handler, receiver, id);
    if (self->index && PSC_Dictionary_get(self->index, &hdl, sizeof hdl))
    {
	return;
    }
    EvHandlerEntry *entry = PSC_malloc(sizeof *entry);
    entry->prev = self->last;
    entry->next = 0;
    entry->handler = hdl;
    if (!self->index) self->index = PSC_Dictionary_create(PSC_DICT_NODELETE);
    PSC_Dictionary_set(self->index, &hdl, sizeof hdl, entry, 0);
    if (self->last) self->last->next = entry;
    else self->first = entry;
    self->last = entry;
}

SOEXPORT void PSC_Event_unregister(
	PSC_Event *self, void *receiver, PSC_EventHandler handler, int id)
{
    if (!self->first || !self->index) return;
    EvHandler hdl;
    EVHDL_INIT(&hdl, handler, receiver, id);
    EvHandlerEntry *entry = PSC_Dictionary_get(self->index, &hdl, sizeof hdl);
    if (!entry) return;
    PSC_Dictionary_set(self->index, &hdl, sizeof hdl, 0, 0);
    if (entry->prev) entry->prev->next = entry->next;
    else self->first = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
    else self->last = entry->prev;
    free(entry);
}

SOEXPORT void PSC_Event_raise(PSC_Event *self, int id, void *args)
{
    for (EvHandlerEntry *entry = self->first; entry;)
    {
	EvHandlerEntry *next = entry->next;
	if (entry->handler.id == id)
	{
	    if (!args && id) args = &id;
	    entry->handler.cb(entry->handler.receiver, self->sender, args);
	}
	entry = next;
    }
}

SOLOCAL void PSC_Event_destroyStatic(PSC_Event *self)
{
    PSC_Dictionary_destroy(self->index);
    for (EvHandlerEntry *entry = self->first; entry;)
    {
	EvHandlerEntry *next = entry->next;
	free(entry);
	entry = next;
    }
    memset(self, 0, sizeof *self);
}

SOEXPORT void PSC_Event_destroy(PSC_Event *self)
{
    if (!self) return;
    PSC_Dictionary_destroy(self->index);
    for (EvHandlerEntry *entry = self->first; entry;)
    {
	EvHandlerEntry *next = entry->next;
	free(entry);
	entry = next;
    }
    free(self);
}

