#undef STACK_MFLAGS
#if defined(HAVE_MANON) || defined(HAVE_MANONYMOUS)
#  define _DEFAULT_SOURCE
#  ifdef HAVE_MSTACK
#    ifdef HAVE_MANON
#      define STACK_MFLAGS (MAP_ANON|MAP_PRIVATE|MAP_STACK)
#    else
#      define STACK_MFLAGS (MAP_ANONYMOUS|MAP_PRIVATE|MAP_STACK)
#    endif
#  else
#    ifdef HAVE_MANON
#      define STACK_MFLAGS (MAP_ANON|MAP_PRIVATE)
#    else
#      define STACK_MFLAGS (MAP_ANONYMOUS|MAP_PRIVATE)
#    endif
#  endif
#endif

#include "stackmgr.h"

#include <poser/core/util.h>
#include <pthread.h>
#include <stdlib.h>

#ifdef STACK_MFLAGS
#  include <poser/core/service.h>
#  include <sys/mman.h>
#endif

#define STACKSCHUNK 16

static size_t sz = 2U * 1024U * 1024U;

static void **stacks;
static size_t nstacks;
static size_t cstacks;
static pthread_mutex_t stackslock = PTHREAD_MUTEX_INITIALIZER;

SOLOCAL int StackMgr_setSize(size_t stacksz)
{
    if (stacks) return -1;
    if (stacksz < 64U * 1024U) stacksz = 64U * 1024U;
    if (stacksz > 16U * 1024U * 1024U) stacksz = 16U * 1024U * 1024U;
    sz = stacksz;
    return 0;
}

SOLOCAL size_t StackMgr_size(void)
{
    return sz;
}

SOLOCAL void *StackMgr_getStack(void)
{
    void *stack = 0;
    pthread_mutex_lock(&stackslock);
    if (nstacks) stack = stacks[--nstacks];
    pthread_mutex_unlock(&stackslock);
    if (stack) return stack;
#ifdef STACK_MFLAGS
    stack = mmap(0, sz, PROT_READ|PROT_WRITE, STACK_MFLAGS, -1, 0);
    if (stack == MAP_FAILED) PSC_Service_panic("stack allocation failed.");
#else
    stack = PSC_malloc(sz);
#endif
    return stack;
}

SOLOCAL void StackMgr_returnStack(void *stack)
{
    if (!stack) return;
    pthread_mutex_lock(&stackslock);
    if (nstacks == cstacks)
    {
	cstacks += STACKSCHUNK;
	stacks = PSC_realloc(stacks, cstacks * sizeof *stacks);
    }
    stacks[nstacks++] = stack;
#if defined(STACK_MFLAGS) && defined(HAVE_MADVISE) && defined(HAVE_MADVFREE)
    madvise(stack, sz, MADV_FREE);
#endif
    pthread_mutex_unlock(&stackslock);
}

SOLOCAL void StackMgr_clean(void)
{
    for (size_t i = 0; i < nstacks; ++i)
    {
#ifdef STACK_MFLAGS
	munmap(stacks[i], sz);
#else
	free(stacks[i]);
#endif
    }
    free(stacks);
    nstacks = 0;
    cstacks = 0;
}

