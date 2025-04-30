#ifndef POSER_CORE_HASH_H
#define POSER_CORE_HASH_H

/** declarations for the PSC_Hash class
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>
#include <stdint.h>

/** Calculate non-cryptographic hashes of some data.
 * This class hashes some given data using xxhash3.
 * @class PSC_Hash hash.h <poser/core/hash.h>
 */
C_CLASS_DECL(PSC_Hash);

/** PSC_Hash default constructor.
 * Creates a new PSC_Hash with given options
 * @memberof PSC_Hash
 * @param func the hash function to use, currently ignored, only xxhash3 is
 *             available
 * @param flags settings for the selected hash function, 0 means defaults.
 *              For xxhash3, 1 means use a random secret (created only once
 *              for all PSC_Hash instances) while hashing
 * @returns a newly created PSC_Hash, or NULL on error
 */
DECLEXPORT PSC_Hash *
PSC_Hash_create(int func, int flags);

/** Calculate a hash from any data.
 * @memberof PSC_Hash
 * @param self the PSC_Hash
 * @param data the data to hash
 * @param size the size of the data
 * @returns the hash value
 */
DECLEXPORT uint64_t
PSC_Hash_bytes(PSC_Hash *self, const void *data, size_t size);

/** Calculate a hash from a string.
 * @memberof PSC_Hash
 * @param self the PSC_Hash
 * @param str the string to hash
 * @returns the hash value
 */
DECLEXPORT uint64_t
PSC_Hash_string(PSC_Hash *self, const char *str);

/** PSC_Hash destructor.
 * @memberof PSC_Hash
 * @param self the PSC_Hash
 */
DECLEXPORT void
PSC_Hash_destroy(PSC_Hash *self);

#endif
