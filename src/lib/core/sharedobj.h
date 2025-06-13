#ifndef POSER_CORE_INT_SHAREDOBJ_H
#define POSER_CORE_INT_SHAREDOBJ_H

#undef NO_SHAREDOBJ
#if defined(NO_ATOMICS) || defined(__STDC_NO_ATOMICS__)
#  define NO_SHAREDOBJ
#else
#  include <stdatomic.h>
#  if ATOMIC_POINTER_LOCK_FREE != 2
#    define NO_SHAREDOBJ
#  endif
#endif

#ifndef NO_SHAREDOBJ
#  include <poser/decl.h>
#  include <stddef.h>

C_CLASS_DECL(SharedObj);

struct SharedObj
{
    void (*destroy)(void *);
    SharedObj *_Atomic next;
    atomic_uint created;
    atomic_uint retired;
};

void *SharedObj_create(size_t sz, void (*destroy)(void *));
void SharedObj_retire(void *self);

void SOM_init(unsigned nthreads,
	unsigned epochInterval, unsigned destroyInterval);
void SOM_registerThread(void);
void *SOM_reserve(void *_Atomic *ref);
void SOM_release(void);

#else

#define SOM_registerThread()

#endif
#endif
