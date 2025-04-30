#include <poser/core/base64.h>

#include <poser/core/util.h>

#include <stdint.h>
#include <string.h>

static uint8_t dec1(char val) ATTR_CONST;
static char enc1(uint8_t val) ATTR_CONST;

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

SOEXPORT size_t PSC_Base64_encodedLen(size_t size)
{
    size_t res = size % 3;
    if (res) ++res;
    return 4 * (size/3) + res;
}

SOEXPORT size_t PSC_Base64_decodedSize(size_t len)
{
    size_t res = len % 4;
    if (res > 1) --res;
    return 3 * (len/4) + res;
}

SOEXPORT void PSC_Base64_encodeTo(char *enc, const void *data, size_t size)
{
    size_t pos = 0;
    const uint8_t *d = data;
    while (size-pos >= 3)
    {
	*enc++ = enc1(d[pos]>>2);
	*enc++ = enc1(d[pos]<<4|d[pos+1]>>4);
	*enc++ = enc1(d[pos+1]<<2|d[pos+2]>>6);
	*enc++ = enc1(d[pos+2]);
	pos += 3;
    }
    if (size - pos == 2)
    {
	*enc++ = enc1(d[pos]>>2);
	*enc++ = enc1(d[pos]<<4|d[pos+1]>>4);
	*enc++ = enc1(d[pos+1]<<2);
    }
    else if (pos < size)
    {
	*enc++ = enc1(d[pos]>>2);
	*enc++ = enc1(d[pos]<<4);
    }
    *enc = 0;
}

SOEXPORT char *PSC_Base64_encode(const void *data, size_t size)
{
    char *encoded = PSC_malloc(PSC_Base64_encodedLen(size) + 1);
    PSC_Base64_encodeTo(encoded, data, size);
    return encoded;
}

SOEXPORT void PSC_Base64_decodeTo(void *data, const char *enc, size_t len)
{
    size_t pos = 0;
    uint8_t *d = data;
    uint8_t b1, b2, b3;
    while (len - pos >= 4)
    {
	b1 = dec1(enc[pos++]);
	b2 = dec1(enc[pos++]);
	b3 = dec1(enc[pos++]);
	*d++ = b1<<2|b2>>4;
	*d++ = b2<<4|b3>>2;
	*d++ = b3<<6|dec1(enc[pos++]);
    }
    if (len - pos == 3)
    {
	b1 = dec1(enc[pos++]);
	b2 = dec1(enc[pos++]);
	b3 = dec1(enc[pos]);
	*d++ = b1<<2|b2>>4;
	*d = b2<<4|b3>>2;
    }
    else if (len - pos  == 2)
    {
	b1 = dec1(enc[pos++]);
	*d = b1<<2|dec1(enc[pos])>>4;
    }
}

SOEXPORT void *PSC_Base64_decode(const char *enc, size_t *size)
{
    size_t esz = strlen(enc);
    size_t dsz = PSC_Base64_decodedSize(esz);
    void *decoded = PSC_malloc(dsz);
    PSC_Base64_decodeTo(decoded, enc, esz);
    if (size) *size = dsz;
    return decoded;
}

