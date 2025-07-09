#ifndef POSER_CORE_INT_OBJECTPOOL_H
#define POSER_CORE_INT_OBJECTPOOL_H

#include <poser/decl.h>
#include <stddef.h>

#define POOLOBJ_IDMASK (((size_t)-1ll)>>1)
#define POOLOBJ_USEDMASK (POOLOBJ_IDMASK+1u)

C_CLASS_DECL(ObjectPool);
C_CLASS_DECL(PoolObj);

struct PoolObj
{
    size_t id;
    ObjectPool *pool;
};

#if defined(HAVE_MANON) || defined(HAVE_MANONYMOUS)
void ObjectPool_init(void);
#else
#  define ObjectPool_init()
#endif

ObjectPool *ObjectPool_create(size_t objSz, size_t objsPerChunk);
void *ObjectPool_alloc(ObjectPool *self);
void ObjectPool_destroy(ObjectPool *self, void (*objdestroy)(void *));

void PoolObj_free(void *obj);

#endif
