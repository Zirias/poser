#ifndef POSER_CORE_INT_UTIL_H
#define POSER_CORE_INT_UTIL_H

#include <poser/core/util.h>

#include <stddef.h>
#include <stdint.h>

size_t base64encsz(size_t size) ATTR_CONST;
size_t base64decsz(size_t size) ATTR_CONST;
void base64enc(char *enc, const uint8_t *data, size_t size)
    ATTR_NONNULL((1)) ATTR_NONNULL((2))
    ATTR_ACCESS((write_only, 1)) ATTR_ACCESS((read_only, 2, 3));
void base64dec(uint8_t *data, const char *enc, size_t size)
    ATTR_NONNULL((1)) ATTR_NONNULL((2))
    ATTR_ACCESS((write_only, 1)) ATTR_ACCESS((read_only, 2, 3));

#endif
