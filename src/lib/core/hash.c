#include <poser/core/hash.h>

#include "xxhash.h"

#include <poser/core/random.h>
#include <poser/core/util.h>
#include <stdlib.h>
#include <string.h>

#define SECRETSIZE 192

struct PSC_Hash
{
    int flags;
    uint8_t secret[];
};

DECLEXPORT PSC_Hash *PSC_Hash_create(int func, int flags)
{
    (void)func;

    PSC_Hash *self = PSC_malloc(sizeof *self + (flags ? SECRETSIZE : 0));
    if ((self->flags = flags))
    {
	if (PSC_Random_bytes(self->secret, SECRETSIZE, 0) != SECRETSIZE)
	{
	    free(self);
	    return 0;
	}
    }
    return self;
}

DECLEXPORT uint64_t PSC_Hash_bytes(PSC_Hash *self,
	const uint8_t *data, size_t size)
{
    return self->flags
	? XXH3_64bits_withSecret(data, size, self->secret, SECRETSIZE)
	: XXH3_64bits(data, size);
}

DECLEXPORT uint64_t PSC_Hash_string(PSC_Hash *self, const char *str)
{
    return PSC_Hash_bytes(self, (const uint8_t *)str, strlen(str));
}

DECLEXPORT void PSC_Hash_destroy(PSC_Hash *self)
{
    free(self);
}

