#define _DEFAULT_SOURCE
#include <poser/core/list.h>

#include <poser/core/util.h>

#include <stdlib.h>
#include <string.h>

#define INITIALCAPA 8

typedef struct PSC_ListItem
{
    void *obj;
    void (*deleter)(void *);
} PSC_ListItem;

struct PSC_List
{
    PSC_ListItem *items;
    size_t capa;
    size_t count;
};

struct PSC_ListIterator
{
    size_t count;
    size_t pos;
    PSC_ListItem items[];
};

SOEXPORT PSC_List *PSC_List_create(void)
{
    PSC_List *self = PSC_malloc(sizeof *self);
    self->capa = INITIALCAPA;
    self->count = 0;
    self->items = PSC_malloc(self->capa * sizeof *self->items);
    return self;
}

SOEXPORT size_t PSC_List_size(const PSC_List *self)
{
    return self->count;
}

SOEXPORT void *PSC_List_at(const PSC_List *self, size_t idx)
{
    if (idx >= self->count) return 0;
    return self->items[idx].obj;
}

SOEXPORT void PSC_List_append(PSC_List *self, void *obj, void (*deleter)(void *))
{
    if (self->count == self->capa)
    {
	self->capa *= 2;
	self->items = PSC_realloc(self->items,
		self->capa * sizeof *self->items);
    }
    self->items[self->count].obj = obj;
    self->items[self->count].deleter = deleter;
    ++self->count;
}

static void removeAt(PSC_List *self, size_t i, int delete)
{
    if (delete && self->items[i].deleter)
    {
	self->items[i].deleter(self->items[i].obj);
    }
    if (i < self->count - 1)
    {
	memmove(self->items+i, self->items+i+1,
		(self->count-i-1) * sizeof *self->items);
    }
    --self->count;
}

SOEXPORT void PSC_List_remove(PSC_List *self, void *obj)
{
    for (size_t i = 0; i < self->count; ++i)
    {
	if (self->items[i].obj == obj)
	{
	    removeAt(self, i, 0);
	    break;
	}
    }
}

SOEXPORT void PSC_List_removeAll(PSC_List *self,
	int (*matcher)(void *, const void *), const void *arg)
{
    for (size_t i = 0; i < self->count; ++i)
    {
	if (matcher(self->items[i].obj, arg))
	{
	    removeAt(self, i, 1);
	    --i;
	}
    }
}

SOEXPORT PSC_ListIterator *PSC_List_iterator(const PSC_List *self)
{
    PSC_ListIterator *iter = PSC_malloc(sizeof *iter +
	    self->count * sizeof *self->items);
    iter->count = self->count;
    iter->pos = self->count;
    memcpy(iter->items, self->items, self->count * sizeof *self->items);
    return iter;
}

SOEXPORT void PSC_List_clear(PSC_List *self)
{
    for (size_t i = 0; i < self->count; ++i)
    {
	if (self->items[i].deleter) self->items[i].deleter(self->items[i].obj);
    }
    self->count = 0;
}

SOEXPORT void PSC_List_destroy(PSC_List *self)
{
    if (!self) return;
    PSC_List_clear(self);
    free(self->items);
    free(self);
}

SOEXPORT int PSC_ListIterator_moveNext(PSC_ListIterator *self)
{
    if (self->pos >= self->count) self->pos = 0;
    else ++self->pos;
    return self->pos < self->count;
}

SOEXPORT void *PSC_ListIterator_current(const PSC_ListIterator *self)
{
    if (self->pos >= self->count) return 0;
    return self->items[self->pos].obj;
}

SOEXPORT void PSC_ListIterator_destroy(PSC_ListIterator *self)
{
    free(self);
}

SOEXPORT PSC_List *PSC_List_fromString(const char *str, const char *delim)
{
    PSC_List *list = 0;
    char *buf = PSC_copystr(str);
    char *bufp = buf;
    char *word = 0;

    while ((word = strsep(&bufp, delim)))
    {
	if (*word)
	{
	    if (!list) list = PSC_List_create();
	    PSC_List_append(list, PSC_copystr(word), free);
	}
    }

    free(buf);
    return list;
}
