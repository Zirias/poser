#ifndef POSER_CORE_UTIL_H
#define POSER_CORE_UTIL_H

/** generic utility functions
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>

/** Allocate memory.
 * This is a wrapper around malloc() that ensures only valid pointers are
 * returned. In case of an allocation error, PSC_Service_panic() is called.
 * @param size size of the memory to allocate
 * @returns the newly allocated memory
 */
DECLEXPORT void *
PSC_malloc(size_t size)
    ATTR_MALLOC ATTR_RETNONNULL ATTR_ALLOCSZ((1));

/** Re-allocate memory.
 * This is a wrapper around realloc() that ensures only valid pointers are
 * returned. In case of an allocation error, PSC_Service_panic() is called.
 * @param ptr the previously allocated memory
 * @param size new size for the allocated memory
 * @returns the newly allocated memory
 */
DECLEXPORT void *
PSC_realloc(void *ptr, size_t size)
    ATTR_RETNONNULL ATTR_ALLOCSZ((2));

/** Copy a string.
 * This is similar to strdup(), but uses PSC_malloc() internally.
 * @param src the string to copy
 * @returns a newly allocated copy of the string
 */
DECLEXPORT char *
PSC_copystr(const char *src)
    ATTR_MALLOC;

/** Lowercase a string.
 * This creates a copy of the string in all lowercase.
 * @param src the string to copy
 * @returns a newly allocated lowercased string
 */
DECLEXPORT char *
PSC_lowerstr(const char *src)
    ATTR_MALLOC;

/** Join multiple strings.
 * This joins an array of strings into a new string, putting a delimeter
 * between all the elements.
 * @param delim the delimeter
 * @param strings the strings to join, array must end with a NULL pointer
 * @returns a newly allocated joined string
 */
DECLEXPORT char *
PSC_joinstr(const char *delim, char **strings)
    ATTR_MALLOC ATTR_NONNULL((1));

#endif
