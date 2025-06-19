#include "event.h"

#include "assert.h"

#include <poser/core/util.h>
#include <pthread.h>

#include <stdlib.h>
#include <string.h>

#ifndef NDEBUG
#include <poser/core/service.h>
#endif

typedef struct EvHandler
{
    PSC_EventHandler cb;
    void *receiver;
    int id;
} EvHandler;

/* Explicit initializer, use to avoid non-zero padding, so the struct
 * can be compared byte-wise */
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

struct EvHandlerPool
{
    EvHandlerEntry *first;
    EvHandlerEntry *last;
    size_t refcnt;
#ifndef NDEBUG
    void *thr;
#endif
};

static THREADLOCAL EvHandlerPool *pool;

static EvHandlerPool *EvHandlerPool_init(void)
{
    if (!pool)
    {
	pool = PSC_malloc(sizeof *pool);
	memset(pool, 0, sizeof *pool);
#ifndef NDEBUG
	pool->thr = (void *)pthread_self();
#endif
    }
    ++pool->refcnt;
    return pool;
}

static void EvHandlerPool_done(EvHandlerPool *self)
{
    if (!--self->refcnt)
    {
	pool = 0;
	for (EvHandlerEntry *e = self->first, *n = 0; e; e = n)
	{
	    n = e->next;
	    free(e);
	}
	free(self);
    }
}

static EvHandlerEntry *EvHandlerPool_get(EvHandlerPool *self)
{
    assert(self->thr == (void *)pthread_self());
    EvHandlerEntry *e = self->first;
    if (e)
    {
	if (!(self->first = e->next)) self->last = 0;
    }
    else e = PSC_malloc(sizeof *e);
    return e;
}

static void EvHandlerPool_return(EvHandlerPool *self, EvHandlerEntry *e)
{
    assert(self->thr == (void *)pthread_self());
    e->next = 0;
    if (self->last) self->last->next = e;
    else self->first = e;
    self->last = e;
}

SOLOCAL void PSC_Event_initStatic(PSC_Event *self, void *sender)
{
    memset(self, 0, sizeof *self);
    self->sender = sender;
    self->pool = EvHandlerPool_init();
}

SOEXPORT PSC_Event *PSC_Event_create(void *sender)
{
    assert(PSC_Service_threadNo() > -2);
    PSC_Event *self = PSC_malloc(sizeof *self);
    PSC_Event_initStatic(self, sender);
    return self;
}

static EvHandlerEntry *findEntry(PSC_Event *self, EvHandler *handler)
{
    for (EvHandlerEntry *e = self->first; e; e = e->next)
    {
	if (!memcmp(&e->handler, handler, sizeof *handler)) return e;
    }
    return 0;
}

SOEXPORT void PSC_Event_register(PSC_Event *self, void *receiver,
	PSC_EventHandler handler, int id)
{
    EvHandler hdl;
    EVHDL_INIT(&hdl, handler, receiver, id);
    if (findEntry(self, &hdl)) return;
    EvHandlerEntry *entry = EvHandlerPool_get(self->pool);
    entry->prev = self->last;
    entry->next = 0;
    entry->handler = hdl;
    if (self->last) self->last->next = entry;
    else self->first = entry;
    self->last = entry;
}

SOEXPORT void PSC_Event_unregister(
	PSC_Event *self, void *receiver, PSC_EventHandler handler, int id)
{
    if (!self->first) return;
    EvHandler hdl;
    EVHDL_INIT(&hdl, handler, receiver, id);
    EvHandlerEntry *entry = findEntry(self, &hdl);
    if (!entry) return;
    if (entry == self->handling) self->handling = entry->next;
    if (entry->prev) entry->prev->next = entry->next;
    else self->first = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
    else self->last = entry->prev;
    EvHandlerPool_return(self->pool, entry);
}

SOEXPORT void PSC_Event_raise(PSC_Event *self, int id, void *args)
{
    assert(pool->thr == (void *)pthread_self());
    for (EvHandlerEntry *entry = self->first; entry;)
    {
	self->handling = entry->next;
	if (entry->handler.id == id)
	{
	    if (!args && id) args = &id;
	    entry->handler.cb(entry->handler.receiver, self->sender, args);
	}
	entry = self->handling;
    }
}

SOLOCAL void PSC_Event_destroyStatic(PSC_Event *self)
{
    if (!self->pool) return;
    for (EvHandlerEntry *entry = self->first; entry;)
    {
	EvHandlerEntry *next = entry->next;
	EvHandlerPool_return(self->pool, entry);
	entry = next;
    }
    EvHandlerPool_done(self->pool);
    memset(self, 0, sizeof *self);
}

SOEXPORT void PSC_Event_destroy(PSC_Event *self)
{
    if (!self) return;
    for (EvHandlerEntry *entry = self->first; entry;)
    {
	EvHandlerEntry *next = entry->next;
	EvHandlerPool_return(self->pool, entry);
	entry = next;
    }
    EvHandlerPool_done(self->pool);
    free(self);
}

