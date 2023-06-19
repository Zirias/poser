#ifndef POSER_CORE_STRINGBUILDER_H
#define POSER_CORE_STRINGBUILDER_H

#include <poser/decl.h>

/** declarations for the PSC_StringBuilder class
 * @file
 */

/** A simple string builder
 * @class PSC_StringBuilder stringbuilder.h <poser/core/stringbuilder.h>
 */
C_CLASS_DECL(PSC_StringBuilder);

/** PSC_StringBuilder default constructor.
 * Creates a new PSC_StringBuilder
 * @memberof PSC_StringBuilder
 * @returns a newly created PSC_StringBuilder
 */
DECLEXPORT PSC_StringBuilder *
PSC_StringBuilder_create(void)
    ATTR_RETNONNULL;

/** Append a string to the builder.
 * @memberof PSC_StringBuilder
 * @param self the PSC_StringBuilder
 * @param str the string to append
 */
DECLEXPORT void
PSC_StringBuilder_append(PSC_StringBuilder *self, const char *str)
    CMETHOD ATTR_NONNULL((2));

/** Append a single character to the builder.
 * @memberof PSC_StringBuilder
 * @param self the PSC_StringBuilder
 * @param c the character to append
 */
DECLEXPORT void
PSC_StringBuilder_appendChar(PSC_StringBuilder *self, char c)
    CMETHOD;

/** Get the complete string.
 * @memberof PSC_StringBuilder
 * @param self the PSC_StringBuilder
 * @returns the complete string
 */
DECLEXPORT const char *
PSC_StringBuilder_str(const PSC_StringBuilder *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** PSC_StringBuilder destructor.
 * @memberof PSC_StringBuilder
 * @param self the PSC_StringBuilder
 */
DECLEXPORT void
PSC_StringBuilder_destroy(PSC_StringBuilder *self);

#endif
