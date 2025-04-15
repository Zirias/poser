#define _DEFAULT_SOURCE

#include <poser/core/random.h>

#include <poser/core/base64.h>
#include <poser/core/log.h>
#include <poser/core/util.h>

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef HAVE_ARC4R

#  ifdef HAVE_GETRANDOM
#    include <sys/random.h>
#  else
#    include <fcntl.h>
#    include <unistd.h>
#  endif

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
#endif

SOEXPORT size_t PSC_Random_bytes(uint8_t *buf, size_t count,
	PSC_RandomFlags flags)
{
#ifdef HAVE_ARC4R
    (void) flags;
    arc4random_buf(buf, count);
    return count;
#else
    PSC_LogLevel level = PSC_L_DEBUG;
    if (flags & PSC_RF_ELOGPSEUDO) level = PSC_L_ERROR;
    else if (flags & PSC_RF_WLOGPSEUDO) level = PSC_L_WARNING;

    size_t pos = 0;
#  ifdef HAVE_GETRANDOM
    unsigned grflags = 0;
    if (flags & PSC_RF_NONBLOCK) grflags = GRND_NONBLOCK;
    unsigned grflags2 = grflags;
    if (flags & PSC_RF_SECURE)
    {
	grflags2 |= GRND_RANDOM;
    }
    if (flags & (PSC_RF_WLOGPSEUDO|PSC_RF_ELOGPSEUDO))
    {
	grflags |= GRND_RANDOM;
    }
    int dolog = 0;
    while (pos < count)
    {
	if (dolog == 1 && (flags & (PSC_RF_WLOGPSEUDO|PSC_RF_ELOGPSEUDO)))
	{
	    PSC_Log_msg(level, "random: Could not obtain cryptographically "
		    "secure random data, trying alternative source (urandom) "
		    "which might not be secure.");
	    ++dolog;
	}
	errno = 0;
	ssize_t rc = getrandom(buf + pos, count - pos, grflags);
	if (rc < 0)
	{
	    if (errno == EINTR) continue;
	    if (errno == EAGAIN) rc = 0;
	    else break;
	}
	if (!dolog)
	{
	    grflags = grflags2;
	    ++dolog;
	}
	pos += rc;
    }
#  else
    int rflags = O_RDONLY;
    if (flags & PSC_RF_NONBLOCK) rflags |= O_NONBLOCK;
    int doswitch = 0;
    int rndfd = open("/dev/random", rflags);
    if (rndfd < 0)
    {
	if (flags & PSC_RF_SECURE) return pos;
	rndfd = open("/dev/urandom", rflags);
	if (rndfd < 0) goto useprng;
	doswitch = 1;
    }
    while (pos < count)
    {
	if (doswitch == 1 && (flags & (PSC_RF_WLOGPSEUDO|PSC_RF_ELOGPSEUDO)))
	{
	    PSC_Log_msg(level, "random: Could not obtain cryptographically "
		    "secure random data, trying alternative source (urandom) "
		    "which might not be secure.");
	    ++doswitch;
	}
	errno = 0;
	ssize_t rc = read(rndfd, buf + pos, count - pos);
	if (rc < 0)
	{
	    if (errno == EINTR) continue;
	    if (errno == EAGAIN) rc = 0;
	    else break;
	}
	pos += rc;
	if (pos < count && !doswitch)
	{
	    close(rndfd);
	    if (flags & PSC_RF_SECURE) return pos;
	    rndfd = open("/dev/urandom", rflags);
	    if (rndfd < 0) goto useprng;
	    ++doswitch;
	}
    }
    close(rndfd);
useprng:
#  endif
    if (pos < count)
    {
	if (flags & PSC_RF_SECURE) return pos;
	if (flags & (PSC_RF_WLOGPSEUDO|PSC_RF_ELOGPSEUDO))
	{
	    PSC_Log_msg(level, "random: Could not obtain random data, falling "
		    "back to xorshift-based internal PRNG, which is NOT "
		    "cryptographically secure.");
	}
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
#endif
}

SOEXPORT size_t PSC_Random_string(char *str, size_t size,
	PSC_RandomFlags flags)
{
    size_t count = PSC_Base64_decodedSize(size - 1);
    uint8_t *buf = PSC_malloc(count);
    size_t got = PSC_Random_bytes(buf, count, flags);
    if (got < count) size = PSC_Base64_encodedLen(got) + 1;
    PSC_Base64_encodeTo(str, buf, got);
    free(buf);
    return size;
}

SOEXPORT char *PSC_Random_createStr(size_t count, PSC_RandomFlags flags)
{
    uint8_t *buf = PSC_malloc(count);
    size_t got = PSC_Random_bytes(buf, count, flags);
    char *str = 0;
    if (got == count) str = PSC_Base64_encode(buf, count);
    free(buf);
    return str;
}

