#ifndef POSER_CORE_JSON_H

/** Declarations for the PSC_Json and PSC_JsonSerializer classes
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>
#include <stdint.h>

/** The type of a PSC_Json value */
typedef enum PSC_JsonValueType
{
    PSC_JVT_NULL,	/**< a literal 'null' value */
    PSC_JVT_FALSE,	/**< a literal 'false' value */
    PSC_JVT_TRUE,	/**< a literal 'true' value */
    PSC_JVT_INTEGER,	/**< a JSON number, represented as a 64bit integer */
    PSC_JVT_FLOAT,	/**< a JSON number, represented as a 'double' */
    PSC_JVT_STRING,	/**< a JSON string, should be UTF-8 encoded */
    PSC_JVT_ARRAY,	/**< a JSON array */
    PSC_JVT_OBJECT	/**< a JSON object */
} PSC_JsonValueType;

/** A JSON value capable of representing all JSON types.
 * Instances of PSC_Json are immutable after creation, except for arrays which
 * allow appending items and objects which allow setting properties. There are
 * different constructors for the different types a JSON value can hold, as
 * well as different accessor methods. The accessors attempt to transparently
 * convert the type. To avoid conversion, first check the type of a PSC_Json
 * instance.
 *
 * For numbers, JSON does not distinguish between integer and floating point,
 * still there are two types provided by PSC_Json. When deserializing from a
 * string containing a JSON document, integers are automatically used unless
 * the number has a fractional part.
 *
 * JSON strings must be UTF-8 encoded. RFC 8259 requires them to contain
 * Unicode, but also requires serialized JSON documents to be UTF-8 encoded for
 * exchange with other systems, so requiring UTF-8 everywhere is the
 * straight-forward choice. The encoding is NOT validated by PSC_Json, it's up
 * to the consumer to ensure proper encoding. Note that JSON strings are
 * allowed to contain the NUL character, which is used as an end marker for C
 * strings. When working with strings containing NUL, make sure to correctly
 * use the size arguments of the respective functions.
 * @class PSC_Json json.h <poser/core/json.h>
 */
C_CLASS_DECL(PSC_Json);

/** A JSON serializer and deserializer.
 * This class can convert between serialized JSON documents and PSC_Json
 * objects.
 *
 * When deserializing, everything allowed by RFC 8259 is accepted, including
 * any kind of escaped characters in JSON strings. UTF-8 encoding of strings
 * is not checked, if required, this must be checked on the deserialized
 * objects. The deserializer ignores trailing ',' characters in arrays and
 * objects although not allowed by RFC 8259. Any other error will make the
 * whole deserialization fail. As of now, no details about parsing errors are
 * reported, this might change later.
 *
 * When serializing, the most compact form possible is used, which means
 * there is no whitespace added and in JSON strings, only characters strictly
 * required to be escaped are escaped. Serializer options might be added
 * later to allow configuration of the behavior.
 * @class PSC_JsonSerializer json.h <poser/core/json.h>
 */
C_CLASS_DECL(PSC_JsonSerializer);

/** Options for the JSON serializer.
 * These are currently not available and reserved for future extensions.
 * @class PSC_JsonSerializerOpts json.h <poser/core/json.h>
 */
C_CLASS_DECL(PSC_JsonSerializerOpts);

/** Create a JSON 'null' value.
 * Creates a new PSC_Json representing 'null'
 * @memberof PSC_Json
 * @returns a newly created PSC_Json representing 'null'
 */
DECLEXPORT PSC_Json *
PSC_Json_createNull(void)
    ATTR_RETNONNULL;

/** Create a boolean JSON value.
 * Creates a new PSC_Json representing 'true' or 'false'
 * @memberof PSC_Json
 * @param value 0 for false, non-zero for true
 * @returns a newly created PSC_Json representing 'true' or 'false'
 */
DECLEXPORT PSC_Json *
PSC_Json_createBool(int value)
    ATTR_RETNONNULL;

/** Create an integer JSON value.
 * Creates a new PSC_Json representing an integer number
 * @memberof PSC_Json
 * @param value the number
 * @returns a newly created PSC_Json representing an integer number
 */
DECLEXPORT PSC_Json *
PSC_Json_createInteger(int64_t value)
    ATTR_RETNONNULL;

/** Create a floating point JSON value.
 * Creates a new PSC_Json representing a floating point number
 * @memberof PSC_Json
 * @param value the number
 * @returns a newly created PSC_Json representing a floating point number
 */
DECLEXPORT PSC_Json *
PSC_Json_createFloat(double value)
    ATTR_RETNONNULL;

/** Create a JSON string copying it.
 * Creates a new PSC_Json representing a string, which is copied
 * @memberof PSC_Json
 * @param value the string, should be UTF-8 encoded
 * @param len the length of the string, or 0 to determine it from the string
 *            contents. Required if the string contains NUL characters.
 * @returns a newly created PSC_Json representing a string
 */
DECLEXPORT PSC_Json *
PSC_Json_createString(const char *value, size_t len)
    ATTR_NONNULL((1)) ATTR_RETNONNULL;

/** Create a JSON string from static data.
 * Creates a new PSC_Json representing a string by referencing static data
 * @memberof PSC_Json
 * @param value the string, should be UTF-8 encoded
 * @param len the length of the string, or 0 to determine it from the string
 *            contents. Required if the string contains NUL characters.
 * @returns a newly created PSC_Json representing a string
 */
DECLEXPORT PSC_Json *
PSC_Json_createStaticString(const char *value, size_t len)
    ATTR_NONNULL((1)) ATTR_RETNONNULL;

/** Create a JSON string owning the passed object.
 * Creates a new PSC_Json representing a string by taking ownership
 * @memberof PSC_Json
 * @param value the string, should be UTF-8 encoded
 * @param len the length of the string, or 0 to determine it from the string
 *            contents. Required if the string contains NUL characters.
 * @returns a newly created PSC_Json representing a string
 */
DECLEXPORT PSC_Json *
PSC_Json_createOwnedString(char *value, size_t len)
    ATTR_NONNULL((1)) ATTR_RETNONNULL;

/** Create a JSON array.
 * Creates a new PSC_Json representing an array
 * @memberof PSC_Json
 * @returns a newly created PSC_Json representing an (empty) array
 */
DECLEXPORT PSC_Json *
PSC_Json_createArray(void)
    ATTR_RETNONNULL;

/** Create a JSON object.
 * Creates a new PSC_Json representing an object
 * @memberof PSC_Json
 * @returns a newly created PSC_Json representing an (empty) object
 */
DECLEXPORT PSC_Json *
PSC_Json_createObject(void)
    ATTR_RETNONNULL;

/** Append a value to a JSON array.
 * Append a given JSON value to a JSON array, fails if the instance is not
 * an JSON array.
 * @memberof PSC_Json
 * @param self the PSC_Json array
 * @param item the JSON value to append
 * @returns 0 on success, -1 on error
 */
DECLEXPORT int
PSC_Json_append(PSC_Json *self, PSC_Json *item)
    CMETHOD ATTR_NONNULL((2));

/** Set a property on a JSON object.
 * Sets a named property of a JSON object to a given JSON value, possibly
 * replacing an already stored property by the same name, fails if the
 * instance is not a JSON object.
 * @memberof PSC_Json
 * @param self the PSC_Json object
 * @param name the name of the property
 * @param namelen the length of the name, or 0 to determine it from the name
 *                contents. Required if the name contains NUL characters.
 * @param value the JSON value to set the property to
 * @returns 0 on success, -1 on error
 */
DECLEXPORT int
PSC_Json_setProperty(PSC_Json *self, const char *name, size_t namelen,
	PSC_Json *value)
    CMETHOD ATTR_NONNULL((2)) ATTR_NONNULL((4));

/** The type of a JSON value.
 * @memberof PSC_Json
 * @param self the PSC_Json
 * @returns the type
 */
DECLEXPORT PSC_JsonValueType
PSC_Json_type(const PSC_Json *self)
    CMETHOD ATTR_PURE;

/** The JSON value as a boolean.
 * Evaluates to true for an actual 'true' value, a non-zero number or a
 * non-empty string, array or object, false otherwise.
 * @memberof PSC_Json
 u* @param self the PSC_Json
 * @returns 1 for true, 0 for false
 */
DECLEXPORT int
PSC_Json_bool(const PSC_Json *self)
    CMETHOD ATTR_PURE;

/** The JSON value as an integer.
 * For 'null' and 'false' values evaluates to 0, for 'true' to 1. For
 * floating point numbers, the fractional part is discarded, but overflows
 * might occur. For strings, their content is parsed as a number. For arrays
 * and objects, evaluates to the number of entries.
 * @memberof PSC_Json
 * @param self the PSC_Json
 * @returns the value as an integer
 */
DECLEXPORT int64_t
PSC_Json_integer(const PSC_Json *self)
    CMETHOD ATTR_PURE;

/** The JSON value as a floating point number.
 * For 'null' and 'false' values evaluates to 0, for 'true' to 1. Integer
 * numbers are converted to double. For strings, their content is parsed as
 * a number. For arrays and objects, evaluates to the number of entries.
 * @memberof PSC_Json
 * @param self the PSC_Json
 * @returns the value as a floating point number
 */
DECLEXPORT double
PSC_Json_float(const PSC_Json *self)
    CMETHOD ATTR_PURE;

/** The JSON value as a string.
 * For 'null', 'false' and 'true' values, returns exactly these literals as
 * a string. Numbers are converted to strings. For an array or object, returns
 * '[array]' or '[object]'.
 * @memberof PSC_Json
 * @param self the PSC_Json
 * @returns the value as a string
 */
DECLEXPORT const char *
PSC_Json_string(const PSC_Json *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** The length of a JSON value.
 * Evaluates to 0 for 'null', 1 for 'true', 'false' or a number, the number
 * of bytes for strings (keep in mind that's NOT the number of characters in
 * UTF-8), or the number of items resp. properties for arrays and objects.
 * @memberof PSC_Json
 * @param self the PSC_Json
 * @returns the length of the JSON value
 */
DECLEXPORT size_t
PSC_Json_length(const PSC_Json *self)
    CMETHOD ATTR_PURE;

/** Get a specific item from a JSON array or object.
 * For arrays, get the i'th stored item. For objects, get the key of the i'th
 * property as a PSC_Json string. For all other types, always returns NULL
 * @memberof PSC_Json
 * @param self the PSC_Json
 * @returns the requested item, or NULL if the item does not exist
 */
DECLEXPORT const PSC_Json *
PSC_Json_itemAt(const PSC_Json *self, size_t i)
    CMETHOD ATTR_PURE;

/** Get the value of a JSON property.
 * Only defined on JSON objects, always returns NULL for all other types
 * @memberof PSC_Json
 * @param self the PSC_Json
 * @param name the name of the property
 * @param namelen the lenght of the name, or 0 to determine it from the name
 *                contents. Required if the name contains NUL characters.
 * @returns the value of the property, or NULL if the property does not exist
 */
DECLEXPORT const PSC_Json *
PSC_Json_property(const PSC_Json *self, const char *name, size_t namelen)
    CMETHOD ATTR_NONNULL((2)) ATTR_PURE;

/** PSC_Json destructor.
 * Destroys a JSON value. For arrays and objects, all items resp. properties
 * are destroyed as well.
 * @memberof PSC_Json
 * @param self the PSC_Json
 */
DECLEXPORT void
PSC_Json_destroy(PSC_Json *self);

/** PSC_JsonSeriaizer default constructor.
 * Creates a new PSC_JsonSerializer
 * @memberof PSC_JsonSerializer
 * @param opts Options for the serializer, currently ignored (pass NULL)
 * @returns a newly created PSC_JsonSerializer
 */
DECLEXPORT PSC_JsonSerializer *
PSC_JsonSerializer_create(const PSC_JsonSerializerOpts *opts)
    ATTR_RETNONNULL;

/** Serialize a PSC_Json value to a given buffer.
 * The result will be truncated if the buffer isn't large enough. Unless the
 * size of the buffer is 0, the result will always be NUL-terminated.
 * @memberof PSC_JsonSerializer
 * @param self the PSC_JsonSerializer
 * @param buf the buffer to hold the result
 * @param bufsz the size of the buffer
 * @param value the JSON value to serialize
 * @returns the number of bytes written to the buffer, including the
 *          terminating NUL byte, or bufsz+1 if truncation occured
 */
DECLEXPORT size_t
PSC_JsonSerializer_serializeTo(PSC_JsonSerializer *self,
	char *buf, size_t bufsz, const PSC_Json *value)
    CMETHOD ATTR_NONNULL((2)) ATTR_NONNULL((4));

/** Serialize a PSC_Json value.
 * @memberof PSC_JsonSerializer
 * @param self the PSC_JsonSerializer
 * @param value the JSON value to serialize
 * @returns a newly created string containing the serialized JSON
 */
DECLEXPORT char *
PSC_JsonSerializer_serialize(PSC_JsonSerializer *self, const PSC_Json *value)
    CMETHOD ATTR_NONNULL((2));

/** Deserialize a JSON document.
 * @memberof PSC_JsonSerializer
 * @param self the PSC_JsonSerializer
 * @param json the JSON document to deserialize
 * @returns a JSON value of the deserialized document, or NULL on errors
 */
DECLEXPORT PSC_Json *
PSC_JsonSerializer_deserialize(PSC_JsonSerializer *self, const char *json)
    CMETHOD ATTR_NONNULL((2));

/** PSC_JsonSerializer destructor.
 * @memberof PSC_JsonSerializer
 * @param self the PSC_JsonSerializer
 */
DECLEXPORT void
PSC_JsonSerializer_destroy(PSC_JsonSerializer *self);

#endif
