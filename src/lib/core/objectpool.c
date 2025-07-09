#include "objectpool.h"

#undef POOL_MFLAGS
#if defined(HAVE_MANON) || defined(HAVE_MANONYMOUS)
#  define _DEFAULT_SOURCE
#  ifdef HAVE_MANON
#    define POOL_MFLAGS (MAP_ANON|MAP_PRIVATE)
#  else
#    define POOL_MFLAGS (MAP_ANONYMOUS|MAP_PRIVATE)
#  endif
#endif

#include <poser/core/util.h>
#include <stdlib.h>
#include <string.h>

#ifdef POOL_MFLAGS
#  include <poser/core/service.h>
#  include <sys/mman.h>
#  include <unistd.h>
static long pagesz;
#endif

C_CLASS_DECL(ObjPoolHdr);

struct ObjectPool
{
    size_t objsz;
    size_t objsperchunk;
    size_t nobj;
    size_t nfree;
    size_t chunksz;
    size_t firstfree;
    size_t lastused;
    ObjPoolHdr *first;
    ObjPoolHdr *last;
    ObjPoolHdr *keep;
    unsigned keepcnt;
};

struct ObjPoolHdr
{
    ObjPoolHdr *prev;
    ObjPoolHdr *next;
    size_t nfree;
};

#ifdef POOL_MFLAGS
SOLOCAL void ObjectPool_init(void)
{
    pagesz = sysconf(_SC_PAGESIZE);
}
#endif

SOLOCAL ObjectPool *ObjectPool_create(size_t objSz, size_t objsPerChunk)
{
    ObjectPool *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->objsz = objSz;
    self->objsperchunk = objsPerChunk;
    self->chunksz = objSz * objsPerChunk + sizeof (ObjPoolHdr);
#ifdef POOL_MFLAGS
    size_t partialpg = self->chunksz % pagesz;
    if (partialpg)
    {
	size_t extra = (pagesz - partialpg);
	self->chunksz += extra;
	self->objsperchunk += extra / objSz;
    }
#endif
    self->firstfree = POOLOBJ_USEDMASK;
    self->lastused = POOLOBJ_USEDMASK;
    return self;
}

void *ObjectPool_alloc(ObjectPool *self)
{
    if (self->keep) ++self->keepcnt;
    if (!(self->firstfree & POOLOBJ_USEDMASK))
    {
	size_t chunkno = self->firstfree / self->objsperchunk;
	ObjPoolHdr *hdr = self->first;
	for (size_t i = 0; i < chunkno; ++i) hdr = hdr->next;
	char *p = (char *)hdr + sizeof *hdr +
	    (self->firstfree % self->objsperchunk) * self->objsz;
	((PoolObj *)p)->id = self->firstfree | POOLOBJ_USEDMASK;
	((PoolObj *)p)->pool = self;
	if ((self->lastused & POOLOBJ_USEDMASK)
		|| self->firstfree > self->lastused)
	{
	    self->lastused = self->firstfree;
	}
	--hdr->nfree;
	if (--self->nfree)
	{
	    size_t nextfree;
	    char *f;
	    if (hdr->nfree)
	    {
		f = p + self->objsz;
		nextfree = self->firstfree + 1;
	    }
	    else
	    {
		while (!hdr->nfree)
		{
		    ++chunkno;
		    hdr = hdr->next;
		}
		f = (char *)hdr + sizeof *hdr;
		nextfree = chunkno * self->objsperchunk;
	    }
	    while (((PoolObj *)f)->id & POOLOBJ_USEDMASK)
	    {
		f += self->objsz;
		++nextfree;
	    }
	    self->firstfree = nextfree;
	}
	else self->firstfree = POOLOBJ_USEDMASK;
	return p;
    }

    ObjPoolHdr *hdr;
    if (self->keep)
    {
	hdr = self->keep;
	self->keep = 0;
    }
    else
    {
#ifdef POOL_MFLAGS
	hdr = mmap(0, self->chunksz, PROT_READ|PROT_WRITE, POOL_MFLAGS, -1, 0);
	if (hdr == MAP_FAILED) PSC_Service_panic("Out of memory!");
#else
	hdr = PSC_malloc(self->chunksz);
#endif
    }
    hdr->prev = self->last;
    hdr->next = 0;
    hdr->nfree = self->objsperchunk - 1;
    self->nfree += hdr->nfree;
    self->firstfree = self->nobj + 1;
    char *p = (char *)hdr + sizeof *hdr;
    ((PoolObj *)p)->id = self->nobj | POOLOBJ_USEDMASK;
    ((PoolObj *)p)->pool = self;
    self->nobj += self->objsperchunk;
    if (self->last) self->last->next = hdr;
    else self->first = hdr;
    self->last = hdr;
    return p;
}

SOLOCAL void ObjectPool_destroy(ObjectPool *self, void (*objdestroy)(void *))
{
    if (!self) return;

#ifdef POOL_MFLAGS
    if (self->keep) munmap(self->keep, self->chunksz);
#else
    free(self->keep);
#endif

    for (ObjPoolHdr *hdr = self->first, *next = 0; hdr; hdr = next)
    {
	next = hdr->next;
	if (objdestroy)
	{
	    size_t used = self->objsperchunk - hdr->nfree;
	    if (used)
	    {
		char *p = (char *)hdr + sizeof *hdr;
		while (used)
		{
		    while (!(((PoolObj *)p)->id & POOLOBJ_USEDMASK))
		    {
			p += self->objsz;
		    }
		    objdestroy(p);
		    --used;
		    p += self->objsz;
		}
	    }
	}
#ifdef POOL_MFLAGS
	munmap(hdr, self->chunksz);
#else
	free(hdr);
#endif
    }

    free(self);
}

SOLOCAL void PoolObj_free(void *obj)
{
    if (!obj) return;
    PoolObj *po = obj;
    ObjectPool *self = po->pool;

    if (self->keep && !--self->keepcnt)
    {
#ifdef POOL_MFLAGS
	munmap(self->keep, self->chunksz);
#else
	free(self->keep);
#endif
	self->keep = 0;
    }

    po->id &= ~POOLOBJ_USEDMASK;
    if ((self->firstfree & POOLOBJ_USEDMASK)
	    || po->id < self->firstfree) self->firstfree = po->id;
    ++self->nfree;

    size_t chunkno = po->id / self->objsperchunk;
    ObjPoolHdr *hdr = self->first;
    for (size_t i = 0; i < chunkno; ++i) hdr = hdr->next;
    ++hdr->nfree;

    if (po->id != self->lastused) return;

    size_t lastchunk = chunkno;
    while (hdr && hdr->nfree == self->objsperchunk)
    {
	--lastchunk;
	self->last = hdr->prev;
	self->nfree -= self->objsperchunk;
	self->nobj -= self->objsperchunk;
	if (self->keep)
	{
#ifdef POOL_MFLAGS
	    munmap(self->keep, self->chunksz);
#else
	    free(self->keep);
#endif
	}
	self->keep = hdr;
	self->keepcnt = 16;
#if defined(POOL_MFLAGS) && defined(HAVE_MADVISE) && defined(HAVE_MADVFREE)
	madvise(self->keep, self->chunksz, MADV_FREE);
#endif
	hdr = self->last;
	if (hdr) hdr->next = 0;
    }

    if (lastchunk & POOLOBJ_USEDMASK)
    {
	self->lastused = POOLOBJ_USEDMASK;
	self->firstfree = POOLOBJ_USEDMASK;
	return;
    }

    char *p = obj;
    if (lastchunk < chunkno)
    {
	self->lastused = (chunkno + 1) * self->objsperchunk - 1;
	p = (char *)hdr + sizeof hdr + self->lastused * self->objsz;
    }
    while (!(((PoolObj *)p)->id & POOLOBJ_USEDMASK))
    {
	p -= self->objsz;
	--self->lastused;
    }

#if defined(POOL_MFLAGS) && defined(HAVE_MADVISE) && defined(HAVE_MADVFREE)
    size_t usedbytes = (p - (char *)hdr) + self->objsz;
    size_t usedpg = usedbytes / pagesz + !!(usedbytes % pagesz) * pagesz;
    size_t freebytes = self->chunksz - (usedpg * pagesz);
    if (freebytes)
    {
	madvise((char *)hdr + usedpg * pagesz, freebytes, MADV_FREE);
    }
#endif
}

