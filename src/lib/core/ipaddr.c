#define _POSIX_C_SOURCE 200112L

#include "ipaddr.h"

#include <errno.h>
#include <poser/core/proto.h>
#include <poser/core/util.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct PSC_IpAddr
{
    PSC_Proto proto;
    unsigned prefixlen;
    int port;
    uint8_t data[16];
    char str[44];
};
#define IPADDR_CLONEOFFSET offsetof(PSC_IpAddr, proto)

static int parsev4(uint8_t *data, const char *buf)
{
    uint8_t v4dat[4] = { 0 };
    const char *bufp = buf;
    for (int i = 0; i < 4; ++i)
    {
	errno = 0;
	char *endp = 0;
	long byte = strtol(bufp, &endp, 10);
	if (!endp || endp == bufp || (i == 3 ? *endp : *endp != '.')
		|| errno == ERANGE || byte < 0 || byte > 255) return -1;
	v4dat[i] = byte;
	bufp = endp + (i < 3);
    }
    memcpy(data + 12, v4dat, 4);
    return 0;
}

static int parsev6(uint8_t *data, const char *buf)
{
    uint8_t taildat[16] = { 0 };
    size_t len = 0;
    int taillen = -1;

    while (*buf)
    {
	if (taillen < 0 && buf[0] == ':' && buf[1] == ':')
	{
	    taillen = 0;
	    buf += 2;
	    continue;
	}

	errno = 0;
	char *endp = 0;
	long word = strtol(buf, &endp, 16);
	if (!endp || endp == buf || (*endp && *endp != ':')
		|| errno == ERANGE || word < 0 || word > 0xffff) return -1;
	if (len >= 16) return -1;
	buf = endp;
	if (taillen < 0)
	{
	    data[len++] = word >> 8;
	    data[len++] = word & 0xff;
	}
	else
	{
	    taildat[taillen++] = word >> 8;
	    taildat[taillen++] = word & 0xff;
	    len += 2;
	}

	if (*buf && *buf == ':' && buf[1] != ':') ++buf;
    }

    if (taillen > 0) memcpy(data + 16 - taillen, taildat, taillen);
    return 0;
}

static void toString(PSC_IpAddr *self)
{
    int len = 0;
    if (self->proto == PSC_P_IPv4)
    {
	len = sprintf(self->str, "%hhu.%hhu.%hhu.%hhu",
		self->data[12], self->data[13],
		self->data[14], self->data[15]);
	if (len < 0) len = 0;
	if (self->prefixlen < 32) sprintf(self->str + len,
		"/%u", self->prefixlen);
    }
    else
    {
	unsigned word[8];
	int gap = 0;
	int gaplen = 0;
	for (int i = 0; i < 8; ++i)
	{
	    if (!(word[i] = (self->data[2*i] << 8) | self->data[2*i+1]))
	    {
		if (i > gap + gaplen)
		{
		    gap = i;
		    gaplen = 1;
		}
		else ++gaplen;
	    }
	}
	int needcolon = 0;
	for (int i = 0; i < 8;)
	{
	    if (i == gap && gaplen > 0)
	    {
		self->str[len++] = ':';
		self->str[len++] = ':';
		self->str[len] = 0;
		needcolon = 0;
		i += gaplen;
		continue;
	    }
	    if (needcolon) self->str[len++] = ':';
	    needcolon = 1;
	    int rc = sprintf(self->str + len, "%x", word[i]);
	    if (rc < 0) self->str[len] = 0;
	    else len += rc;
	    ++i;
	}
	if (self->prefixlen < 128) sprintf(self->str+len,
		"/%u", self->prefixlen);
    }
}

SOLOCAL PSC_IpAddr *PSC_IpAddr_fromSockAddr(const struct sockaddr *addr)
{
    uint8_t data[16] = {0};
    PSC_Proto proto = PSC_P_ANY;
    unsigned prefixlen = 0;
    int port = -1;
    const struct sockaddr_in *sain;
    const struct sockaddr_in6 *sain6;

    switch (addr->sa_family)
    {
	case AF_INET:
	    sain = (const struct sockaddr_in *)addr;
	    memcpy(data+12, &sain->sin_addr.s_addr, 4);
	    proto = PSC_P_IPv4;
	    prefixlen = 32;
	    port = sain->sin_port;
	    break;

	case AF_INET6:
	    sain6 = (const struct sockaddr_in6 *)addr;
	    memcpy(data, sain6->sin6_addr.s6_addr, 16);
	    proto = PSC_P_IPv6;
	    prefixlen = 128;
	    port = sain6->sin6_port;
	    break;

	default:
	    return 0;
    }

    PSC_IpAddr *self = PSC_malloc(sizeof *self);
    self->proto = proto;
    self->prefixlen = prefixlen;
    self->port = port;
    memcpy(self->data, data, 16);
    toString(self);

    return self;
}

SOEXPORT PSC_IpAddr *PSC_IpAddr_create(const char *str)
{
    uint8_t data[16] = { 0 };
    char buf[44];
    size_t inlen = strlen(str);
    if (inlen < 2 || inlen > 43) return 0;
    strcpy(buf, str);

    unsigned prefixlen = (unsigned)-1;
    char *prstr = strchr(buf, '/');
    if (prstr)
    {
	*prstr++ = 0;
	errno = 0;
	char *endp = 0;
	long prefixval = strtol(prstr, &endp, 10);
	if (!endp || endp == prstr || *endp || errno == ERANGE
		|| prefixval < 0 || prefixval > 128) return 0;
	prefixlen = prefixval;
    }

    PSC_Proto proto = PSC_P_ANY;
    if (parsev4(data, buf) == 0)
    {
	if (prefixlen == (unsigned)-1) prefixlen = 32;
	else if (prefixlen > 32) return 0;
	proto = PSC_P_IPv4;
    }
    else if (parsev6(data, buf) == 0)
    {
	if (prefixlen == (unsigned)-1) prefixlen = 128;
	proto = PSC_P_IPv6;
    }
    else return 0;

    PSC_IpAddr *self = PSC_malloc(sizeof *self);
    self->proto = proto;
    self->prefixlen = prefixlen;
    self->port = -1;
    memcpy(self->data, data, 16);
    toString(self);

    return self;
}

SOEXPORT PSC_IpAddr *PSC_IpAddr_clone(const PSC_IpAddr *other)
{
    PSC_IpAddr *self = PSC_malloc(sizeof *self);
    memcpy(((char *)self) + IPADDR_CLONEOFFSET,
	    ((const char *)other) + IPADDR_CLONEOFFSET,
	    sizeof *self - IPADDR_CLONEOFFSET);
    return self;
}

SOEXPORT PSC_IpAddr *PSC_IpAddr_tov4(const PSC_IpAddr *self,
	const PSC_IpAddr **prefixes)
{
    if (self->prefixlen < 96) return 0;

    int matches = 0;
    for (const PSC_IpAddr **prefix = prefixes; *prefix; ++prefix)
    {
	if (PSC_IpAddr_prefixlen(*prefix) == 96 &&
		PSC_IpAddr_matches(self, *prefix))
	{
	    matches = 1;
	    break;
	}
    }
    if (!matches) return 0;

    PSC_IpAddr *mapped = PSC_malloc(sizeof *mapped);
    mapped->proto = PSC_P_IPv4;
    mapped->prefixlen = self->prefixlen - 96;
    mapped->port = -1;
    memset(mapped->data, 0, 12);
    memcpy(mapped->data+12, self->data+12, 4);
    toString(mapped);

    return mapped;
}

SOEXPORT PSC_IpAddr *PSC_IpAddr_tov6(const PSC_IpAddr *self,
	const PSC_IpAddr *prefix)
{
    if (self->proto != PSC_P_IPv4 || prefix->prefixlen != 96) return 0;

    PSC_IpAddr *mapped = PSC_malloc(sizeof *mapped);
    mapped->proto = PSC_P_IPv6;
    mapped->prefixlen = self->prefixlen + 96;
    memcpy(mapped->data, prefix->data, 12);
    memcpy(mapped->data+12, self->data+12, 4);
    toString(mapped);

    return mapped;
}

SOEXPORT PSC_Proto PSC_IpAddr_proto(const PSC_IpAddr *self)
{
    return self->proto;
}

SOEXPORT unsigned PSC_IpAddr_prefixlen(const PSC_IpAddr *self)
{
    return self->prefixlen;
}

SOLOCAL int PSC_IpAddr_port(const PSC_IpAddr *self)
{
    return self->port;
}

SOLOCAL int PSC_IpAddr_sockAddr(const PSC_IpAddr *self,
	struct sockaddr *addr)
{
    if (self->proto == PSC_P_IPv4)
    {
	struct sockaddr_in *sain = (struct sockaddr_in *)addr;
	memset(sain, 0, sizeof *sain);
	sain->sin_family = AF_INET;
	memcpy(&sain->sin_addr.s_addr, self->data+12, 4);
	return 0;
    }
    if (self->proto == PSC_P_IPv6)
    {
	struct sockaddr_in6 *sain6 = (struct sockaddr_in6 *)addr;
	memset(sain6, 0, sizeof *sain6);
	sain6->sin6_family = AF_INET6;
	memcpy(sain6->sin6_addr.s6_addr, self->data, 16);
	return 0;
    }
    return -1;
}

SOEXPORT const char *PSC_IpAddr_string(const PSC_IpAddr *self)
{
    return self->str;
}

SOEXPORT int PSC_IpAddr_equals(const PSC_IpAddr *self, const PSC_IpAddr *other)
{
    if (self->proto != other->proto) return 0;
    if (self->prefixlen != other->prefixlen) return 0;
    return !memcmp(self->data, other->data, 16);
}

SOEXPORT int PSC_IpAddr_matches(const PSC_IpAddr *self,
	const PSC_IpAddr *prefix)
{
    if (self->proto != prefix->proto) return 0;
    if (self->prefixlen < prefix->prefixlen) return 0;

    unsigned bytes = prefix->prefixlen / 8;
    if (memcmp(self->data, prefix->data, bytes)) return 0;
    unsigned bits = prefix->prefixlen % 8;
    if (!bits) return 1;
    uint8_t mask = (0xff << (8 - bits));
    return ((self->data[bytes] & mask) == (prefix->data[bytes] & mask));
}

SOEXPORT void PSC_IpAddr_destroy(PSC_IpAddr *self)
{
    free(self);
}

