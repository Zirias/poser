#ifndef POSER_CORE_HASHTABLE_H
#define POSER_CORE_HASHTABLE_H

/** declarations for the PSC_HashTable class
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>
#include <stdint.h>

/** A hash table storing any data objects using string keys.
 * @class PSC_HashTable hashtable.h <poser/core/hashtable.h>
 */
C_CLASS_DECL(PSC_HashTable);

/** An iterator over the contents of an PSC_HashTable.
 * @class PSC_HashTableIterator hashtable.h <poser/core/hashtable.h>
 */
C_CLASS_DECL(PSC_HashTableIterator);

/** PSC_HashTable default constructor.
 * Creates a new PSC_HashTable
 * @memberof PSC_HashTable
 * @param bits number of bits for the hashes (valid range [2..8])
 * @returns a newly created PSC_HashTable
 */
DECLEXPORT PSC_HashTable *
PSC_HashTable_create(uint8_t bits)
    ATTR_RETNONNULL;

/** Set a new object for a key.
 * If there was already an object for the given key, it is replaced. The old
 * object is destroyed if it has a deleter attached.
 * @memberof PSC_HashTable
 * @param self the PSC_HashTable
 * @param key the key
 * @param obj the new object
 * @param deleter optional function to destroy the object
 */
DECLEXPORT void
PSC_HashTable_set(PSC_HashTable *self, const char *key,
	void *obj, void (*deleter)(void *))
    CMETHOD ATTR_NONNULL((2)) ATTR_NONNULL((3));

/** Deletes the object with the specified key.
 * If the object has a deleter attached, it is also destroyed.
 * @memberof PSC_HashTable
 * @param self the PSC_HashTable
 * @param key the key
 * @returns 1 if an object was deleted, 0 otherwise
 */
DECLEXPORT int
PSC_HashTable_delete(PSC_HashTable *self, const char *key)
    CMETHOD ATTR_NONNULL((2));

/** Number of entries.
 * @memberof PSC_HashTable
 * @param self the PSC_HashTable
 * @returns the number of entries
 */
DECLEXPORT size_t
PSC_HashTable_count(const PSC_HashTable *self)
    CMETHOD ATTR_PURE;

/** Gets an object by key.
 * @memberof PSC_HashTable
 * @param self the PSC_HashTable
 * @param key the key
 * @returns the object stored for the give key, or NULL
 */
DECLEXPORT void *
PSC_HashTable_get(const PSC_HashTable *self, const char *key)
    CMETHOD ATTR_NONNULL((2));

/** Creates an iterator for all entries.
 * The iterator contains a snapshot of all objects currently stored,
 * modifications to the PSC_HashTable will not be reflected in the iterator.
 * In its initial state, the iterator points to an invalid position.
 * @memberof PSC_HashTable
 * @param self the PSC_HashTable
 * @returns an iterator
 */
DECLEXPORT PSC_HashTableIterator *
PSC_HashTable_iterator(const PSC_HashTable *self)
    CMETHOD ATTR_RETNONNULL;

/** PSC_HashTable destructor.
 * All stored objects that have a deleter attached are destroyed as well.
 * @memberof PSC_HashTable
 * @param self the PSC_HashTable
 */
DECLEXPORT void
PSC_HashTable_destroy(PSC_HashTable *self);

/** Move to the next position.
 * If the position was invalid, move to the first position. If the position
 * was pointing to the last entry, move to invalid position.
 * @memberof PSC_HashTableIterator
 * @param self the PSC_HashTableIterator
 * @returns 1 if the new position is a valid one, 0 otherwise
 */
DECLEXPORT int
PSC_HashTableIterator_moveNext(PSC_HashTableIterator *self)
    CMETHOD;

/** Gets the key at the current position.
 * @memberof PSC_HashTableIterator
 * @param self the PSC_HashTableIterator
 * @returns the current key, or NULL for the invalid position
 */
DECLEXPORT const char *
PSC_HashTableIterator_key(const PSC_HashTableIterator *self)
    CMETHOD ATTR_PURE;

/** Gets the object at the current position.
 * @memberof PSC_HashTableIterator
 * @param self the PSC_HashTableIterator
 * @returns the current object, or NULL for the invalid position
 */
DECLEXPORT void *
PSC_HashTableIterator_current(const PSC_HashTableIterator *self)
    CMETHOD ATTR_PURE;

/** PSC_HashTableIterator destructor.
 * @memberof PSC_HashTableIterator
 * @param self the PSC_HashTableIterator
 */
DECLEXPORT void
PSC_HashTableIterator_destroy(PSC_HashTableIterator *self);

#endif
