#ifndef POSER_CORE_RANDOM_H
#define POSER_CORE_RANDOM_H

/** declarations for the PSC_Random class
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>

/** Get some random data.
 * Provides static methods to obtain random bytes and random strings.
 * If available, arc4random() is used for that, which is expected to never
 * block, never fail and return cryptographically secure random data.
 *
 * If arc4random is not available, several other methods are tried.
 *
 * - If available, getrandom() is the first fallback, with flags matching
 *   the flags specified in the call to PSC_Random.
 * - If this is unavailable or fails, reading from /dev/urandom or /dev/random
 *   is tried if applicable given the flags specified.
 * - Finally, if cryptographically secure randomness is not specifically
 *   requested, an internal xorshift-based PRNG is used.
 *
 * @class PSC_Random random.h <poser/core/random.h>
 */

/** Flags controlling how random bytes are obtained */
typedef enum PSC_RandomFlags
{
    PSC_RF_ANY		= 0,	    /**< Allow any method for random data */
    PSC_RF_NONBLOCK	= (1 << 0), /**< Don't use methods that might block */
    PSC_RF_WLOGPSEUDO	= (1 << 1), /**< Log a warning when using simple PRNG */
    PSC_RF_ELOGPSEUDO	= (1 << 2), /**< Log an error when using simple PRNG */
    PSC_RF_SECURE	= (1 << 3)  /**< Never use a simple PRNG */
} PSC_RandomFlags;

/** Fill a buffer with random bytes.
 * This never fails unless PSC_RF_SECURE is given and arc4random() is not
 * available.
 * @memberof PSC_Random
 * @static
 * @param buf the buffer to fill
 * @param count the number of random bytes put into @p buf
 * @param flags flags controlling methods used
 * @returns the number of bytes written, which will be less than @p count
 *          on error
 */
DECLEXPORT size_t
PSC_Random_bytes(void *buf, size_t count, PSC_RandomFlags flags)
    ATTR_NONNULL((1)) ATTR_ACCESS((write_only, 1, 2));

/** Fill a buffer with a random string.
 * The string is constructed by base64-encoding random bytes. This never fails
 * unless PSC_RF_SECURE is given and arc4random() is not available.
 * @memberof PSC_Random
 * @static
 * @param str the buffer to put the string into
 * @param size the length of the string, including a terminating NUL
 * @param flags flags controlling methods used
 * @returns the string size (including a terminating NUL) actually written,
 *          which will be less than @p size on error
 */
DECLEXPORT size_t
PSC_Random_string(char *str, size_t size, PSC_RandomFlags flags)
    ATTR_NONNULL((1)) ATTR_ACCESS((write_only, 1, 2));

/** Create a newly allocated random string.
 * The string is constructed by base64-encoding random bytes. Memory for the
 * new string is allocated, so the caller has to free() it later. This never
 * fails unless PSC_RF_SECURE is given and arc4random() is not available.
 * @memberof PSC_Random
 * @static
 * @param count use this many random bytes to construct the string
 * @param flags flags controlling methods used
 * @returns the new random string, or NULL on error
 */
DECLEXPORT char *
PSC_Random_createStr(size_t count, PSC_RandomFlags flags)
    ATTR_MALLOC;

#endif
