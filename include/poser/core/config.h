#ifndef POSER_CORE_CONFIG_H
#define POSER_CORE_CONFIG_H

#include <poser/decl.h>
#include <stdio.h>

/** declarations for the classes handling configuration
 * @file
 */

/** A description of a configuration section.
 * @class PSC_ConfigSection config.h <poser/core/config.h>
 */
C_CLASS_DECL(PSC_ConfigSection);

/** A description of a configuration element.
 * @class PSC_ConfigElement config.h <poser/core/config.h>
 */
C_CLASS_DECL(PSC_ConfigElement);

/** Context for custom parsing or validating a single element.
 * @class PSC_ConfigParserCtx config.h <poser/core/config.h>
 */
C_CLASS_DECL(PSC_ConfigParserCtx);

/** Generic parser for configurations.
 * @class PSC_ConfigParser config.h <poser/core/config.h>
 */
C_CLASS_DECL(PSC_ConfigParser);

/** Parsed configuration.
 * @class PSC_Config config.h <poser/core/config.h>
 */
C_CLASS_DECL(PSC_Config);

C_CLASS_DECL(PSC_List);

/** Callback for custom parsing or validating a single element.
 * @param ctx a context for parsing/validating
 */
typedef void (*PSC_ConfigElementCallback)(PSC_ConfigParserCtx *ctx);

/** Callback for a custom immediate action when a config element is found.
 * @param data some custom data
 */
typedef void (*PSC_ConfigAction)(void *data);

/** PSC_ConfigSection default constructor.
 * Creates a new PSC_ConfigSection
 * @memberof PSC_ConfigSection
 * @param name the name of the section, NULL for the root section
 * @returns a newly created PSC_ConfigSection
 */
DECLEXPORT PSC_ConfigSection *
PSC_ConfigSection_create(const char *name)
    ATTR_RETNONNULL;

/** Add an element to the config section.
 * @memberof PSC_ConfigSection
 * @param self the PSC_ConfigSection
 * @param element the element to add
 */
DECLEXPORT void
PSC_ConfigSection_add(PSC_ConfigSection *self, PSC_ConfigElement *element)
    CMETHOD ATTR_NONNULL((2));

DECLEXPORT void
PSC_ConfigSection_addHelpArg(PSC_ConfigSection *self,
	const char *description, const char *name, char flag)
    CMETHOD;

DECLEXPORT void
PSC_ConfigSection_addVersionArg(PSC_ConfigSection *self, const char *version,
	const char *description, const char *name, char flag)
    CMETHOD ATTR_NONNULL((2));

/** PSC_ConfigSection destructor.
 * This also destroys any added members.
 * @memberof PSC_ConfigSection
 * @param self the PSC_ConfigSection
 */
DECLEXPORT void
PSC_ConfigSection_destroy(PSC_ConfigSection *self);

DECLEXPORT PSC_ConfigElement *
PSC_ConfigElement_createString(const char *name, const char *defval,
	int required)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT PSC_ConfigElement *
PSC_ConfigElement_createInteger(const char *name, long defval, int required)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT PSC_ConfigElement *
PSC_ConfigElement_createFloat(const char *name, double defval, int required)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT PSC_ConfigElement *
PSC_ConfigElement_createBool(const char *name)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT PSC_ConfigElement *
PSC_ConfigElement_createSection(PSC_ConfigSection *section, int required)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT PSC_ConfigElement *
PSC_ConfigElement_createList(PSC_ConfigElement *element, int required)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT PSC_ConfigElement *
PSC_ConfigElement_createSectionList(PSC_ConfigSection *section, int required)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT PSC_ConfigElement *
PSC_ConfigElement_createAction(const char *name,
	PSC_ConfigAction action, void *actionData)
    ATTR_RETNONNULL ATTR_NONNULL((1)) ATTR_NONNULL((2));

DECLEXPORT void
PSC_ConfigElement_argInfo(PSC_ConfigElement *self, int flag,
	const char *argname)
    CMETHOD;

DECLEXPORT void
PSC_ConfigElement_argOnly(PSC_ConfigElement *self)
    CMETHOD;

DECLEXPORT void
PSC_ConfigElement_fileOnly(PSC_ConfigElement *self)
    CMETHOD;

DECLEXPORT void
PSC_ConfigElement_describe(PSC_ConfigElement *self, const char *description)
    CMETHOD;

DECLEXPORT void
PSC_ConfigElement_parse(PSC_ConfigElement *self,
	PSC_ConfigElementCallback parser)
    CMETHOD;

DECLEXPORT void
PSC_ConfigElement_validate(PSC_ConfigElement *self,
	PSC_ConfigElementCallback validator)
    CMETHOD;

DECLEXPORT void
PSC_ConfigElement_destroy(PSC_ConfigElement *self);

DECLEXPORT const char *
PSC_ConfigParserCtx_string(const PSC_ConfigParserCtx *self)
    CMETHOD ATTR_RETNONNULL;

DECLEXPORT long
PSC_ConfigParserCtx_integer(const PSC_ConfigParserCtx *self)
    CMETHOD;

DECLEXPORT double
PSC_ConfigParserCtx_float(const PSC_ConfigParserCtx *self)
    CMETHOD;

DECLEXPORT void
PSC_ConfigParserCtx_setString(PSC_ConfigParserCtx *self, const char *val)
    CMETHOD;

DECLEXPORT void
PSC_ConfigParserCtx_setInteger(PSC_ConfigParserCtx *self, long val)
    CMETHOD;

DECLEXPORT void
PSC_ConfigParserCtx_setFloat(PSC_ConfigParserCtx *self, double val)
    CMETHOD;

DECLEXPORT void
PSC_ConfigParserCtx_fail(PSC_ConfigParserCtx *self, const char *error)
    CMETHOD;

DECLEXPORT PSC_ConfigParser *
PSC_ConfigParser_create(const PSC_ConfigSection *root)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT void
PSC_ConfigParser_addArgs(PSC_ConfigParser *self, const char *defname,
	int argc, char **argv)
    CMETHOD;

DECLEXPORT void
PSC_ConfigParser_argsAutoUsage(PSC_ConfigParser *self)
    CMETHOD;

DECLEXPORT void
PSC_ConfigParser_addFile(PSC_ConfigParser *self, const char *filename)
    CMETHOD ATTR_NONNULL((2));

DECLEXPORT void
PSC_ConfigParser_autoPage(PSC_ConfigParser *self)
    CMETHOD;

DECLEXPORT int
PSC_ConfigParser_parse(PSC_ConfigParser *self, PSC_Config **config)
    CMETHOD ATTR_NONNULL((2));

DECLEXPORT const PSC_List *
PSC_ConfigParser_errors(const PSC_ConfigParser *self)
    CMETHOD ATTR_RETNONNULL;

DECLEXPORT int
PSC_ConfigParser_usage(const PSC_ConfigParser *self, FILE *out)
    CMETHOD;

DECLEXPORT int
PSC_ConfigParser_help(const PSC_ConfigParser *self, FILE *out)
    CMETHOD;

DECLEXPORT int
PSC_ConfigParser_sampleFile(const PSC_ConfigParser *self, FILE *out)
    CMETHOD;

DECLEXPORT void
PSC_ConfigParser_destroy(PSC_ConfigParser *self);

DECLEXPORT const void *
PSC_Config_get(const PSC_Config *self, const char *name)
    CMETHOD ATTR_NONNULL((2));

DECLEXPORT void
PSC_Config_destroy(PSC_Config *self);

#endif
