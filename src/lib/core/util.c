#include <poser/core/service.h>
#include <poser/core/util.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

SOEXPORT void *PSC_malloc(size_t size)
{
    void *m = malloc(size);
    if (!m) PSC_Service_panic("memory allocation failed.");
    return m;
}

SOEXPORT void *PSC_realloc(void *ptr, size_t size)
{
    void *m = realloc(ptr, size);
    if (!m) PSC_Service_panic("memory allocation failed.");
    return m;
}

SOEXPORT char *PSC_copystr(const char *src)
{
    if (!src) return 0;
    char *copy = PSC_malloc(strlen(src) + 1);
    strcpy(copy, src);
    return copy;
}

SOEXPORT char *PSC_lowerstr(const char *src)
{
    char *lower = PSC_copystr(src);
    char *p = lower;
    if (p) while (*p)
    {
	*p = tolower(*p);
	++p;
    }
    return lower;
}

SOEXPORT char *PSC_joinstr(const char *delim, char **strings)
{
    int n = 0;
    size_t rlen = 0;
    size_t dlen = strlen(delim);
    char **cur;
    for (cur = strings; *cur; ++cur)
    {
	++n;
	rlen += strlen(*cur);
    }
    if (!n) return 0;
    if (n > 1)
    {
	rlen += (n - 1) * dlen;
    }
    char *joined = PSC_malloc(rlen + 1);
    strcpy(joined, *strings);
    char *w = joined + strlen(*strings);
    cur = strings+1;
    while (*cur)
    {
	strcpy(w, delim);
	w += dlen;
	strcpy(w, *cur);
	w += strlen(*cur);
	++cur;
    }
    return joined;
}

