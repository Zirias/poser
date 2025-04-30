#include <poser/core/hash.h>

#include "xxhash.h"

#include <poser/core/random.h>
#include <poser/core/util.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define SECRETSIZE 192

struct PSC_Hash
{
    uint8_t *secret;
};

static uint8_t *getSecret(void)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static int haveSecret = 0;
    static uint8_t secret[SECRETSIZE];

    uint8_t *s = 0;

    pthread_mutex_lock(&lock);
    if (haveSecret) s = secret;
    else
    {
	if (PSC_Random_bytes(secret, SECRETSIZE, PSC_RF_NONBLOCK)
		== SECRETSIZE)
	{
	    s = secret;
	    haveSecret = 1;
	}
    }
    pthread_mutex_unlock(&lock);
    return s;
}

SOEXPORT PSC_Hash *PSC_Hash_create(int func, int flags)
{
    (void)func;

    uint8_t *secret = 0;
    if (flags && !(secret = getSecret())) return 0;

    PSC_Hash *self = PSC_malloc(sizeof *self);
    self->secret = secret;
    return self;
}

SOEXPORT uint64_t PSC_Hash_bytes(PSC_Hash *self,
	const void *data, size_t size)
{
    return self->secret
	? XXH3_64bits_withSecret(data, size, self->secret, SECRETSIZE)
	: XXH3_64bits(data, size);
}

SOEXPORT uint64_t PSC_Hash_string(PSC_Hash *self, const char *str)
{
    return PSC_Hash_bytes(self, (const uint8_t *)str, strlen(str));
}

SOEXPORT void PSC_Hash_destroy(PSC_Hash *self)
{
    free(self);
}

