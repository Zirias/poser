#ifndef POSER_CORE_DICTIONARY_H
#define POSER_CORE_DICTIONARY_H

/** Declarations for the PSC_Dictionary class
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>

/** A dictionary storing any data objects using any type of keys.
 * Keys are interpreted as arrays of bytes of a specified size, so any object
 * stored in contiguous memory can be used as a key. For a string key, just
 * pass the string length as the size.
 *
 * This works internally as a structure of nested hash tables. A 64bit hash
 * function (currently xxHash 3) is used to calculate a hash of the given key.
 * The root of a PSC_Dictionary is a hash table with 256 entries, using the
 * lowest 8 bits of the hash. On collissions, a second level hash table with
 * another 256 entries is created, using the next 8 bits of the hash.
 *
 * Further collissions in a second-level table will create more child tables,
 * but only with 16 entries, using another 4 bits of the hash, which is
 * repeated until there are no bits of the hash left. If this is ever reached,
 * collissions are resolved using a linked list for the affected bucket of the
 * innermost hash table.
 * @class PSC_Dictionary dictionary.h <poser/core/dictionary.h>
 */
C_CLASS_DECL(PSC_Dictionary);

/** Dummy deleter, indicating entries should not be deleted.
 * @memberof PSC_Dictionary
 * @static
 */
DECLDATA void (*PSC_DICT_NODELETE)(void *);

/** PSC_Dictionary default constructor.
 * Creates a new PSC_Dictionary
 * @memberof PSC_Dictionary
 * @param deleter Function to delete stored objects when they are removed,
 *                replaced or the PSC_Dictionary is destroyed. If this is given
 *                as NULL, individual deletion functions can be passed to
 *                PSC_Dictionary_set(), otherwise functions passed there are
 *                ignored. Can be set to the special value PSC_DICT_NODELETE
 *                to completely disable deletion of stored objects for the
 *                whole PSC_Dictionary.
 * @param shared If set non-zero, the dictionary is configured to be used from
 *               multiple threads.
 * @returns a newly created PSC_Dictionary
 */
DECLEXPORT PSC_Dictionary *
PSC_Dictionary_create(void (*deleter)(void *), int shared);

/** Set a new object for a given key.
 * If the key already exists, the associated object is replaced.
 * @memberof PSC_Dictionary
 * @param self the PSC_Dictionary
 * @param key the key
 * @param keysz the size of the key
 * @param obj the new object to store, or NULL to remove the object for the
 *            given key
 * @param deleter Individual deletion function for the stored object, ignored
 *                if the dictionary was constructed with global deleter.
 */
DECLEXPORT void
PSC_Dictionary_set(PSC_Dictionary *self, const void *key, size_t keysz,
	void *obj, void (*deleter)(void *))
    CMETHOD ATTR_NONNULL((2));

/** Retrieve a stored object by its key.
 * @memberof PSC_Dictionary
 * @param self the PSC_Dictionary
 * @param key the key
 * @param keysz the size of the key
 * @returns the object stored for the given key, or NULL if not found
 */
DECLEXPORT void *
PSC_Dictionary_get(const PSC_Dictionary *self, const void *key, size_t keysz)
    CMETHOD ATTR_PURE ATTR_NONNULL((2));

/** The total number of stored objects.
 * @memberof PSC_Dictionary
 * @param self the PSC_Dictionary
 * @returns the number of objects stored
 */
DECLEXPORT size_t
PSC_Dictionary_count(const PSC_Dictionary *self)
    CMETHOD ATTR_PURE;

/** Remove all matching entries from dictionary.
 * @memberof PSC_Dictionary
 * @param self the PSC_Dictionary
 * @param matcher function to check each entry, given by its key, the key
 *                size and the stored object itself, according to some
 *                specified argument. Must return 1 to have the entry removed,
 *                0 to leave it.
 * @param arg some argument for the matcher function
 * @returns the number of entries removed
 */
DECLEXPORT size_t
PSC_Dictionary_removeAll(PSC_Dictionary *self,
	int (*matcher)(const void *, size_t, void *, const void *),
	const void *arg)
    CMETHOD ATTR_NONNULL((1));

/** PSC_Dictionary destructor.
 * Stored objects are also destructed if a global deleter is set or they have
 * individual deleters attached.
 * @memberof PSC_Dictionary
 * @param self the PSC_Dictionary
 */
void
PSC_Dictionary_destroy(PSC_Dictionary *self);

#endif
