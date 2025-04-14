#include <poser/core/dictionary.h>

#include <poser/core/hash.h>
#include <poser/core/util.h>
#include <stdlib.h>
#include <string.h>

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

struct PSC_Dictionary
{
    void (*deleter)(void *);
    PSC_Hash *hash;
    size_t count;
    HashBucket buckets[HT8_SIZE];
};

SOEXPORT void (*PSC_DICT_NODELETE)(void *) =
    (void (*)(void *))PSC_Dictionary_destroy;

SOEXPORT PSC_Dictionary *PSC_Dictionary_create(void (*deleter)(void *))
{
    PSC_Hash *hash = PSC_Hash_create(0, 1);
    if (!hash) return 0;

    PSC_Dictionary *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->deleter = deleter;
    self->hash = hash;
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
    ++self->count;
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
			    --self->count;
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
			--self->count;
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
    set(self, self->buckets + (hash & 0xff), 1, hash >> 8, key, keysz,
	    obj, deleter);
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
    return get(self, self->buckets + (hash & 0xff), 1, hash >> 8, key, keysz);
}

SOEXPORT size_t PSC_Dictionary_count(const PSC_Dictionary *self)
{
    return self->count;
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
			--self->count;
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
		    --self->count;
		    ++*removed;
		}
	    }
	    break;

	case BT_HT8:
	    if (!self->count) return;
	    ht8 = bucket->content;
	    for (size_t i = 0; i < HT8_SIZE; ++i)
	    {
		removeAll(self, ht8->buckets + i, depth + 1, removed,
			matcher, arg);
	    }
	    break;

	case BT_HT4:
	    if (!self->count) return;
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
    if (!self->count) return 0;
    size_t removed = 0;
    for (size_t i = 0; i < HT8_SIZE; ++i)
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
    }
    PSC_Hash_destroy(self->hash);
    free(self);
}

