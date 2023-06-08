#include "util.h"

#include <poser/core/config.h>
#include <poser/core/hashtable.h>
#include <poser/core/list.h>
#include <poser/core/service.h>
#include <stdlib.h>
#include <string.h>

struct PSC_ConfigSection
{
    char *name;
    PSC_List *elements;
};

typedef enum ElemType
{
    ET_STRING,
    ET_INTEGER,
    ET_FLOAT,
    ET_SECTION,
    ET_LIST,
    ET_SECTIONLIST
} ElemType;

struct PSC_ConfigElement
{
    PSC_ConfigElementCallback parser;
    PSC_ConfigElementCallback validator;
    char *name;
    char *description;
    union
    {
	char *defString;
	long defInteger;
	double defFloat;
	PSC_ConfigSection *section;
	PSC_ConfigElement *element;
    };
    ElemType type;
    int flag;
};

struct PSC_ConfigParserCtx
{
    char *string;
    char *error;
    union
    {
	long integerVal;
	double floatVal;
    };
    ElemType type;
    int success;
};

struct PSC_ConfigParser
{
    PSC_ConfigSection *root;
    PSC_List *parsers;
};

struct PSC_Config
{
    PSC_HashTable *values;
};

SOEXPORT PSC_ConfigSection *PSC_ConfigSection_create(const char *name)
{
    PSC_ConfigSection *self = PSC_malloc(sizeof *self);
    self->name = PSC_copystr(name);
    self->elements = PSC_List_create();
    return self;
}

static void deleteElement(void *element)
{
    PSC_ConfigElement_destroy(element);
}

SOEXPORT void PSC_ConfigSection_add(PSC_ConfigSection *self,
	PSC_ConfigElement *element)
{
    PSC_List_append(self->elements, element, deleteElement);
}

SOEXPORT void PSC_ConfigSection_destroy(PSC_ConfigSection *self)
{
    if (!self) return;
    PSC_List_destroy(self->elements);
    free(self->name);
    free(self);
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createString(const char *name,
	const char *defval)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->name = PSC_copystr(name);
    self->defString = PSC_copystr(defval);
    self->type = ET_STRING;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createInteger(const char *name,
	long defval)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->name = PSC_copystr(name);
    self->defInteger = defval;
    self->type = ET_INTEGER;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createFloat(const char *name,
	double defval)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->name = PSC_copystr(name);
    self->defFloat = defval;
    self->type = ET_FLOAT;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createSection(
	PSC_ConfigSection *section)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->section = section;
    self->type = ET_SECTION;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createList(
	PSC_ConfigElement *element)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->element = element;
    self->type = ET_LIST;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createSectionList(
	PSC_ConfigSection *section)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->section = section;
    self->type = ET_SECTIONLIST;
    return self;
}

SOEXPORT void PSC_ConfigElement_setFlag(PSC_ConfigElement *self, int flag)
{
    self->flag = flag;
}

SOEXPORT void PSC_ConfigElement_describe(PSC_ConfigElement *self,
	const char *description)
{
    free(self->description);
    self->description = PSC_copystr(description);
}

SOEXPORT void PSC_ConfigElement_parse(PSC_ConfigElement *self,
	PSC_ConfigElementCallback parser)
{
    self->parser = parser;
}

SOEXPORT void PSC_ConfigElement_validate(PSC_ConfigElement *self,
	PSC_ConfigElementCallback validator)
{
    self->validator = validator;
}

SOEXPORT void PSC_ConfigElement_destroy(PSC_ConfigElement *self)
{
    if (!self) return;
    free(self->name);
    free(self->description);
    switch (self->type)
    {
	case ET_STRING:
	    free(self->defString);
	    break;
	
	case ET_SECTION:
	case ET_SECTIONLIST:
	    PSC_ConfigSection_destroy(self->section);
	    break;

	case ET_LIST:
	    PSC_ConfigElement_destroy(self->element);
	    break;
	
	default:
	    ;
    }
    free(self);
}

SOEXPORT const char *PSC_ConfigParserCtx_string(
	const PSC_ConfigParserCtx *self)
{
    return self->string;
}

SOEXPORT long PSC_ConfigParserCtx_integer(const PSC_ConfigParserCtx *self)
{
    if (self->type != ET_INTEGER)
    {
	PSC_Service_panic("Tried to get integer value from configuration "
		"element that is not an integer");
    }
    return self->integerVal;
}

SOEXPORT double PSC_ConfigParserCtx_float(const PSC_ConfigParserCtx *self)
{
    if (self->type != ET_FLOAT)
    {
	PSC_Service_panic("Tried to get float value from configuration "
		"element that is not a float");
    }
    return self->floatVal;
}

SOEXPORT void PSC_ConfigParserCtx_setString(PSC_ConfigParserCtx *self,
	const char *val)
{
    if (self->type != ET_STRING)
    {
	PSC_Service_panic("Tried to set string value on configuration "
		"element that is not a string");
    }
    free(self->string);
    self->string = PSC_copystr(val);
}

SOEXPORT void PSC_ConfigParserCtx_setInteger(PSC_ConfigParserCtx *self,
	long val)
{
    if (self->type != ET_INTEGER)
    {
	PSC_Service_panic("Tried to set integer value on configuration "
		"element that is not an integer");
    }
    self->integerVal = val;
}

SOEXPORT void PSC_ConfigParserCtx_setFloat(PSC_ConfigParserCtx *self,
	double val)
{
    if (self->type != ET_FLOAT)
    {
	PSC_Service_panic("Tried to set float value on configuration "
		"element that is not a float");
    }
    self->floatVal = val;
}

SOEXPORT void PSC_ConfigParserCtx_fail(PSC_ConfigParserCtx *self,
	const char *error)
{
    free(self->error);
    self->error = PSC_copystr(error);
    self->success = 0;
}

