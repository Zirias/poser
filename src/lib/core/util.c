#include "util.h"

#include <poser/core/service.h>

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
	*p = tolower((unsigned char)*p);
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

SOEXPORT const char *PSC_basename(const char *path)
{
    size_t seppos;
    while (*path && path[(seppos = strcspn(path, "/"))]) path += seppos + 1;
    if (*path) return path;
    return ".";
}

SOLOCAL uint8_t hashstr(const char *key, uint8_t mask)
{
    size_t h = 5381;
    while (*key)
    {
	h += (h << 5) + ((uint8_t)*key++);
    }
    return h & mask;
}

static uint8_t dec1(char val) ATTR_CONST;
static char enc1(uint8_t val) ATTR_CONST;

SOLOCAL size_t base64encsz(size_t size)
{
    size_t res = size % 3;
    if (res) ++res;
    return 4 * (size/3) + res;
}

SOLOCAL size_t base64decsz(size_t size)
{
    size_t res = size % 4;
    if (res > 1) --res;
    return 3 * (size/4) + res;
}

static char enc1(uint8_t val)
{
    val &= 0x3f;
    if (val < 0x1a) return 'A'+val;
    if (val < 0x34) return 'a'+(val-0x1a);
    if (val < 0x3e) return '0'+(val-0x34);
    return val == 0x3e ? '+' : '/';
}

static uint8_t dec1(char val)
{
    if (val >= 'A' && val <='Z') return val-'A';
    if (val >= 'a' && val <='z') return val-'a'+0x1a;
    if (val >= '0' && val <='9') return val-'0'+0x34;
    if (val == '+') return 0x3e;
    return 0x3f;
}

SOLOCAL void base64enc(char *enc, const uint8_t *data, size_t size)
{
    size_t pos = 0;
    while (size-pos >= 3)
    {
	*enc++ = enc1(data[pos]>>2);
	*enc++ = enc1(data[pos]<<4|data[pos+1]>>4);
	*enc++ = enc1(data[pos+1]<<2|data[pos+2]>>6);
	*enc++ = enc1(data[pos+2]);
	pos += 3;
    }
    if (size - pos == 2)
    {
	*enc++ = enc1(data[pos]>>2);
	*enc++ = enc1(data[pos]<<4|data[pos+1]>>4);
	*enc++ = enc1(data[pos+1]<<2);
    }
    else if (pos < size)
    {
	*enc++ = enc1(data[pos]>>2);
	*enc++ = enc1(data[pos]<<4);
    }
    *enc = 0;
}

SOLOCAL void base64dec(uint8_t *data, const char *enc, size_t size)
{
    size_t pos = 0;
    uint8_t b1, b2, b3;
    while (size-pos >= 4)
    {
	b1 = dec1(enc[pos++]);
	b2 = dec1(enc[pos++]);
	b3 = dec1(enc[pos++]);
	*data++ = b1<<2|b2>>4;
	*data++ = b2<<4|b3>>2;
	*data++ = b3<<6|dec1(enc[pos++]);
    }
    if (size - pos == 3)
    {
	b1 = dec1(enc[pos++]);
	b2 = dec1(enc[pos++]);
	b3 = dec1(enc[pos]);
	*data++ = b1<<2|b2>>4;
	*data = b2<<4|b3>>2;
    }
    else if (size - pos  == 2)
    {
	b1 = dec1(enc[pos++]);
	*data = b1<<2|dec1(enc[pos])>>4;
    }
}

