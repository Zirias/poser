#include <poser/core/json.h>

#include <inttypes.h>
#include <poser/core/dictionary.h>
#include <poser/core/list.h>
#include <poser/core/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char litNull[] = "null";
static const char litFalse[] = "false";
static const char litTrue[] = "true";

struct PSC_Json
{
    PSC_JsonValueType type;
};

typedef struct JsonInteger
{
    PSC_Json base;
    int64_t value;
    char *str;
} JsonInteger;

typedef struct JsonFloat
{
    PSC_Json base;
    double value;
    char *str;
} JsonFloat;

typedef struct JsonString
{
    PSC_Json base;
    int owned;
    size_t len;
    union
    {
	const char *svalue;
	char *value;
    };
} JsonString;

typedef struct JsonArray
{
    PSC_Json base;
    PSC_List *values;
} JsonArray;

typedef struct JsonObject
{
    PSC_Json base;
    PSC_List *keys;
    PSC_Dictionary *values;
} JsonObject;

SOEXPORT PSC_Json *PSC_Json_createNull(void)
{
    PSC_Json *self = PSC_malloc(sizeof *self);
    self->type = PSC_JVT_NULL;
    return self;
}

SOEXPORT PSC_Json *PSC_Json_createBool(int value)
{
    PSC_Json *self = PSC_malloc(sizeof *self);
    self->type = value ? PSC_JVT_TRUE : PSC_JVT_FALSE;
    return self;
}

SOEXPORT PSC_Json *PSC_Json_createInteger(int64_t value)
{
    JsonInteger *self = PSC_malloc(sizeof *self);
    self->base.type = PSC_JVT_INTEGER;
    self->value = value;
    self->str = 0;
    return &self->base;
}

SOEXPORT PSC_Json *PSC_Json_createFloat(double value)
{
    JsonFloat *self = PSC_malloc(sizeof *self);
    self->base.type = PSC_JVT_FLOAT;
    self->value = value;
    self->str = 0;
    return &self->base;
}

SOEXPORT PSC_Json *PSC_Json_createString(const char *value, size_t len)
{
    if (!len && *value) len = strlen(value);
    JsonString *self = PSC_malloc(sizeof *self);
    self->base.type = PSC_JVT_STRING;
    self->owned = 1;
    self->len = len;
    self->value = PSC_malloc(len+1);
    memcpy(self->value, value, len);
    self->value[len] = 0;
    return &self->base;
}

SOEXPORT PSC_Json *PSC_Json_createStaticString(const char *value, size_t len)
{
    if (!len && *value) len = strlen(value);
    JsonString *self = PSC_malloc(sizeof *self);
    self->base.type = PSC_JVT_STRING;
    self->owned = 0;
    self->len = len;
    self->svalue = value;
    return &self->base;
}

SOEXPORT PSC_Json *PSC_Json_createOwnedString(char *value, size_t len)
{
    if (!len && *value) len = strlen(value);
    JsonString *self = PSC_malloc(sizeof *self);
    self->base.type = PSC_JVT_STRING;
    self->owned = 1;
    self->len = len;
    self->value = value;
    return &self->base;
}

SOEXPORT PSC_Json *PSC_Json_createArray(void)
{
    JsonArray *self = PSC_malloc(sizeof *self);
    self->base.type = PSC_JVT_ARRAY;
    self->values = 0;
    return &self->base;
}

SOEXPORT PSC_Json *PSC_Json_createObject(void)
{
    JsonObject *self = PSC_malloc(sizeof *self);
    self->base.type = PSC_JVT_OBJECT;
    self->keys = 0;
    self->values = 0;
    return &self->base;
}

static void jsonDeleter(void *obj)
{
    PSC_Json_destroy(obj);
}

SOEXPORT int PSC_Json_append(PSC_Json *self, PSC_Json *item)
{
    if (self->type != PSC_JVT_ARRAY) return -1;
    JsonArray *array = (JsonArray *)self;
    if (!array->values) array->values = PSC_List_create();
    PSC_List_append(array->values, item, jsonDeleter);
    return 0;
}

SOEXPORT int PSC_Json_setProperty(PSC_Json *self,
	const char *name, size_t namelen, PSC_Json *value)
{
    if (self->type != PSC_JVT_OBJECT) return -1;
    JsonObject *obj = (JsonObject *)self;
    JsonString *key = (JsonString *)PSC_Json_createString(name, namelen);
    if (!obj->values) obj->values = PSC_Dictionary_create(jsonDeleter);
    if (!PSC_Dictionary_get(obj->values, key->value, key->len))
    {
	if (!obj->keys) obj->keys = PSC_List_create();
	PSC_List_append(obj->keys, key, jsonDeleter);
    }
    PSC_Dictionary_set(obj->values, key->value, key->len, value, 0);
    return 0;
}

SOEXPORT PSC_JsonValueType PSC_Json_type(const PSC_Json *self)
{
    return self->type;
}

static const char *strVal(const PSC_Json *self)
{
    const JsonString *str = (const JsonString *)self;
    return str->owned ? str->value : str->svalue;
}

static int strIsNullOrEmpty(const PSC_Json *self)
{
    const char *str = strVal(self);
    return !str || !*str;
}

static double strToFloat(const char *str)
{
    return strtod(str, 0);
}

static int64_t strToInteger(const char *str)
{
    char *endp;
    long long val = strtoll(str, &endp, 10);
    if (*endp == '.' || *endp == 'e' || *endp == 'E')
    {
	return (int64_t)strToFloat(str);
    }
    return val;
}

static const char *numToString(const PSC_Json *self)
{
    JsonInteger *intv;
    JsonFloat *floatv;

    switch (self->type)
    {
	case PSC_JVT_INTEGER:
	    intv = (JsonInteger *)self;
	    if (!intv->str)
	    {
		intv->str = PSC_malloc(64);
		snprintf(intv->str, 64, "%" PRId64, intv->value);
	    }
	    return intv->str;

	case PSC_JVT_FLOAT:
	    floatv = (JsonFloat *)self;
	    if (!floatv->str)
	    {
		floatv->str = PSC_malloc(64);
		snprintf(floatv->str, 64, "%.15g", floatv->value);
	    }
	    return floatv->str;

	default:
	    return 0;
    }
}

SOEXPORT int PSC_Json_bool(const PSC_Json *self)
{
    switch (self->type)
    {
	case PSC_JVT_NULL:
	case PSC_JVT_FALSE:
	    return 0;

	case PSC_JVT_TRUE:
	    return 1;

	case PSC_JVT_INTEGER:
	    return !!((const JsonInteger *)self)->value;

	case PSC_JVT_FLOAT:
	    return !!((const JsonFloat *)self)->value;

	case PSC_JVT_STRING:
	    return !strIsNullOrEmpty(self);

	case PSC_JVT_ARRAY:
	    return !!((const JsonArray *)self)->values;

	case PSC_JVT_OBJECT:
	    return !!((const JsonObject *)self)->values;

	default:
	    return 0;
    }
}

SOEXPORT int64_t PSC_Json_integer(const PSC_Json *self)
{
    switch (self->type)
    {
	case PSC_JVT_NULL:
	case PSC_JVT_FALSE:
	    return 0;

	case PSC_JVT_TRUE:
	    return 1;

	case PSC_JVT_INTEGER:
	    return ((const JsonInteger *)self)->value;

	case PSC_JVT_FLOAT:
	    return (int64_t)((const JsonFloat *)self)->value;

	case PSC_JVT_STRING:
	    return strToInteger(strVal(self));

	default:
	    return PSC_Json_length(self);
    }
}

SOEXPORT double PSC_Json_float(const PSC_Json *self)
{
    switch (self->type)
    {
	case PSC_JVT_NULL:
	case PSC_JVT_FALSE:
	    return 0.;

	case PSC_JVT_TRUE:
	    return 1.;

	case PSC_JVT_INTEGER:
	    return (double)((const JsonInteger *)self)->value;

	case PSC_JVT_FLOAT:
	    return ((const JsonFloat *)self)->value;

	case PSC_JVT_STRING:
	    return strToFloat(strVal(self));

	default:
	    return PSC_Json_length(self);
    }
}

SOEXPORT const char *PSC_Json_string(const PSC_Json *self)
{
    switch (self->type)
    {
	case PSC_JVT_NULL:
	    return litNull;

	case PSC_JVT_FALSE:
	    return litFalse;

	case PSC_JVT_TRUE:
	    return litTrue;

	case PSC_JVT_INTEGER:
	case PSC_JVT_FLOAT:
	    return numToString(self);

	case PSC_JVT_STRING:
	    return strVal(self);

	case PSC_JVT_ARRAY:
	    return "[array]";

	case PSC_JVT_OBJECT:
	    return "[object]";

	default:
	    return litNull;
    }
}

SOEXPORT size_t PSC_Json_length(const PSC_Json *self)
{
    const JsonArray *arr;
    const JsonObject *obj;

    switch (self->type)
    {
	case PSC_JVT_NULL:
	    return 0;

	case PSC_JVT_FALSE:
	case PSC_JVT_TRUE:
	case PSC_JVT_INTEGER:
	case PSC_JVT_FLOAT:
	    return 1;

	case PSC_JVT_STRING:
	    return ((const JsonString *)self)->len;

	case PSC_JVT_ARRAY:
	    arr = (const JsonArray *)self;
	    if (!arr->values) return 0;
	    return PSC_List_size(arr->values);

	case PSC_JVT_OBJECT:
	    obj = (const JsonObject *)self;
	    if (!obj->values) return 0;
	    return PSC_Dictionary_count(obj->values);

	default:
	    return 0;
    }
}

SOEXPORT const PSC_Json *PSC_Json_itemAt(const PSC_Json *self, size_t i)
{
    const JsonArray *arr;
    const JsonObject *obj;
    switch (self->type)
    {
	case PSC_JVT_ARRAY:
	    arr = (const JsonArray *)self;
	    if (!arr->values) return 0;
	    return PSC_List_at(arr->values, i);

	case PSC_JVT_OBJECT:
	    obj = (const JsonObject *)self;
	    if (!obj->keys) return 0;
	    return PSC_List_at(obj->keys, i);

	default:
	    return 0;
    }
}

SOEXPORT const PSC_Json *PSC_Json_property(const PSC_Json *self,
	const char *name, size_t namelen)
{
    if (self->type != PSC_JVT_OBJECT) return 0;
    if (!namelen && *name) namelen = strlen(name);
    const JsonObject *obj = (const JsonObject *)self;
    if (!obj->values) return 0;
    return PSC_Dictionary_get(obj->values, name, namelen);
}

SOEXPORT void PSC_Json_destroy(PSC_Json *self)
{
    if (!self) return;
    JsonString *str;
    switch (self->type)
    {
	case PSC_JVT_INTEGER:
	    free(((JsonInteger *)self)->str);
	    break;

	case PSC_JVT_FLOAT:
	    free(((JsonFloat *)self)->str);
	    break;

	case PSC_JVT_STRING:
	    str = (JsonString *)self;
	    if (str->owned) free(str->value);
	    break;

	case PSC_JVT_ARRAY:
	    PSC_List_destroy(((JsonArray *)self)->values);
	    break;

	case PSC_JVT_OBJECT:
	    PSC_List_destroy(((JsonObject *)self)->keys);
	    PSC_Dictionary_destroy(((JsonObject *)self)->values);
	    break;

	default:
	    break;
    }
    free(self);
}

struct PSC_JsonSerializer
{
    union
    {
	const char *rdbuf;
	char *wrbuf;
    };
    size_t bufsz;
    size_t bufpos;
    int bufowned;
};

SOEXPORT PSC_JsonSerializer *PSC_JsonSerializer_create(
	const PSC_JsonSerializerOpts *opts)
{
    (void)opts;
    PSC_JsonSerializer *self = PSC_malloc(sizeof *self);
    return self;
}

static const char hex[] = "0123456789abcdefABCDEF";

static int appendChr(char **buf, size_t *bufpos, size_t *bufsz,
	int canresize, unsigned char c)
{
    if (!canresize && *bufpos + 1 >= *bufsz) return -1;
    if (*bufpos == *bufsz)
    {
	*bufsz += 1024;
	*buf = PSC_realloc(*buf, *bufsz);
    }
    (*buf)[(*bufpos)++] = (char)c;
    return 0;
}

static int serialize(PSC_JsonSerializer *self, const PSC_Json *value);

#define serappend(c) if (appendChr(&self->wrbuf, &self->bufpos, &self->bufsz, \
	    self->bufowned, (c)) < 0) return -1

static int serializePlainString(PSC_JsonSerializer *self,
	const char *p, size_t len)
{
    serappend('"');
    for (const char *e = p+len; p < e; ++p)
    {
	switch (*p)
	{
	    case '"':
	    case '\\':
		serappend('\\');
		serappend(*p);
		continue;

	    case '\b':
		serappend('\\');
		serappend('b');
		continue;

	    case '\f':
		serappend('\\');
		serappend('f');
		continue;

	    case '\n':
		serappend('\\');
		serappend('n');
		continue;

	    case '\r':
		serappend('\\');
		serappend('r');
		continue;

	    case '\t':
		serappend('\\');
		serappend('t');
		continue;

	    default:
		break;
	}
	if (*((const unsigned char *)p) < 0x20U)
	{
	    serappend('\\');
	    serappend('u');
	    serappend('0');
	    serappend('0');
	    serappend(hex[*p >> 4]);
	    serappend(hex[*p & 0xfU]);
	}
	else serappend(*p);
    }
    serappend('"');
    return 0;
}

static int serializeObject(PSC_JsonSerializer *self, const JsonObject *value)
{
    serappend('{');
    size_t len = PSC_Dictionary_count(value->values);
    for (size_t i = 0; i < len; ++i)
    {
	const JsonString *key = PSC_List_at(value->keys, i);
	const PSC_Json *val = PSC_Dictionary_get(value->values,
		key->value, key->len);
	if (i) serappend(',');
	if (serializePlainString(self, key->value, key->len) < 0) return -1;
	serappend(':');
	if (serialize(self, val) < 0) return -1;
    }
    serappend('}');
    return 0;
}

static int serializeArray(PSC_JsonSerializer *self, const JsonArray *value)
{
    serappend('[');
    size_t len = PSC_List_size(value->values);
    for (size_t i = 0; i < len; ++i)
    {
	if (i) serappend(',');
	if (serialize(self, PSC_List_at(value->values, i)) < 0) return -1;
    }
    serappend(']');
    return 0;
}

static int serializeString(PSC_JsonSerializer *self, const JsonString *value)
{
    return serializePlainString(self,
	    value->owned ? value->value : value->svalue, value->len);
}

static int serialize(PSC_JsonSerializer *self, const PSC_Json *value)
{
    const char *numstr;
    int len;

    switch (value->type)
    {
	case PSC_JVT_NULL:
	    for (int i = 0; i < (int) sizeof litNull - 1; ++i)
	    {
		serappend(litNull[i]);
	    }
	    break;

	case PSC_JVT_FALSE:
	    for (int i = 0; i < (int) sizeof litFalse - 1; ++i)
	    {
		serappend(litFalse[i]);
	    }
	    break;

	case PSC_JVT_TRUE:
	    for (int i = 0; i < (int) sizeof litTrue - 1; ++i)
	    {
		serappend(litTrue[i]);
	    }
	    break;

	case PSC_JVT_INTEGER:
	case PSC_JVT_FLOAT:
	    numstr = PSC_Json_string(value);
	    len = strlen(numstr);
	    for (int i = 0; i < len; ++i)
	    {
		serappend(numstr[i]);
	    }
	    break;

	case PSC_JVT_STRING:
	    if (serializeString(self, (const JsonString *)value) < 0)
	    {
		return -1;
	    }
	    break;

	case PSC_JVT_ARRAY:
	    if (serializeArray(self, (const JsonArray *)value) < 0)
	    {
		return -1;
	    }
	    break;

	case PSC_JVT_OBJECT:
	    if (serializeObject(self, (const JsonObject *)value) < 0)
	    {
		return -1;
	    }
	    break;

	default:
	    break;
    }
    return 0;
}

SOEXPORT size_t PSC_JsonSerializer_serializeTo(PSC_JsonSerializer *self,
	char *buf, size_t bufsz, const PSC_Json *value)
{
    self->wrbuf = buf;
    self->bufsz = bufsz;
    self->bufpos = 0;
    self->bufowned = 0;
    int rc = serialize(self, value);
    if (self->bufpos < self->bufsz) buf[self->bufpos++] = 0;
    return self->bufpos + (rc < 0);
}

SOEXPORT char *PSC_JsonSerializer_serialize(PSC_JsonSerializer *self,
	const PSC_Json *value)
{
    self->wrbuf = 0;
    self->bufsz = 0;
    self->bufpos = 0;
    self->bufowned = 1;
    serialize(self, value);
    appendChr(&self->wrbuf, &self->bufpos, &self->bufsz, 1, 0);
    return self->wrbuf;
}

static void skipWs(PSC_JsonSerializer *self)
{
    while (self->bufpos < self->bufsz && (
		self->rdbuf[self->bufpos] == ' ' ||
		self->rdbuf[self->bufpos] == '\t' ||
		self->rdbuf[self->bufpos] == '\r' ||
		self->rdbuf[self->bufpos] == '\n')) ++self->bufpos;
}

static int parse(PSC_JsonSerializer *self, PSC_Json **obj);

static int parsePlainString(PSC_JsonSerializer *self, char **str, size_t *len)
{
    size_t parsepos = 1;
    size_t bufsz = self->bufsz - self->bufpos;
    size_t parsedsz = 0;
    size_t parsedcapa = 0;
    char *parsed = 0;
    const char *buf = self->rdbuf + self->bufpos;
    uint32_t unichr;
    uint16_t bmpchr;
    int ok = 0;

    while (!ok && parsepos < bufsz)
    {
	switch (buf[parsepos])
	{
	    case '"':
		++parsepos;
		ok = 1;
		break;

	    case '\\':
		++parsepos;
		switch (buf[parsepos])
		{
		    case '"':
		    case '\\':
		    case '/':
			appendChr(&parsed, &parsedsz, &parsedcapa, 1,
				buf[parsepos++]);
			break;

		    case 'b':
			++parsepos;
			appendChr(&parsed, &parsedsz, &parsedcapa, 1, '\b');
			break;

		    case 'f':
			++parsepos;
			appendChr(&parsed, &parsedsz, &parsedcapa, 1, '\f');
			break;

		    case 'n':
			++parsepos;
			appendChr(&parsed, &parsedsz, &parsedcapa, 1, '\n');
			break;

		    case 'r':
			++parsepos;
			appendChr(&parsed, &parsedsz, &parsedcapa, 1, '\r');
			break;

		    case 't':
			++parsepos;
			appendChr(&parsed, &parsedsz, &parsedcapa, 1, '\t');
			break;

		    case 'u':
			bmpchr = 0;
			for (int i = 1; i < 5; ++i)
			{
			    if (!buf[parsepos+i]) goto fail;
			    char *pos;
			    if (!(pos = strchr(hex, buf[parsepos+i])))
			    {
				goto fail;
			    }
			    unsigned nibble = pos - hex;
			    if (nibble > 15) nibble -= 6;
			    bmpchr <<= 4;
			    bmpchr |= nibble;
			}
			unichr = bmpchr;
			parsepos += 5;
			if (buf[parsepos] == '\\' && buf[parsepos+1] == 'u'
				&& ((unichr & 0xfc00U) == 0xd800U))
			{
			    parsepos += 2;
			    bmpchr = 0;
			    for (int i = 0; i < 4; ++i)
			    {
				if (!buf[parsepos+i]) goto fail;
				char *pos;
				if (!(pos = strchr(hex, buf[parsepos+i])))
				{
				    goto fail;
				}
				unsigned nibble = pos - hex;
				if (nibble > 15) nibble -= 6;
				bmpchr <<= 4;
				bmpchr |= nibble;
			    }
			    if ((bmpchr & 0xfc00U) != 0xdc00U) goto fail;
			    unichr = (((unichr & 0x3ffU) << 10) |
				    (bmpchr & 0x3ffU)) + 0x10000U;
			    parsepos += 4;
			}
			if (unichr < 0x80U)
			{
			    appendChr(&parsed, &parsedsz, &parsedcapa, 1,
				    unichr);
			    break;
			}
			unsigned char b[] = {
			    unichr & 0xffU,
			    unichr >> 8 & 0xffU,
			    unichr >> 16 & 0xffU
			};
			if (unichr < 0x800U)
			{
			    appendChr(&parsed, &parsedsz, &parsedcapa, 1,
				    0xc0U | (b[1] << 2) | (b[0] >> 6));
			    goto follow2;
			}
			if (unichr < 0x10000U)
			{
			    appendChr(&parsed, &parsedsz, &parsedcapa, 1,
				    0xe0U | (b[1] >> 4));
			    goto follow1;
			}
			appendChr(&parsed, &parsedsz, &parsedcapa, 1,
				0xf0U | (b[2] >> 2));
			appendChr(&parsed, &parsedsz, &parsedcapa, 1,
				0x80U | ((b[2] << 4) & 0x3fU) | (b[1] >> 4));
		    follow1:
			appendChr(&parsed, &parsedsz, &parsedcapa, 1,
				0x80U | ((b[1] << 2) & 0x3fU) | (b[0] >> 6));
		    follow2:
			appendChr(&parsed, &parsedsz, &parsedcapa, 1,
				0x80U | (b[0] & 0x3fU));
			break;

		    default:
			goto fail;
		}
		break;

	    default:
		appendChr(&parsed, &parsedsz, &parsedcapa, 1, buf[parsepos++]);
		break;
	}
    }
    if (!ok) goto fail;

    *len = parsedsz;
    appendChr(&parsed, &parsedsz, &parsedcapa, 1, 0);
    if (parsedcapa > parsedsz) parsed = PSC_realloc(parsed, parsedsz);
    *str = parsed;
    self->bufpos += parsepos;
    return 0;

fail:
    free(parsed);
    return -1;
}

static int parseObject(PSC_JsonSerializer *self, PSC_Json **obj)
{
    PSC_Json *jobj = PSC_Json_createObject();
    char *key = 0;
    size_t keylen = 0;
    PSC_Json *value = 0;
    ++self->bufpos;
    for (;;)
    {
	skipWs(self);
	if (self->rdbuf[self->bufpos] == '}')
	{
	    ++self->bufpos;
	    goto succeed;
	}
	if (parsePlainString(self, &key, &keylen) < 0) goto fail;
	skipWs(self);
	if (self->rdbuf[self->bufpos] != ':') goto fail;
	++self->bufpos;
	skipWs(self);
	if (parse(self, &value) < 0) goto fail;
	PSC_Json_setProperty(jobj, key, keylen, value);
	free(key);
	key = 0;
	skipWs(self);
	if (self->rdbuf[self->bufpos] == '}')
	{
	    ++self->bufpos;
	    goto succeed;
	}
	if (self->rdbuf[self->bufpos] != ',') goto fail;
	++self->bufpos;
    }

succeed:
    *obj = jobj;
    return 0;

fail:
    free(key);
    PSC_Json_destroy(jobj);
    return -1;
}

static int parseArray(PSC_JsonSerializer *self, PSC_Json **obj)
{
    PSC_Json *arr = PSC_Json_createArray();
    PSC_Json *item = 0;
    ++self->bufpos;
    for (;;)
    {
	skipWs(self);
	if (self->rdbuf[self->bufpos] == ']')
	{
	    ++self->bufpos;
	    goto succeed;
	}
	if (parse(self, &item) < 0) goto fail;
	PSC_Json_append(arr, item);
	skipWs(self);
	if (self->rdbuf[self->bufpos] == ']')
	{
	    ++self->bufpos;
	    goto succeed;
	}
	if (self->rdbuf[self->bufpos] != ',') goto fail;
	++self->bufpos;
    }

succeed:
    *obj = arr;
    return 0;

fail:
    PSC_Json_destroy(arr);
    return -1;
}

static int parseString(PSC_JsonSerializer *self, PSC_Json **obj)
{
    char *str;
    size_t len;
    int rc = parsePlainString(self, &str, &len);
    if (rc == 0) *obj = PSC_Json_createOwnedString(str, len);
    return rc;
}

static int parseNumber(PSC_JsonSerializer *self, PSC_Json **obj)
{
    char *endp;
    long long intval = strtoll(self->rdbuf + self->bufpos, &endp, 10);
    if (*endp == '.' || *endp == 'e' || *endp == 'E')
    {
	double floatval = strtod(self->rdbuf + self->bufpos, &endp);
	if (endp == self->rdbuf + self->bufpos) return -1;
	self->bufpos = endp - self->rdbuf;
	*obj = PSC_Json_createFloat(floatval);
	return 0;
    }
    if (endp == self->rdbuf + self->bufpos) return -1;
    self->bufpos = endp - self->rdbuf;
    *obj = PSC_Json_createInteger(intval);
    return 0;
}

static int parse(PSC_JsonSerializer *self, PSC_Json **obj)
{
    skipWs(self);
    if (self->bufpos == self->bufsz) return 0;
    switch (self->rdbuf[self->bufpos])
    {
	case '{':
	    return parseObject(self, obj);

	case '[':
	    return parseArray(self, obj);

	case '"':
	    return parseString(self, obj);

	case '+':
	case '-':
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	    return parseNumber(self, obj);

	default:
	    break;
    }
    if (self->bufsz - self->bufpos >= sizeof litNull - 1 &&
	    !strncmp(self->rdbuf + self->bufpos, litNull, sizeof litNull - 1))
    {
	self->bufpos += sizeof litNull - 1;
	*obj = PSC_Json_createNull();
	return 0;
    }
    if (self->bufsz - self->bufpos >= sizeof litFalse - 1 &&
	    !strncmp(self->rdbuf + self->bufpos, litFalse,
		sizeof litFalse - 1))
    {
	self->bufpos += sizeof litFalse - 1;
	*obj = PSC_Json_createBool(0);
	return 0;
    }
    if (self->bufsz - self->bufpos >= sizeof litTrue - 1 &&
	    !strncmp(self->rdbuf + self->bufpos, litTrue, sizeof litTrue - 1))
    {
	self->bufpos += sizeof litTrue - 1;
	*obj = PSC_Json_createBool(1);
	return 0;
    }
    return -1;
}

SOEXPORT PSC_Json *PSC_JsonSerializer_deserialize(PSC_JsonSerializer *self,
	const char *json)
{
    self->rdbuf = json;
    self->bufsz = strlen(json);
    self->bufpos = 0;
    self->bufowned = 0;
    PSC_Json *obj = 0;
    parse(self, &obj);
    skipWs(self);
    if (self->bufpos != self->bufsz)
    {
	PSC_Json_destroy(obj);
	obj = 0;
    }
    return obj;
}

