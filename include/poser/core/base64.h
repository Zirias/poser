#ifndef POSER_CORE_BAS64_H
#define POSER_CORE_BAS64_H

/** declarations for the PSC_Base64 class
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>

/** Encode and decode data using Base64.
 * Provides static methods to encode and decode binary data with Base64.
 * @class PSC_Base64 base64.h <poser/core/base64.h>
 */

/** Calculate Base64-encoded size.
 * @memberof PSC_Base64
 * @static
 * @param size the size of some binary data
 * @returns the length of the Base64-encoded string
 */
DECLEXPORT size_t
PSC_Base64_encodedLen(size_t size)
    ATTR_CONST;

/** Calculate Base64-decoded size.
 * @memberof PSC_Base64
 * @static
 * @param len the length of a Base64-encoded string
 * @returns the size of the decoded binary data
 */
DECLEXPORT size_t
PSC_Base64_decodedSize(size_t len)
    ATTR_CONST;

/** Base64-encode data to a given buffer.
 * @memberof PSC_Base64
 * @static
 * @param enc buffer to write Base64-encoded string to, must have enough
 *            room for the encoded string length plus a NUL terminator
 * @param data the binary data to encode
 * @param size the size of the binary data
 */
DECLEXPORT void
PSC_Base64_encodeTo(char *enc, const void *data, size_t size)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));

/** Base64-encode data to a newly created string.
 * @memberof PSC_Base64
 * @static
 * @param data the binary data to encode
 * @param size the size of the binary data
 * @returns a newly allocated Base64-encoded string
 */
DECLEXPORT char *
PSC_Base64_encode(const void *data, size_t size)
    ATTR_MALLOC ATTR_NONNULL((1));

/** Base64-decode a string to a given buffer.
 * @memberof PSC_Base64
 * @static
 * @param data buffer to write the decoded data to, must have enough room
 *             for the decoded size
 * @param enc the Base64-encoded string
 * @param len the length of the Base64-encoded string
 */
DECLEXPORT void
PSC_Base64_decodeTo(void *data, const char *enc, size_t len)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));

/** Base64-decode a string to a newly allocated buffer.
 * @memberof PSC_Base64
 * @static
 * @param enc the Base64-encoded string
 * @param size if not NULL, set this to the size of the decoded data
 * @returns a newly allocated buffer containing the decoded data
 */
DECLEXPORT void *
PSC_Base64_decode(const char *enc, size_t *size)
    ATTR_MALLOC ATTR_NONNULL((1));

#endif
