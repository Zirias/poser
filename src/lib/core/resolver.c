#define _DEFAULT_SOURCE

#include <poser/core/resolver.h>

#include "event.h"
#include "ipaddr.h"

#include <netdb.h>
#include <netinet/in.h>
#include <poser/core/list.h>
#include <poser/core/threadpool.h>
#include <poser/core/util.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

struct PSC_Resolver
{
    PSC_Event done;
    PSC_ThreadJob *job;
    PSC_List *entries;
    int handling;
};

struct PSC_ResolverEntry
{
    PSC_IpAddr *addr;
    char *name;
};

static void deleteEntry(void *obj)
{
    if (!obj) return;
    PSC_ResolverEntry *entry = obj;
    free(entry->name);
    PSC_IpAddr_destroy(entry->addr);
    free(entry);
}

SOEXPORT PSC_Resolver *PSC_Resolver_create(void)
{
    PSC_Resolver *self = PSC_malloc(sizeof *self);
    PSC_Event_initStatic(&self->done, self);
    self->job = 0;
    self->entries = PSC_List_create();
    self->handling = 0;
    return self;
}

SOEXPORT int PSC_Resolver_addAddr(PSC_Resolver *self, const PSC_IpAddr *addr)
{
    if (self->job) return -1;
    PSC_ResolverEntry *entry = PSC_malloc(sizeof *entry);
    entry->addr = PSC_IpAddr_ref(addr);
    entry->name = 0;
    PSC_List_append(self->entries, entry, deleteEntry);
    return 0;
}

static void resolveProc(void *arg)
{
    struct sockaddr_storage saddr;
    size_t saddrlen;
    char nbuf[NI_MAXHOST];
    char sbuf[NI_MAXSERV];

    PSC_Resolver *self = arg;

    PSC_ListIterator *i = PSC_List_iterator(self->entries);
    while (PSC_ListIterator_moveNext(i))
    {
	PSC_ResolverEntry *entry = PSC_ListIterator_current(i);
	if (entry->addr && !entry->name)
	{
	    if (PSC_IpAddr_sockAddr(entry->addr,
			(struct sockaddr *)&saddr) < 0) continue;
	    if (PSC_IpAddr_proto(entry->addr) == PSC_P_IPv4)
	    {
		saddrlen = sizeof (struct sockaddr_in);
	    }
	    else saddrlen = sizeof (struct sockaddr_in6);
	    if (getnameinfo((struct sockaddr *)&saddr, saddrlen, nbuf,
			sizeof nbuf, sbuf, sizeof sbuf,
			NI_NAMEREQD|NI_NUMERICSERV) != 0) continue;
	    if (PSC_ThreadJob_canceled()) break;
	    entry->name = PSC_copystr(nbuf);
	}
    }
    PSC_ListIterator_destroy(i);
}

static void resolveDone(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Resolver *self = receiver;
    if (self->done.pool)
    {
	if (PSC_ThreadJob_hasCompleted(self->job))
	{
	    self->handling = 1;
	    PSC_Event_raise(&self->done, 0, 0);
	    if (self->handling < 0)
	    {
		self->job = 0;
		self->handling = 0;
		PSC_Resolver_destroy(self);
		return;
	    }
	    self->handling = 0;
	}
	self->job = 0;
    }
    else
    {
	PSC_List_destroy(self->entries);
	free(self);
    }
}

SOEXPORT int PSC_Resolver_resolve(PSC_Resolver *self, int forceAsync)
{
    if (self->job || PSC_List_size(self->entries) == 0) return -1;
    if (PSC_ThreadPool_active())
    {
	self->job = PSC_ThreadJob_create(resolveProc, self, 0);
	PSC_Event_register(PSC_ThreadJob_finished(self->job), self,
		resolveDone, 0);
	if (PSC_ThreadPool_enqueue(self->job) < 0)
	{
	    PSC_ThreadJob_destroy(self->job);
	    self->job = 0;
	}
    }
    if (!self->job)
    {
	if (forceAsync) return -1;
	resolveProc(self);
	PSC_Event_raise(&self->done, 0, 0);
    }
    return 0;
}

SOEXPORT PSC_Event *PSC_Resolver_done(PSC_Resolver *self)
{
    return &self->done;
}

SOEXPORT const PSC_List *PSC_Resolver_entries(const PSC_Resolver *self)
{
    return self->entries;
}

SOEXPORT void PSC_Resolver_destroy(PSC_Resolver *self)
{
    if (!self) return;
    if (self->handling)
    {
	self->handling = -1;
	return;
    }
    PSC_Event_destroyStatic(&self->done);
    if (self->job)
    {
	PSC_ThreadPool_cancel(self->job);
	return;
    }
    PSC_List_destroy(self->entries);
    free(self);
}

SOEXPORT const PSC_IpAddr *PSC_ResolverEntry_addr(
	const PSC_ResolverEntry *self)
{
    return self->addr;
}

SOEXPORT const char *PSC_ResolverEntry_name(const PSC_ResolverEntry *self)
{
    return self->name;
}

