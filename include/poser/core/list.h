#ifndef POSER_CORE_LIST_H
#define POSER_CORE_LIST_H

#include <poser/decl.h>

#include <stddef.h>

/** declarations for the PSC_List class
 * @file
 */

/** A list of objects.
 * @class PSC_List list.h <poser/core/list.h>
 */
C_CLASS_DECL(PSC_List);

/** An iterator over the contents of a PSC_List.
 * @class PSC_ListIterator list.h <poser/core/list.h>
 */
C_CLASS_DECL(PSC_ListIterator);

/** PSC_List default constructor.
 * Creates a new PSC_List
 * @memberof PSC_List
 * @returns a newly created PSC_List
 */
DECLEXPORT PSC_List *
PSC_List_create(void)
    ATTR_RETNONNULL;

/** Number of entries.
 * @memberof PSC_List
 * @param self the PSC_List
 * @returns the number of entries
 */
DECLEXPORT size_t
PSC_List_size(const PSC_List *self)
    CMETHOD ATTR_PURE;

/** Gets an object by position.
 * @memberof PSC_List
 * @param self the PSC_List
 * @param idx position of the object
 * @returns the object stored at that position, or NULL
 */
DECLEXPORT void *
PSC_List_at(const PSC_List *self, size_t idx)
    CMETHOD ATTR_PURE;

/** Append an object to the list.
 * @memberof PSC_List
 * @param self the PSC_List
 * @param obj the new object
 * @param deleter optional function to destroy the object
 */
DECLEXPORT void
PSC_List_append(PSC_List *self, void *obj, void (*deleter)(void *))
    CMETHOD ATTR_NONNULL((2));

/** Remove a given object from the list.
 * The object will *not* be automatically destroyed.
 * @memberof PSC_List
 * @param self the PSC_List
 * @param obj the object to remove
 */
DECLEXPORT void
PSC_List_remove(PSC_List *self, void *obj)
    CMETHOD ATTR_NONNULL((2));

/** Remove matching objects from the list.
 * Objects that are removed will be destroyed if they have a deleter attached.
 * @memberof PSC_List
 * @param self the PSC_List
 * @param matcher function to compare each object to some specified value, must
 *                return 1 to have that object removed, 0 otherwise
 * @param arg some value for the matcher function to compare the objects
 *            against.
 */
DECLEXPORT void
PSC_List_removeAll(PSC_List *self, int (*matcher)(void *, const void *),
	const void *arg)
    CMETHOD ATTR_NONNULL((2));

/** Creates an iterator for all entries.
 * The iterator contains a snapshot of all objects currently stored,
 * modifications to the PSC_List will not be reflected in the iterator. In its
 * initial state, the iterator points to an invalid position.
 * @memberof PSC_List
 * @param self the PSC_List
 * @returns an iterator
 */
DECLEXPORT PSC_ListIterator *
PSC_List_iterator(const PSC_List *self)
    CMETHOD;

/** Clear the list.
 * All stored objects are removed from the list, and those having a deleter
 * attached are also destroyed.
 * @memberof PSC_List
 * @param self the PSC_List
 */
DECLEXPORT void
PSC_List_clear(PSC_List *self)
    CMETHOD;

/** PSC_List destructor.
 * All stored objects that have a deleter attached are destroyed as well.
 * @memberof PSC_List
 * @param self the PSC_List
 */
DECLEXPORT void
PSC_List_destroy(PSC_List *self);

/** Move to the next position.
 * If the position was invalid, move to the first position. If the position was
 * pointing to the last entry, move to invalid position.
 * @memberof PSC_ListIterator
 * @param self the PSC_ListIterator
 * @returns 1 if the new position is a valid one, 0 otherwise
 */
DECLEXPORT int
PSC_ListIterator_moveNext(PSC_ListIterator *self)
    CMETHOD;

/** Gets the object at the current position.
 * @memberof PSC_ListIterator
 * @param self the PSC_ListIterator
 * @returns the current object, or NULL for the invalid position
 */
DECLEXPORT void *
PSC_ListIterator_current(const PSC_ListIterator *self)
    CMETHOD ATTR_PURE;

/** PSC_ListIterator destructor.
 * @memberof PSC_ListIterator
 * @param self the PSC_ListIterator
 */
DECLEXPORT void
PSC_ListIterator_destroy(PSC_ListIterator *self);

/** Create a List of strings by splitting a given string.
 * The string is split at any of the characters given in delim. Empty fields
 * are ignored.
 * @memberof PSC_List
 * @param str the string to split
 * @param delim characters that are considered delimiting fields
 * @returns a list of strings, or NULL if no non-empty fields were found
 */
DECLEXPORT PSC_List *
PSC_List_fromString(const char *str, const char *delim);

#endif
