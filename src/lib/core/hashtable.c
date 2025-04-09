#include <poser/core/hashtable.h>

#include "util.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct PSC_HashTableEntry PSC_HashTableEntry;
struct PSC_HashTableEntry
{
    PSC_HashTableEntry *next;
    char *key;
    void *obj;
    void (*deleter)(void *);
};

struct PSC_HashTable
{
    size_t count;
    uint8_t bits;
    PSC_HashTableEntry *bucket[];
};

typedef struct IteratorEntry
{
    const char *key;
    void *obj;
} IteratorEntry;

struct PSC_HashTableIterator
{
    size_t count;
    size_t pos;
    IteratorEntry entries[];
};

SOEXPORT PSC_HashTable *PSC_HashTable_create(uint8_t bits)
{
    size_t htsize = HT_SIZE(bits);
    PSC_HashTable *self = PSC_malloc(
	    sizeof *self + htsize * sizeof *self->bucket);
    memset(self, 0, sizeof *self + htsize * sizeof *self->bucket);
    self->bits = bits;
    return self;
}

SOEXPORT void PSC_HashTable_set(PSC_HashTable *self, const char *key,
	void *obj, void (*deleter)(void *))
{
    uint8_t h = hashstr(key, self->bits);
    PSC_HashTableEntry *parent = 0;
    PSC_HashTableEntry *entry = self->bucket[h];
    while (entry)
    {
	if (!strcmp(entry->key, key)) break;
	parent = entry;
	entry = parent->next;
    }
    if (entry)
    {
	if (entry->deleter) entry->deleter(entry->obj);
	entry->obj = obj;
	entry->deleter = deleter;
    }
    else
    {
	entry = PSC_malloc(sizeof *entry);
	entry->next = 0;
	entry->key = PSC_copystr(key);
	entry->obj = obj;
	entry->deleter = deleter;
	if (parent) parent->next = entry;
	else self->bucket[h] = entry;
	++self->count;
    }
}

SOEXPORT int PSC_HashTable_delete(PSC_HashTable *self, const char *key)
{
    uint8_t h = hashstr(key, self->bits);
    PSC_HashTableEntry *parent = 0;
    PSC_HashTableEntry *entry = self->bucket[h];
    while (entry)
    {
	if (!strcmp(entry->key, key)) break;
	parent = entry;
	entry = entry->next;
    }
    if (entry)
    {
	if (entry->deleter) entry->deleter(entry->obj);
	if (parent) parent->next = entry->next;
	else self->bucket[h] = entry->next;
	free(entry->key);
	free(entry);
	--self->count;
	return 1;
    }
    return 0;
}

SOEXPORT int PSC_HashTable_deleteAll(PSC_HashTable *self,
	int (*matcher)(const char *, void *, const void *), const void *arg)
{
    int deleted = 0;
    for (unsigned h = 0; h < HT_SIZE(self->bits); ++h)
    {
	PSC_HashTableEntry *parent = 0;
	PSC_HashTableEntry *entry = self->bucket[h];
	while (entry)
	{
	    PSC_HashTableEntry *next = entry->next;
	    if (matcher(entry->key, entry->obj, arg))
	    {
		if (entry->deleter) entry->deleter(entry->obj);
		if (parent) parent->next = next;
		else self->bucket[h] = next;
		free(entry->key);
		free(entry);
		--self->count;
		++deleted;
	    }
	    else parent = entry;
	    entry = next;
	}
    }
    return deleted;
}

SOEXPORT size_t PSC_HashTable_count(const PSC_HashTable *self)
{
    return self->count;
}

SOEXPORT void *PSC_HashTable_get(const PSC_HashTable *self, const char *key)
{
    PSC_HashTableEntry *entry = self->bucket[hashstr(key, self->bits)];
    while (entry)
    {
	if (!strcmp(entry->key, key)) return entry->obj;
	entry = entry->next;
    }
    return 0;
}

SOEXPORT PSC_HashTableIterator *PSC_HashTable_iterator(const PSC_HashTable *self)
{
    PSC_HashTableIterator *iter = PSC_malloc(
	    sizeof *iter + self->count * sizeof *iter->entries);
    iter->count = self->count;
    iter->pos = self->count;
    size_t pos = 0;
    for (unsigned h = 0; h < HT_SIZE(self->bits); ++h)
    {
	PSC_HashTableEntry *entry = self->bucket[h];
	while (entry)
	{
	    iter->entries[pos].key = entry->key;
	    iter->entries[pos].obj = entry->obj;
	    ++pos;
	    entry = entry->next;
	}
    }
    return iter;
}

SOEXPORT void PSC_HashTable_destroy(PSC_HashTable *self)
{
    if (!self) return;
    for (unsigned h = 0; h < HT_SIZE(self->bits); ++h)
    {
	PSC_HashTableEntry *entry = self->bucket[h];
	PSC_HashTableEntry *next;
	while (entry)
	{
	    next = entry->next;
	    if (entry->deleter) entry->deleter(entry->obj);
	    free(entry->key);
	    free(entry);
	    entry = next;
	}
    }
    free(self);
}

SOEXPORT int PSC_HashTableIterator_moveNext(PSC_HashTableIterator *self)
{
    if (self->pos >= self->count) self->pos = 0;
    else ++self->pos;
    return self->pos < self->count;
}

SOEXPORT const char *PSC_HashTableIterator_key(
	const PSC_HashTableIterator *self)
{
    if (self->pos >= self->count) return 0;
    return self->entries[self->pos].key;
}

SOEXPORT void *PSC_HashTableIterator_current(const PSC_HashTableIterator *self)
{
    if (self->pos >= self->count) return 0;
    return self->entries[self->pos].obj;
}

SOEXPORT void PSC_HashTableIterator_destroy(PSC_HashTableIterator *self)
{
    free(self);
}

