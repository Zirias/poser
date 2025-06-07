#include <poser/core/dictionary.h>

#include <poser/core/hash.h>
#include <poser/core/util.h>
#include <stdlib.h>
#include <string.h>

#undef DICT_NO_ATOMICS
#if defined(NO_ATOMICS) || defined(__STDC_NO_ATOMICS__)
#  define DICT_NO_ATOMICS
#else
#  include <stdatomic.h>
#  if ATOMIC_POINTER_LOCK_FREE != 2
#    define DICT_NO_ATOMICS
#  endif
#endif
#ifdef DICT_NO_ATOMICS
#  include <pthread.h>
#endif

#define HT8_SIZE 256
#define HT4_SIZE 16
#define MAXNEST 14

typedef enum BucketType
{
    BT_EMPTY,
    BT_ITEM,
    BT_HT8,
    BT_HT4
} BucketType;

typedef struct Entry
{
    size_t keysz;
    void *key;
    void *obj;
} Entry;

typedef struct EntryD
{
    Entry base;
    void (*deleter)(void *);
} EntryD;

typedef struct EntryL
{
    Entry base;
    Entry *next;
} EntryL;

typedef struct EntryDL
{
    EntryL base;
    void (*deleter)(void *);
} EntryDL;

typedef struct HashBucket
{
    void *content;
    BucketType type;
} HashBucket;

typedef struct HashTable4
{
    HashBucket buckets[HT4_SIZE];
} HashTable4;

typedef struct HashTable8
{
    HashBucket buckets[HT8_SIZE];
} HashTable8;

typedef struct DictLockData
{
#ifdef DICT_NO_ATOMICS
    pthread_mutex_t lock;
    pthread_mutex_t bucklock[HT8_SIZE];
#else
    atomic_int reserved[HT8_SIZE];
#endif
} DictLockData;

struct PSC_Dictionary
{
    void (*deleter)(void *);
    PSC_Hash *hash;
#ifdef DICT_NO_ATOMICS
    size_t count;
#else
    atomic_size_t count;
#endif
    int shared;
    HashBucket buckets[HT8_SIZE];
    DictLockData l[];
};

SOEXPORT void (*PSC_DICT_NODELETE)(void *) =
    (void (*)(void *))PSC_Dictionary_destroy;

SOEXPORT PSC_Dictionary *PSC_Dictionary_create(void (*deleter)(void *),
	int shared)
{
    PSC_Hash *hash = PSC_Hash_create(0, 1);
    if (!hash) return 0;

    PSC_Dictionary *self = PSC_malloc(sizeof *self
	    + (!!shared) * sizeof *self->l);
    memset(self, 0, sizeof *self + (!!shared) * sizeof *self->l);
    self->deleter = deleter;
    self->hash = hash;
    self->shared = shared;
#ifdef DICT_NO_ATOMICS
    if (shared)
    {
	pthread_mutex_init(&self->l->lock, 0);
	for (size_t i = 0; i < HT8_SIZE; ++i)
	{
	    pthread_mutex_init(self->l->bucklock + i, 0);
	}
    }
#endif
    return self;
}

static Entry *createEntry(PSC_Dictionary *self, int depth, const void *key,
	size_t keysz, void *obj, void (*deleter)(void *))
{
    Entry *entry = 0;
    if (self->deleter)
    {
	if (depth >= MAXNEST)
	{
	    EntryL *el = PSC_malloc(sizeof *el);
	    el->next = 0;
	    entry = (Entry *)el;
	}
	else entry = PSC_malloc(sizeof *entry);
    }
    else
    {
	if (depth >= MAXNEST)
	{
	    EntryDL *edl = PSC_malloc(sizeof *edl);
	    edl->base.next = 0;
	    edl->deleter = deleter;
	    entry = (Entry *)edl;
	}
	else
	{
	    EntryD *ed = PSC_malloc(sizeof *ed);
	    ed->deleter = deleter;
	    entry = (Entry *)ed;
	}
    }
    entry->keysz = keysz;
    entry->key = PSC_malloc(keysz);
    memcpy(entry->key, key, keysz);
    entry->obj = obj;
#ifdef DICT_NO_ATOMICS
    if (self->shared) pthread_mutex_lock(&self->l->lock);
    ++self->count;
    if (self->shared) pthread_mutex_unlock(&self->l->lock);
#else
    atomic_fetch_add_explicit(&self->count, 1, memory_order_release);
#endif
    return entry;
}

static void set(PSC_Dictionary *self, HashBucket *bucket, int depth,
	uint64_t hash, const void *key, size_t keysz,
	void *obj, void (*deleter)(void *))
{
    Entry *e;
    HashTable8 *ht8;
    HashTable4 *ht4;

    switch (bucket->type)
    {
	case BT_EMPTY:
	    if (!obj) break;
	    bucket->type = BT_ITEM;
	    bucket->content = createEntry(self, depth, key, keysz,
		    obj, deleter);
	    break;

	case BT_ITEM:
	    e = bucket->content;
	    if (depth >= MAXNEST)
	    {
		EntryL *parent = 0;
		EntryL *el = bucket->content;
		while (el)
		{
		    if (el->base.keysz == keysz && !memcmp(el->base.key,
				key, keysz))
		    {
			EntryDL *edl = 0;
			if (self->deleter)
			{
			    if (self->deleter != PSC_DICT_NODELETE)
			    {
				self->deleter(el->base.obj);
			    }
			}
			else
			{
			    edl = (EntryDL *)el;
			    if (edl->deleter) edl->deleter(el->base.obj);
			}
			if (obj)
			{
			    el->base.obj = obj;
			    if (edl) edl->deleter = deleter;
			}
			else
			{
			    if (parent)
			    {
				parent->next = el->next;
			    }
			    else
			    {
				if (el->next)
				{
				    bucket->content = el->next;
				}
				else bucket->type = BT_EMPTY;
			    }
			    free(el->base.key);
			    free(el);
#ifdef DICT_NO_ATOMICS
			    if (self->shared) pthread_mutex_lock(
				    &self->l->lock);
			    --self->count;
			    if (self->shared) pthread_mutex_unlock(
				    &self->l->lock);
#else
			    atomic_fetch_sub_explicit(&self->count, 1,
				    memory_order_release);
#endif
			}
			break;
		    }
		    parent = el;
		    el = (EntryL *)el->next;
		}
		if (!el)
		{
		    el = (EntryL *)createEntry(self, depth, key, keysz,
			    obj, deleter);
		    parent->next = (Entry *)el;
		}
	    }
	    else
	    {
		if (e->keysz == keysz && !memcmp(e->key, key, keysz))
		{
		    EntryD *ed = 0;
		    if (self->deleter)
		    {
			if (self->deleter != PSC_DICT_NODELETE)
			{
			    self->deleter(e->obj);
			}
		    }
		    else
		    {
			ed = (EntryD *)e;
			if (ed->deleter) ed->deleter(e->obj);
		    }
		    if (obj)
		    {
			e->obj = obj;
			if (ed) ed->deleter = deleter;
		    }
		    else
		    {
			bucket->type = BT_EMPTY;
			free(e->key);
			free(e);
#ifdef DICT_NO_ATOMICS
			if (self->shared) pthread_mutex_lock(
				&self->l->lock);
			--self->count;
			if (self->shared) pthread_mutex_unlock(
				&self->l->lock);
#else
			atomic_fetch_sub_explicit(&self->count, 1,
				memory_order_release);
#endif
		    }
		}
		else
		{
		    uint64_t ehash = PSC_Hash_bytes(self->hash,
			    e->key, e->keysz);
		    if (depth > 1)
		    {
			ehash >>= 16 + (depth - 2) * 4;
			ht4 = PSC_malloc(sizeof *ht4);
			memset(ht4, 0, sizeof *ht4);
			if (depth >= MAXNEST - 1)
			{
			    if (self->deleter)
			    {
				EntryL *el = PSC_malloc(sizeof *el);
				memcpy(el, e, sizeof *e);
				el->next = 0;
				ht4->buckets[ehash & 0xf].content = el;
			    }
			    else
			    {
				EntryDL *edl = PSC_malloc(sizeof *edl);
				EntryD *ed = (EntryD *)e;
				memcpy(edl, e, sizeof *e);
				edl->base.next = 0;
				edl->deleter = ed->deleter;
				ht4->buckets[ehash & 0xf].content = edl;
			    }
			    free(e);
			}
			else ht4->buckets[ehash & 0xf].content = e;
			ht4->buckets[ehash & 0xf].type = BT_ITEM;
			bucket->content = ht4;
			bucket->type = BT_HT4;
		    }
		    else
		    {
			ehash >>= 8;
			ht8 = PSC_malloc(sizeof *ht8);
			memset(ht8, 0, sizeof *ht8);
			ht8->buckets[ehash & 0xff].content = e;
			ht8->buckets[ehash & 0xff].type = BT_ITEM;
			bucket->content = ht8;
			bucket->type = BT_HT8;
		    }
		    set(self, bucket, depth, hash, key, keysz, obj, deleter);
		}
	    }
	    break;

	case BT_HT8:
	    ht8 = bucket->content;
	    set(self, ht8->buckets + (hash & 0xff), depth + 1, hash >> 8,
		    key, keysz, obj, deleter);
	    break;

	case BT_HT4:
	    ht4 = bucket->content;
	    set(self, ht4->buckets + (hash & 0xf), depth + 1, hash >> 4,
		    key, keysz, obj, deleter);
	    break;
    }
}

SOEXPORT void PSC_Dictionary_set(PSC_Dictionary *self,
	const void *key, size_t keysz, void *obj, void (*deleter)(void *))
{
    uint64_t hash = PSC_Hash_bytes(self->hash, key, keysz);
    if (deleter == PSC_DICT_NODELETE) deleter = 0;
    if (self->shared)
    {
#ifdef DICT_NO_ATOMICS
	pthread_mutex_lock(self->l->bucklock + (hash & 0xff));
#else
	int res = 0;
	while (!atomic_compare_exchange_weak_explicit(
		    self->l->reserved + (hash & 0xff), &res, -1,
		    memory_order_release, memory_order_acquire)) res = 0;
#endif
    }
    set(self, self->buckets + (hash & 0xff), 1, hash >> 8, key, keysz,
	    obj, deleter);
    if (self->shared)
    {
#ifdef DICT_NO_ATOMICS
	pthread_mutex_unlock(self->l->bucklock + (hash & 0xff));
#else
	atomic_store_explicit(self->l->reserved + (hash & 0xff), 0,
		memory_order_release);
#endif
    }
}

static void *get(const PSC_Dictionary *self, const HashBucket *bucket,
	int depth, uint64_t hash, const void *key, size_t keysz)
{
    Entry *e = 0;
    EntryL *el = 0;
    HashTable8 *ht8 = 0;
    HashTable4 *ht4 = 0;

    switch (bucket->type)
    {
	case BT_ITEM:
	    if (depth >= MAXNEST)
	    {
		el = bucket->content;
		while (el)
		{
		    if (el->base.keysz == keysz &&
			    !memcmp(el->base.key, key, keysz))
		    {
			return el->base.obj;
		    }
		    el = (EntryL *)el->next;
		}
		return 0;
	    }
	    else
	    {
		e = bucket->content;
		if (!e) return 0;
		if (e->keysz != keysz) return 0;
		if (memcmp(e->key, key, keysz)) return 0;
		return e->obj;
	    }

	case BT_HT8:
	    ht8 = bucket->content;
	    return get(self, ht8->buckets + (hash & 0xff), depth + 1,
		    hash >> 8, key, keysz);

	case BT_HT4:
	    ht4 = bucket->content;
	    return get(self, ht4->buckets + (hash & 0xf), depth + 1,
		    hash >> 4, key, keysz);

	default:
	    return 0;
    }
}

SOEXPORT void *PSC_Dictionary_get(const PSC_Dictionary *self,
	const void *key, size_t keysz)
{
    uint64_t hash = PSC_Hash_bytes(self->hash, key, keysz);
    if (self->shared)
    {
#ifdef DICT_NO_ATOMICS
	pthread_mutex_lock(
		((PSC_Dictionary *)self)->l->bucklock + (hash & 0xff));
#else
	int res;
	do
	{
	    res = atomic_load_explicit(self->l->reserved + (hash & 0xff),
		    memory_order_consume);
	    if (res < 0) continue;
	} while (!atomic_compare_exchange_strong_explicit(
		    ((PSC_Dictionary *)self)->l->reserved + (hash & 0xff),
		    &res, res + 1, memory_order_release,
		    memory_order_consume));
#endif
    }
    void *obj = get(self, self->buckets + (hash & 0xff), 1,
	    hash >> 8, key, keysz);
    if (self->shared)
    {
#ifdef DICT_NO_ATOMICS
	pthread_mutex_unlock(
		((PSC_Dictionary *)self)->l->bucklock + (hash & 0xff));
#else
	atomic_fetch_sub_explicit(
		((PSC_Dictionary *)self)->l->reserved + (hash & 0xff), 1,
		memory_order_release);
#endif
    }
    return obj;
}

SOEXPORT size_t PSC_Dictionary_count(const PSC_Dictionary *self)
{
#ifdef DICT_NO_ATOMICS
    if (self->shared) pthread_mutex_lock(&((PSC_Dictionary *)self)->l->lock);
    size_t count = self->count;
    if (self->shared) pthread_mutex_unlock(&((PSC_Dictionary *)self)->l->lock);
    return count;
#else
    return atomic_load_explicit(&self->count, memory_order_consume);
#endif
}

static void removeAll(PSC_Dictionary *self,
	HashBucket *bucket, int depth, size_t *removed,
	int (*matcher)(const void *, size_t, void *, const void *),
	const void *arg)
{
    HashTable8 *ht8 = 0;
    HashTable4 *ht4 = 0;

    switch (bucket->type)
    {
	case BT_ITEM:
	    if (depth >= MAXNEST)
	    {
		EntryL *parent = 0;
		EntryL *el = bucket->content;
		while (el)
		{
		    if (matcher(el->base.key, el->base.keysz,
				el->base.obj, arg))
		    {
			if (self->deleter)
			{
			    if (self->deleter != PSC_DICT_NODELETE)
			    {
				self->deleter(el->base.obj);
			    }
			}
			else
			{
			    EntryDL *edl = (EntryDL *)el;
			    if (edl->deleter) edl->deleter(el->base.obj);
			}
			if (parent) parent->next = el->next;
			else
			{
			    if (el->next) bucket->content = el->next;
			    else bucket->type = BT_EMPTY;
			}
			free(el->base.key);
			free(el);
#ifdef DICT_NO_ATOMICS
			if (self->shared) pthread_mutex_lock(&self->l->lock);
			--self->count;
			if (self->shared) pthread_mutex_unlock(&self->l->lock);
#else
			atomic_fetch_sub_explicit(&self->count, 1,
				memory_order_release);
#endif
			++*removed;
			if (parent) el = (EntryL *)parent->next;
			else if (bucket->type == BT_ITEM) el = bucket->content;
			else el = 0;
		    }
		    else
		    {
			parent = el;
			el = (EntryL *)el->next;
		    }
		}
	    }
	    else
	    {
		Entry *e = bucket->content;
		if (matcher(e->key, e->keysz, e->obj, arg))
		{
		    if (self->deleter)
		    {
			if (self->deleter != PSC_DICT_NODELETE)
			{
			    self->deleter(e->obj);
			}
		    }
		    else
		    {
			EntryD *ed = (EntryD *)e;
			if (ed->deleter) ed->deleter(e->obj);
		    }
		    free(e->key);
		    free(e);
		    bucket->type = BT_EMPTY;
#ifdef DICT_NO_ATOMICS
		    if (self->shared) pthread_mutex_lock(&self->l->lock);
		    --self->count;
		    if (self->shared) pthread_mutex_unlock(&self->l->lock);
#else
		    atomic_fetch_sub_explicit(&self->count, 1,
			    memory_order_release);
#endif
		    ++*removed;
		}
	    }
	    break;

	case BT_HT8:
	    ht8 = bucket->content;
	    for (size_t i = 0; i < HT8_SIZE; ++i)
	    {
		removeAll(self, ht8->buckets + i, depth + 1, removed,
			matcher, arg);
	    }
	    break;

	case BT_HT4:
	    ht4 = bucket->content;
	    for (size_t i = 0; i < HT4_SIZE; ++i)
	    {
		removeAll(self, ht4->buckets + i, depth + 1, removed,
			matcher, arg);
	    }
	    break;

	default:
	    break;
    }
}

SOEXPORT size_t PSC_Dictionary_removeAll(PSC_Dictionary *self,
	int (*matcher)(const void *, size_t, void *, const void *),
	const void *arg)
{
#ifdef DICT_NO_ATOMICS
    if (self->shared) pthread_mutex_lock(&self->l->lock);
    size_t count = self->count;
    if (self->shared) pthread_mutex_unlock(&self->l->lock);
    if (!count) return 0;
#else
    if (!atomic_load_explicit(&self->count, memory_order_consume)) return 0;
#endif
    size_t removed = 0;
    if (self->shared) for (size_t i = 0; i < HT8_SIZE; ++i)
    {
#ifdef DICT_NO_ATOMICS
	pthread_mutex_lock(self->l->bucklock + i);
#else
	int res = 0;
	while (!atomic_compare_exchange_weak_explicit(
		    self->l->reserved + i, &res, -1,
		    memory_order_release, memory_order_acquire)) res = 0;
#endif
	removeAll(self, self->buckets + i, 1, &removed, matcher, arg);
#ifdef DICT_NO_ATOMICS
	pthread_mutex_unlock(self->l->bucklock + i);
#else
	atomic_store_explicit(self->l->reserved + i, 0,
		memory_order_release);
#endif
    }
    else for (size_t i = 0; i < HT8_SIZE; ++i)
    {
	removeAll(self, self->buckets + i, 1, &removed, matcher, arg);
    }
    return removed;
}

static void destroy(PSC_Dictionary *self, HashBucket *bucket, int depth)
{
    HashTable8 *ht8 = 0;
    HashTable4 *ht4 = 0;

    switch (bucket->type)
    {
	case BT_ITEM:
	    if (depth >= MAXNEST)
	    {
		EntryL *el = bucket->content;
		while (el)
		{
		    EntryL *next = (EntryL *)el->next;
		    if (self->deleter)
		    {
			if (self->deleter != PSC_DICT_NODELETE)
			{
			    self->deleter(el->base.obj);
			}
		    }
		    else
		    {
			EntryDL *edl = (EntryDL *)el;
			if (edl->deleter) edl->deleter(el->base.obj);
		    }
		    free(el->base.key);
		    free(el);
		    el = next;
		}
	    }
	    else
	    {
		if (self->deleter)
		{
		    Entry *e = bucket->content;
		    if (self->deleter != PSC_DICT_NODELETE)
		    {
			self->deleter(e->obj);
		    }
		    free(e->key);
		    free(e);
		}
		else
		{
		    EntryD *ed = bucket->content;
		    if (ed->deleter) ed->deleter(ed->base.obj);
		    free(ed->base.key);
		    free(ed);
		}
	    }
	    return;

	case BT_HT8:
	    ht8 = bucket->content;
	    for (size_t i = 0; i < HT8_SIZE; ++i)
	    {
		destroy(self, ht8->buckets + i, depth + 1);
	    }
	    free(ht8);
	    return;

	case BT_HT4:
	    ht4 = bucket->content;
	    for (size_t i = 0; i < HT4_SIZE; ++i)
	    {
		destroy(self, ht4->buckets + i, depth + 1);
	    }
	    free(ht4);
	    return;

	default:
	    return;
    }
}

SOEXPORT void PSC_Dictionary_destroy(PSC_Dictionary *self)
{
    if (!self) return;
    for (size_t i = 0; i < HT8_SIZE; ++i)
    {
	destroy(self, self->buckets + i, 1);
#ifdef DICT_NO_ATOMICS
	if (self->shared) pthread_mutex_destroy(self->l->bucklock + i);
#endif
    }
    PSC_Hash_destroy(self->hash);
#ifdef DICT_NO_ATOMICS
    if (self->shared) pthread_mutex_destroy(&self->l->lock);
#endif
    free(self);
}

