#ifndef POSER_CORE_RANDOM_H
#define POSER_CORE_RANDOM_H

/** declarations for the PSC_Random class
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>
#include <stdint.h>

/** Get some random data.
 * Provides static methods to obtain random bytes and random strings.
 * If available, getrandom() is used for that, otherwise there's also a
 * fallback to an xorshift-based PRNG for pseudo-random data.
 * @class PSC_Random random.h <poser/core/random.h>
 */

/** Fill a buffer with random bytes.
 * This never fails unless only real randomness is requested.
 * @memberof PSC_Random
 * @static
 * @param buf the buffer to fill
 * @param count the number of random bytes put into @p buf
 * @param onlyReal if non-zero, only allow real randomness
 * @returns the number of bytes written, which will be less than @p count
 *          on error
 */
DECLEXPORT size_t
PSC_Random_bytes(uint8_t *buf, size_t count, int onlyReal)
    ATTR_NONNULL((1)) ATTR_ACCESS((write_only, 1, 2));

/** Fill a buffer with a random string.
 * The string is constructed by base64-encoding random bytes. This never fails
 * unless only real randomness is requested.
 * @memberof PSC_Random
 * @static
 * @param str the buffer to put the string into
 * @param size the length of the string, including a terminating NUL
 * @param onlyReal if non-zero, only allow real randomness
 * @returns the string size (including a terminating NUL) actually written,
 *          which will be less than @p size on error
 */
DECLEXPORT size_t
PSC_Random_string(char *str, size_t size, int onlyReal)
    ATTR_NONNULL((1)) ATTR_ACCESS((write_only, 1, 2));

/** Create a newly allocated random string.
 * The string is constructed by base64-encoding random bytes. Memory for the
 * new string is allocated, so the caller has to free() it later. This never
 * fails unless only real randomness is requested.
 * @memberof PSC_Random
 * @static
 * @param count use this many random bytes to construct the string
 * @param onlyReal if non-zero, only allow real randomness
 * @returns the new random string, or NULL on error
 */
DECLEXPORT char *
PSC_Random_createStr(size_t count, int onlyReal)
    ATTR_MALLOC;

#endif
