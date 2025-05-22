#include <poser/core/base64.h>

#include <poser/core/util.h>

#include <stdint.h>
#include <string.h>

static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/-_";

static uint8_t dec1(char val) ATTR_CONST;
static char enc1(uint8_t val, PSC_Base64Flags flags) ATTR_CONST;

static char enc1(uint8_t val, PSC_Base64Flags flags)
{
    uint8_t pos = val & 0x3f;
    if ((flags & PSC_B64_URLSAFE) && pos >= 0x3e) pos += 2;
    return alphabet[pos];
}

static uint8_t dec1(char val)
{
    char *pos = strchr(alphabet, val);
    if (!pos) return 0xff;
    uint8_t dec = pos - alphabet;
    if (dec > 0x3f) dec -= 2;
    return dec;
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

SOEXPORT void PSC_Base64_encodeTo(char *enc, const void *data, size_t size,
	PSC_Base64Flags flags)
{
    size_t pos = 0;
    const uint8_t *d = data;
    while (size-pos >= 3)
    {
	*enc++ = enc1(d[pos]>>2, flags);
	*enc++ = enc1(d[pos]<<4|d[pos+1]>>4, flags);
	*enc++ = enc1(d[pos+1]<<2|d[pos+2]>>6, flags);
	*enc++ = enc1(d[pos+2], flags);
	pos += 3;
    }
    if (size - pos == 2)
    {
	*enc++ = enc1(d[pos]>>2, flags);
	*enc++ = enc1(d[pos]<<4|d[pos+1]>>4, flags);
	*enc++ = enc1(d[pos+1]<<2, flags);
    }
    else if (pos < size)
    {
	*enc++ = enc1(d[pos]>>2, flags);
	*enc++ = enc1(d[pos]<<4, flags);
    }
    *enc = 0;
}

SOEXPORT char *PSC_Base64_encode(const void *data, size_t size,
	PSC_Base64Flags flags)
{
    char *encoded = PSC_malloc(PSC_Base64_encodedLen(size) + 1);
    PSC_Base64_encodeTo(encoded, data, size, flags);
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

