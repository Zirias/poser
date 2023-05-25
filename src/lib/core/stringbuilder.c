#include <poser/core/stringbuilder.h>

#include "util.h"

#include <stdlib.h>
#include <string.h>

#define SBCHUNKSZ 512

struct PSC_StringBuilder
{
    size_t size;
    size_t capa;
    char *str;
};

SOEXPORT PSC_StringBuilder *PSC_StringBuilder_create(void)
{
    PSC_StringBuilder *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    return self;
}

SOEXPORT void PSC_StringBuilder_append(
	PSC_StringBuilder *self, const char *str)
{
    size_t newsz = self->size + strlen(str);
    if (self->capa <= newsz)
    {
	while (self->capa <= newsz) self->capa += SBCHUNKSZ;
	self->str = PSC_realloc(self->str, self->capa);
    }
    strcpy(self->str + self->size, str);
    self->size = newsz;
}

SOEXPORT const char *PSC_StringBuilder_str(const PSC_StringBuilder *self)
{
    return self->str;
}

SOEXPORT void PSC_StringBuilder_destroy(PSC_StringBuilder *self)
{
    if (!self) return;
    free(self->str);
    free(self);
}

