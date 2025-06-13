#include "sharedobj.h"

#ifndef NO_SHAREDOBJ
#  include <poser/core/util.h>
#  include <stdlib.h>

static atomic_uint intEpoch;
static atomic_uint intDestroy;
static atomic_uint epoch;
static atomic_uint nthr;
static atomic_uint *res;

static THREADLOCAL unsigned tid;
static THREADLOCAL unsigned ncreated;
static THREADLOCAL unsigned nretired;
static THREADLOCAL SharedObj *retired;

SOLOCAL void *SharedObj_create(size_t sz, void (*destroy)(void *))
{
    unsigned created;
    if (++ncreated == atomic_load_explicit(&intEpoch, memory_order_relaxed))
    {
	ncreated = 0;
	created = atomic_fetch_add_explicit(&epoch, 1, memory_order_acq_rel);
    }
    else created = atomic_load_explicit(&epoch, memory_order_acquire);
    SharedObj *self = PSC_malloc(sz);
    self->destroy = destroy;
    atomic_store_explicit(&self->next, 0, memory_order_release);
    atomic_store_explicit(&self->created, created, memory_order_release);
    atomic_store_explicit(&self->retired, created - 1U, memory_order_release);
    return self;
}

SOLOCAL void SharedObj_retire(void *self)
{
    SharedObj *so = self;
    atomic_store_explicit(&so->retired, atomic_load_explicit(&epoch,
		memory_order_consume), memory_order_relaxed);
    atomic_store_explicit(&so->next, retired, memory_order_relaxed);
    retired = so;
    if (++nretired == atomic_load_explicit(&intDestroy, memory_order_relaxed))
    {
	nretired = 0;
	unsigned nt = atomic_load_explicit(&nthr, memory_order_relaxed);
	for (SharedObj *i = retired, *p = 0, *n = 0; i; i = n)
	{
	    n = atomic_load_explicit(&i->next, memory_order_relaxed);
	    unsigned creat = atomic_load_explicit(&i->created,
		    memory_order_relaxed);
	    unsigned retd = atomic_load_explicit(&i->retired,
		    memory_order_relaxed);
	    int conflicts = 0;
	    for (unsigned t = 0; t < nt; ++t)
	    {
		unsigned tres = atomic_load_explicit(res + t,
			memory_order_consume);
		if (creat > retd ?
			(tres >= creat || tres <= retd) :
			(tres >= creat && tres <= retd))
		{
		    conflicts = 1;
		    break;
		}
	    }
	    if (conflicts)
	    {
		p = i;
	    }
	    else
	    {
		if (p) atomic_store_explicit(&p->next, n,
			memory_order_relaxed);
		else retired = n;
		if (i->destroy) i->destroy(i);
		else free(i);
	    }
	}
    }
}

SOLOCAL void SOM_registerThread(void)
{
    tid = atomic_fetch_add_explicit(&nthr, 1, memory_order_acq_rel);
}

SOLOCAL void SOM_init(unsigned nthreads,
	unsigned epochInterval, unsigned destroyInterval)
{
    atomic_store_explicit(&intEpoch, epochInterval, memory_order_release);
    atomic_store_explicit(&intDestroy, destroyInterval, memory_order_release);
    res = PSC_malloc(nthreads * sizeof *res);
}

SOLOCAL void *SOM_reserve(void *_Atomic *ref)
{
    for (;;)
    {
	unsigned ep = atomic_load_explicit(&epoch, memory_order_consume);
	atomic_store_explicit(res + tid, ep, memory_order_release);
	void *p = atomic_load_explicit(ref, memory_order_acquire);
	if (ep == atomic_load_explicit(&epoch, memory_order_consume))
	{
	    return p;
	}
    }
}

SOLOCAL void SOM_release(void)
{
    unsigned ep = atomic_load_explicit(&epoch, memory_order_consume);
    atomic_store_explicit(res + tid, ep - 1U, memory_order_release);
}

#else
typedef int posercore___dummy;
#endif
