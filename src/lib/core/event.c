#include "event.h"

#include "assert.h"
#include "objectpool.h"

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
    PoolObj base;
    EvHandlerEntry *next;
    EvHandler handler;
};

static THREADLOCAL ObjectPool *pool;
static THREADLOCAL size_t refcnt;

static void poolInit(void)
{
    if (!pool) pool = ObjectPool_create(sizeof (EvHandlerEntry), 4096);
    ++refcnt;
}

static void poolDone(void)
{
    if (!--refcnt)
    {
	ObjectPool_destroy(pool, 0);
	pool = 0;
    }
}

SOLOCAL void PSC_Event_initStatic(PSC_Event *self, void *sender)
{
    poolInit();
    memset(self, 0, sizeof *self);
    self->pool = pool;
    self->sender = sender;
}

SOEXPORT PSC_Event *PSC_Event_create(void *sender)
{
    assert(PSC_Service_threadNo() > -2);
    PSC_Event *self = PSC_malloc(sizeof *self);
    PSC_Event_initStatic(self, sender);
    return self;
}

static EvHandlerEntry *findEntry(PSC_Event *self, EvHandler *handler,
	EvHandlerEntry **parent)
{
    EvHandlerEntry *e = 0;
    EvHandlerEntry *p = 0;
    for (e = self->first; e; p = e, e = e->next)
    {
	if (!memcmp(&e->handler, handler, sizeof *handler)) break;
    }
    if (!e) p = 0;
    if (parent) *parent = p;
    return e;
}

SOEXPORT void PSC_Event_register(PSC_Event *self, void *receiver,
	PSC_EventHandler handler, int id)
{
    assert(self->pool == pool);
    EvHandler hdl;
    EVHDL_INIT(&hdl, handler, receiver, id);
    if (findEntry(self, &hdl, 0)) return;
    EvHandlerEntry *entry = ObjectPool_alloc(pool);
    entry->next = 0;
    entry->handler = hdl;
    if (self->last) self->last->next = entry;
    else self->first = entry;
    self->last = entry;
}

SOEXPORT void PSC_Event_unregister(
	PSC_Event *self, void *receiver, PSC_EventHandler handler, int id)
{
    assert(self->pool == pool);
    if (!self->first) return;
    EvHandler hdl;
    EVHDL_INIT(&hdl, handler, receiver, id);
    EvHandlerEntry *parent = 0;
    EvHandlerEntry *entry = findEntry(self, &hdl, &parent);
    if (!entry) return;
    if (entry == self->handling) self->handling = entry->next;
    if (parent) parent->next = entry->next;
    else self->first = entry->next;
    if (!entry->next) self->last = parent;
    PoolObj_free(entry);
}

SOEXPORT void PSC_Event_raise(PSC_Event *self, int id, void *args)
{
    assert(self->pool == pool);
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
    assert(self->pool == pool);
    for (EvHandlerEntry *entry = self->first; entry;)
    {
	EvHandlerEntry *next = entry->next;
	PoolObj_free(entry);
	entry = next;
    }
    memset(self, 0, sizeof *self);
    poolDone();
}

SOEXPORT void PSC_Event_destroy(PSC_Event *self)
{
    if (!self) return;
    PSC_Event_destroyStatic(self);
    free(self);
}

