#ifndef POSER_CORE_INT_UTIL_H
#define POSER_CORE_INT_UTIL_H

#include <poser/core/util.h>

#include <stdint.h>
#include <stdio.h>

#define HT_MASK(bits) (((1U<<(bits))-1)&0xffU)
#define HT_SIZE(bits) (HT_MASK(bits)+1)
#define hash(key, bits) hashstr((key), HT_MASK(bits))

uint8_t hashstr(const char *key, uint8_t mask) ATTR_NONNULL((1)) ATTR_PURE;

#endif
