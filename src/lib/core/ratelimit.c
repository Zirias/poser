#define _POSIX_C_SOURCE 200809L

#include <poser/core/ratelimit.h>

#include <poser/core/dictionary.h>
#include <poser/core/util.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#undef RLIM_NO_ATOMICS
#if defined(NO_ATOMICS) || defined(__STDC_NO_ATOMICS__)
#  define RLIM_NO_ATOMICS
#  include <pthread.h>
#else
#  include <stdatomic.h>
#endif

#define MAXCOUNTS 512

typedef struct Entry
{
#ifndef RLIM_NO_ATOMICS
    atomic_flag lock;
#endif
    uint16_t last;
    uint16_t total;
    uint16_t countpos;
    uint16_t counts[MAXCOUNTS];
} Entry;

#ifdef RLIM_NO_ATOMICS
typedef struct EntryList
{
    pthread_mutex_t lock;
    size_t nlimits;
    Entry entries[];
} EntryList;
#endif

typedef struct Limit
{
    uint16_t seconds;
    uint16_t limit;
    uint16_t res;
    uint16_t ncounts;
} Limit;

struct PSC_RateLimit
{
    size_t nlimits;
    PSC_Dictionary *entries;
    uint16_t cleancount;
    Limit limits[];
};

struct PSC_RateLimitOpts
{
    size_t limits_count;
    size_t limits_capa;
    Limit *limits;
};

#ifdef RLIM_NO_ATOMICS
#  define entries(e) ((e)->entries)
static void freeentries(void *obj)
{
    if (!obj) return;
    EntryList *el = obj;
    pthread_mutex_destroy(&el->lock);
    free(el);
}
#else
#  define entries(e) (e)
#  define freeentries free
#endif

PSC_RateLimit *PSC_RateLimit_create(const PSC_RateLimitOpts *opts)
{
    PSC_RateLimit *self = PSC_malloc(sizeof *self
	    + opts->limits_count * sizeof *self->limits);
    self->nlimits = opts->limits_count;
    self->entries = PSC_Dictionary_create(freeentries, 1);
    memcpy(self->limits, opts->limits, self->nlimits * sizeof *self->limits);
    self->cleancount = 2000;
    return self;
}

struct expiredarg
{
    Limit *limits;
    size_t nlimits;
    uint16_t now[];
};

static int expired(const void *key, size_t keysz, void *obj, const void *arg)
{
    (void)key;
    (void)keysz;

#ifdef RLIM_NO_ATOMICS
    EntryList *e = obj;
#else
    Entry *e = obj;
#endif
    const struct expiredarg *ea = arg;

    int isexpired = 1;
    for (size_t i = 0; i < ea->nlimits; ++i)
    {
	if (ea->now[i] - entries(e)[i].last < ea->limits[i].ncounts)
	{
	    isexpired = 0;
	    break;
	}
    }
    return isexpired;
}

static int checkLimit(Limit *self, Entry *e, struct timespec *ts)
{
    uint16_t now = ts->tv_sec / self->res;
#ifndef RLIM_NO_ATOMICS
    while (atomic_flag_test_and_set_explicit(&e->lock, memory_order_acq_rel)) ;
#endif
    if (e->total)
    {
	if (now - e->last >= self->ncounts)
	{
	    memset(e, 0, sizeof *e);
	    e->last = now;
	}
	else for (; e->last != now; ++e->last)
	{
	    if (++e->countpos == self->ncounts) e->countpos = 0;
	    e->total -= e->counts[e->countpos];
	    e->counts[e->countpos] = 0;
	}
    }
    else e->last = now;
    int ok = 0;
    if (e->total < self->limit)
    {
	++e->counts[e->countpos];
	++e->total;
	ok = 1;
    }
#ifndef RLIM_NO_ATOMICS
    atomic_flag_clear_explicit(&e->lock, memory_order_release);
#endif
    return ok;
}

int PSC_RateLimit_check(PSC_RateLimit *self, const void *key, size_t keysz)
{
    int ok = 1;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    if (!--self->cleancount)
    {
	struct expiredarg *ea = PSC_malloc(sizeof *ea
		+ self->nlimits * sizeof *ea->now);
	ea->limits = self->limits;
	ea->nlimits = self->nlimits;
	for (size_t i = 0; i < self->nlimits; ++i)
	{
	    ea->now[i] = ts.tv_sec / self->limits[i].res;
	}
	PSC_Dictionary_removeAll(self->entries, expired, ea);
	free(ea);
	self->cleancount = 2000;
    }
#ifdef RLIM_NO_ATOMICS
    EntryList *e = PSC_Dictionary_get(self->entries, key, keysz);
#else
    Entry *e = PSC_Dictionary_get(self->entries, key, keysz);
#endif
    while (!e)
    {
#ifdef RLIM_NO_ATOMICS
	EntryList *ne = PSC_malloc(sizeof *ne +
		self->nlimits * sizeof *ne->entries);
	pthread_mutex_init(&ne->lock, 0);
	ne->nlimits = self->nlimits;
#else
	Entry *ne = PSC_malloc(self->nlimits * sizeof *ne);
#endif
	memset(entries(ne), 0, self->nlimits * sizeof *entries(ne));
	for (size_t i = 0; i < self->nlimits; ++i)
	{
	    entries(ne)[i].last = ts.tv_sec / self->limits[i].res;
	}
	PSC_Dictionary_set(self->entries, key, keysz, ne, 0);
	e = PSC_Dictionary_get(self->entries, key, keysz);
    }
#ifdef RLIM_NO_ATOMICS
    pthread_mutex_lock(&e->lock);
#endif
    for (size_t i = 0; i < self->nlimits; ++i)
    {
	if (!checkLimit(self->limits + i, entries(e) + i, &ts)) ok = 0;
    }
#ifdef RLIM_NO_ATOMICS
    pthread_mutex_unlock(&e->lock);
#endif
    return ok;
}

void PSC_RateLimit_destroy(PSC_RateLimit *self)
{
    if (!self) return;
    PSC_Dictionary_destroy(self->entries);
    free(self);
}

PSC_RateLimitOpts *PSC_RateLimitOpts_create(void)
{
    PSC_RateLimitOpts *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    return self;
}

int PSC_RateLimitOpts_addLimit(PSC_RateLimitOpts *self, int seconds, int limit)
{
    if (seconds < 1 || seconds > 0xffff ||
	    limit < 1 || limit > 0xffff) return -1;
    if (self->limits_count == self->limits_capa)
    {
	self->limits_capa += 8;
	self->limits = PSC_realloc(self->limits,
		self->limits_capa * sizeof *self->limits);
    }
    Limit *l = self->limits + self->limits_count++;
    l->seconds = seconds;
    l->limit = limit;
    l->res = (seconds + MAXCOUNTS - 1) / MAXCOUNTS;
    l->ncounts = (seconds + l->res - 1) / l->res;
    return 0;
}

int PSC_RateLimitOpts_equals(const PSC_RateLimitOpts *self,
	const PSC_RateLimitOpts *other)
{
    if (self->limits_count != other->limits_count) return 0;
    for (size_t i = 0; i < self->limits_count; ++i)
    {
	if (self->limits[i].seconds != other->limits[i].seconds) return 0;
	if (self->limits[i].limit != other->limits[i].limit) return 0;
    }
    return 1;
}

void PSC_RateLimitOpts_destroy(PSC_RateLimitOpts *self)
{
    if (!self) return;
    free(self->limits);
    free(self);
}

