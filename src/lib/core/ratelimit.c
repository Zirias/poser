#define _POSIX_C_SOURCE 200809L

#include <poser/core/ratelimit.h>

#include <poser/core/dictionary.h>
#include <poser/core/util.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXCOUNTS 512

typedef struct Entry
{
    uint16_t last;
    uint16_t total;
    uint16_t countpos;
    uint16_t counts[];
} Entry;

typedef struct Limit
{
    PSC_Dictionary *entries;
    uint16_t seconds;
    uint16_t limit;
    uint16_t res;
    uint16_t ncounts;
    uint16_t cleancount;
} Limit;

struct PSC_RateLimit
{
    size_t nlimits;
    pthread_mutex_t lock;
    int locked;
    uint16_t cleanperiod;
    Limit limits[];
};

struct PSC_RateLimitOpts
{
    int locked;
    size_t limits_count;
    size_t limits_capa;
    Limit *limits;
};

PSC_RateLimit *PSC_RateLimit_create(const PSC_RateLimitOpts *opts)
{
    PSC_RateLimit *self = PSC_malloc(sizeof *self
	    + opts->limits_count * sizeof *self->limits);
    self->nlimits = opts->limits_count;
    if (opts->locked) pthread_mutex_init(&self->lock, 0);
    self->locked = opts->locked;
    memcpy(self->limits, opts->limits, self->nlimits * sizeof *self->limits);
    self->cleanperiod = self->nlimits > 20 ? 1000 : 50 * self->nlimits;
    for (size_t i = 0; i < self->nlimits; ++i)
    {
	self->limits[i].cleancount = (i+1) *
	    (self->cleanperiod / self->nlimits);
    }
    return self;
}

struct expiredarg
{
    const void *key;
    size_t keysz;
    uint16_t now;
    uint16_t ncounts;
};

static int expired(const void *key, size_t keysz, void *obj, const void *arg)
{
    const struct expiredarg *ea = arg;
    Entry *e = obj;

    if ((ea->keysz != keysz || memcmp(key, ea->key, keysz))
	    && ea->now - e->last >= ea->ncounts) return 1;
    return 0;
}

static int checkLimit(Limit *self, struct timespec *ts,
	const void *key, size_t keysz, uint16_t cleanperiod)
{
    uint16_t now = ts->tv_sec / self->res;
    if (!self->entries) self->entries = PSC_Dictionary_create(free);
    else
    {
	if (!--self->cleancount)
	{
	    struct expiredarg ea = {
		.key = key,
		.keysz = keysz,
		.now = now,
		.ncounts = self->ncounts
	    };
	    PSC_Dictionary_removeAll(self->entries, expired, &ea);
	    self->cleancount = cleanperiod;
	}
    }
    Entry *e = PSC_Dictionary_get(self->entries, key, keysz);
    if (!e)
    {
	e = PSC_malloc(sizeof *e + self->ncounts * sizeof *e->counts);
	memset(e, 0, sizeof *e + self->ncounts * sizeof *e->counts);
	e->last = now;
	PSC_Dictionary_set(self->entries, key, keysz, e, 0);
    }
    for (; e->last != now; ++e->last)
    {
	if (++e->countpos == self->ncounts) e->countpos = 0;
	e->total -= e->counts[e->countpos];
	e->counts[e->countpos] = 0;
    }
    if (e->total < self->limit)
    {
	++e->counts[e->countpos];
	++e->total;
	return 1;
    }
    return 0;
}

int PSC_RateLimit_check(PSC_RateLimit *self, const void *key, size_t keysz)
{
    int ok = 1;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    if (self->locked) pthread_mutex_lock(&self->lock);
    for (size_t i = 0; i < self->nlimits; ++i)
    {
	if (!checkLimit(self->limits + i, &ts, key, keysz,
		    self->cleanperiod)) ok = 0;
    }
    if (self->locked) pthread_mutex_unlock(&self->lock);
    return ok;
}

void PSC_RateLimit_destroy(PSC_RateLimit *self)
{
    if (!self) return;
    for (size_t i = 0; i < self->nlimits; ++i)
    {
	PSC_Dictionary_destroy(self->limits[i].entries);
    }
    if (self->locked) pthread_mutex_destroy(&self->lock);
    free(self);
}

PSC_RateLimitOpts *PSC_RateLimitOpts_create(int locked)
{
    PSC_RateLimitOpts *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->locked = locked;
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
    l->entries = 0;
    l->seconds = seconds;
    l->limit = limit;
    l->res = (seconds + MAXCOUNTS - 1) / MAXCOUNTS;
    l->ncounts = (seconds + l->res - 1) / l->res;
    return 0;
}

int PSC_RateLimitOpts_equals(const PSC_RateLimitOpts *self,
	const PSC_RateLimitOpts *other)
{
    if (self->locked != other->locked) return 0;
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

