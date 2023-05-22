#ifndef POSER_CORE_UTIL_H
#define POSER_CORE_UTIL_H

#include <poser/decl.h>

#include <stddef.h>

DECLEXPORT void *
PSC_malloc(size_t size)
    ATTR_MALLOC ATTR_RETNONNULL ATTR_ALLOCSZ((1));

DECLEXPORT void *
PSC_realloc(void *ptr, size_t size)
    ATTR_RETNONNULL ATTR_ALLOCSZ((2));

DECLEXPORT char *
PSC_copystr(const char *src)
    ATTR_MALLOC;

DECLEXPORT char *
PSC_lowerstr(const char *src)
    ATTR_MALLOC;

DECLEXPORT char *
PSC_joinstr(const char *delim, char **strings)
    ATTR_MALLOC ATTR_NONNULL((1));

#endif
