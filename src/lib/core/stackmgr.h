#ifndef POSER_CORE_INT_STACKMGR_H
#define POSER_CORE_INT_STACKMGR_H

#include <poser/decl.h>
#include <stddef.h>

int StackMgr_setSize(size_t stacksz);
size_t StackMgr_size(void);
void *StackMgr_getStack(void) ATTR_RETNONNULL;
void StackMgr_returnStack(void *stack);
void StackMgr_clean(void);

#endif
