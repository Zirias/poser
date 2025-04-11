#define _POSIX_C_SOURCE 200112L

#include <poser/core/random.h>

#include "util.h"

#include <errno.h>
#include <poser/core/log.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_GETRANDOM
#  include <sys/random.h>
#endif

static uint64_t prng(void)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    static int seeded = 0;
    static uint64_t s[4] = { 0 };

    pthread_mutex_lock(&mutex);

    if (!seeded)
    {
	seeded = 1;
	s[0] = (uint64_t)time(0);
	for (int i = 0; i < 100; ++i) prng();
    }

    uint64_t num = s[0] + s[3];
    uint64_t tmp = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= tmp;
    s[3] = (s[3]<<45) | (s[3]>>19);

    pthread_mutex_unlock(&mutex);

    return num;
}

SOEXPORT size_t PSC_Random_bytes(uint8_t *buf, size_t count, int onlyReal)
{
    size_t pos = 0;
#ifdef HAVE_GETRANDOM
    while (pos < count)
    {
	errno = 0;
	ssize_t rc = getrandom(buf + pos, count - pos, 0);
	if (rc < 0)
	{
	    if (errno == EAGAIN || errno == EINTR) continue;
	    break;
	}
	pos += rc;
    }
#endif
    if (pos < count)
    {
	if (onlyReal) return pos;
#ifdef HAVE_GETRANDOM
	PSC_Log_msg(PSC_L_WARNING, "random: Could not obtain entropy from "
		"the OS, falling back to internal PRNG.");
#endif
	size_t chunks = (count - pos) / sizeof(uint64_t);
	size_t bytes = (count - pos) % sizeof(uint64_t);
	if (bytes)
	{
	    uint64_t rn = prng();
	    memcpy(buf + pos, &rn, bytes);
	    pos += bytes;
	}
	for (size_t i = 0; i < chunks; ++i)
	{
	    uint64_t rn = prng();
	    memcpy(buf + pos, &rn, sizeof rn);
	    pos += sizeof rn;
	}
    }
    return pos;
}

SOEXPORT size_t PSC_Random_string(char *str, size_t size, int onlyReal)
{
    size_t count = base64decsz(size - 1);
    uint8_t *buf = PSC_malloc(count);
    size_t got = PSC_Random_bytes(buf, count, onlyReal);
    if (got < count) size = base64encsz(got) + 1;
    base64enc(str, buf, got);
    free(buf);
    return size;
}

SOEXPORT char *PSC_Random_createStr(size_t count, int onlyReal)
{
    uint8_t *buf = PSC_malloc(count);
    size_t got = PSC_Random_bytes(buf, count, onlyReal);
    char *str = 0;
    if (got == count)
    {
	size_t strsz = base64encsz(count) + 1;
	str = PSC_malloc(strsz);
	base64enc(str, buf, count);
    }
    free(buf);
    return str;
}

