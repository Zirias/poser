#define _DEFAULT_SOURCE

#include "util.h"

#include <errno.h>
#include <poser/core/config.h>
#include <poser/core/hashtable.h>
#include <poser/core/list.h>
#include <poser/core/service.h>
#include <poser/core/stringbuilder.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define DEFLINELEN 80
#define MINLINELEN 24
#define MAXLINELEN 512

static size_t currlinelen = DEFLINELEN;

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
    ET_BOOL,
    ET_SECTION,
    ET_LIST,
    ET_SECTIONLIST
} ElemType;

struct PSC_ConfigElement
{
    PSC_ConfigElementCallback parser;
    PSC_ConfigElementCallback validator;
    char *name;
    char *argname;
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
    int required;
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

typedef struct ArgData
{
    char *defname;
    char **argv;
    int argc;
} ArgData;

typedef enum ParserType
{
    PT_ARGS,
    PT_FILE
} ParserType;

typedef struct ConcreteParser
{
    void *parserData;
    ParserType type;
} ConcreteParser;

struct PSC_ConfigParser
{
    const PSC_ConfigSection *root;
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
	const char *defval, int required)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->name = PSC_copystr(name);
    self->defString = PSC_copystr(defval);
    self->type = ET_STRING;
    self->required = required;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createInteger(const char *name,
	long defval, int required)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->name = PSC_copystr(name);
    self->defInteger = defval;
    self->type = ET_INTEGER;
    self->required = required;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createFloat(const char *name,
	double defval, int required)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->name = PSC_copystr(name);
    self->defFloat = defval;
    self->type = ET_FLOAT;
    self->required = required;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createBool(const char *name)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->name = PSC_copystr(name);
    self->type = ET_BOOL;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createSection(
	PSC_ConfigSection *section, int required)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->section = section;
    self->type = ET_SECTION;
    self->required = required;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createList(
	PSC_ConfigElement *element, int required)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->element = element;
    self->type = ET_LIST;
    self->required = required;
    return self;
}

SOEXPORT PSC_ConfigElement *PSC_ConfigElement_createSectionList(
	PSC_ConfigSection *section, int required)
{
    PSC_ConfigElement *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->section = section;
    self->type = ET_SECTIONLIST;
    self->required = required;
    return self;
}

SOEXPORT void PSC_ConfigElement_argInfo(PSC_ConfigElement *self, int flag,
	const char *argname)
{
    self->flag = flag;
    free(self->argname);
    self->argname = PSC_copystr(argname);
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
    free(self->argname);
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

SOEXPORT PSC_ConfigParser *PSC_ConfigParser_create(
	const PSC_ConfigSection *root)
{
    PSC_ConfigParser *self = PSC_malloc(sizeof *self);
    self->root = root;
    self->parsers = PSC_List_create();
    return self;
}

static void deleteconcreteparser(void *ptr)
{
    if (!ptr) return;
    ConcreteParser *p = ptr;
    if (p->type == PT_ARGS)
    {
	ArgData *ad = p->parserData;
	free(ad->defname);
	free(ad);
    }
    free(p);
}

SOEXPORT void PSC_ConfigParser_addArgs(PSC_ConfigParser *self,
	const char *defname, int argc, char **argv)
{
    ConcreteParser *p = PSC_malloc(sizeof *p);
    ArgData *ad = PSC_malloc(sizeof *ad);
    ad->defname = PSC_copystr(defname);
    ad->argv = argv;
    ad->argc = argc;
    p->parserData = ad;
    p->type = PT_ARGS;
    PSC_List_append(self->parsers, p, deleteconcreteparser);
}

static int compareflags(const void *a, const void *b)
{
    const char *l = a;
    const char *r = b;
    return (int)*l - (int)*r;
}

static int compareelements(const void *a, const void *b)
{
    const PSC_ConfigElement **l = (void *)a;
    const PSC_ConfigElement **r = (void *)b;
    return (int)(*l)->flag - (int)(*r)->flag;
}

static int compareelemnames(const void *a, const void *b)
{
    const PSC_ConfigElement **l = (void *)a;
    const PSC_ConfigElement **r = (void *)b;
    return strcmp((*l)->name, (*r)->name);
}

static void setoutputwidth(FILE *out)
{
    int outfd = fileno(out);
    if (!isatty(outfd))
    {
	currlinelen = DEFLINELEN;
	return;
    }
    const char *envcols = getenv("COLUMNS");
    if (envcols)
    {
	errno = 0;
	char *endptr = 0;
	long val = strtol(envcols, &endptr, 10);
	if (!*endptr && !errno)
	{
	    if (val < MINLINELEN) currlinelen = MINLINELEN;
	    else if (val > MAXLINELEN) currlinelen = MAXLINELEN;
	    else currlinelen = (size_t)val;
	    return;
	}
    }
    struct winsize sz;
    if (ioctl(outfd, TIOCGWINSZ, &sz) >= 0)
    {
	if (sz.ws_col < MINLINELEN) currlinelen = MINLINELEN;
	else if (sz.ws_col > MAXLINELEN) currlinelen = MAXLINELEN;
	else currlinelen = (size_t)sz.ws_col;
	return;
    }
    currlinelen = DEFLINELEN;
}

static int printformatted(FILE *out, PSC_StringBuilder *str,
	int indentfirst, int indentrest)
{
    char line[MAXLINELEN];
    int linepos = 0;
    int rc = -1;
    
    const char *cstr = PSC_StringBuilder_str(str);
    int indent = indentfirst;

    if (!*cstr) goto done;

    for (;;)
    {
	if (indent)
	{
	    memset(line, ' ', indent);
	    linepos = indent;
	}
	else linepos = 0;
	size_t toklen = strcspn(cstr, " \n");
	if (toklen == 0)
	{
	    if (*cstr == '\n')
	    {
		if (fprintf(out, "\n") <= 0) goto done;
		++cstr;
		continue;
	    }
	    else break;
	}
	size_t needed = toklen;
	while (linepos + needed < currlinelen)
	{
	    if (linepos > indent) line[linepos++] = ' ';
	    for (size_t i = 0; i < toklen; ++i)
	    {
		line[linepos++] = *cstr == '\t' ? ' ' : *cstr;
		++cstr;
	    }
	    while (*cstr == ' ') ++cstr;
	    if (!*cstr || *cstr == '\n') break;
	    toklen = strcspn(cstr, " \n");
	    needed = toklen + 1;
	}
	if (linepos == indent)
	{
	    char *longtok = PSC_malloc(toklen+indent+1);
	    if (indent) memset(longtok, ' ', indent);
	    char *ltp = longtok+indent;
	    for (size_t i = 0; i < toklen; ++i)
	    {
		*ltp++ = *cstr == '\t' ? ' ' : *cstr;
		++cstr;
	    }
	    *ltp = 0;
	    int ltrc = fprintf(out, "%s\n", longtok);
	    free(longtok);
	    if (ltrc <= 0) goto done;
	    while (*cstr == ' ') ++cstr;
	    if (!*cstr) break;
	}
	else if (linepos > indent)
	{
	    line[linepos] = 0;
	    if (fprintf(out, "%s\n", line) <= 0) goto done;
	}
	indent = indentrest;
    }

    rc = 0;
done:

    PSC_StringBuilder_destroy(str);
    return rc;
}

static const char *argstr(const PSC_ConfigElement *e)
{
    const char *result = e->argname;
    if (!result && e->flag < 0) switch (e->type)
    {
	case ET_LIST:
	    result = e->element->name;
	    break;

	case ET_SECTION:
	case ET_SECTIONLIST:
	    result = e->section->name;
	    break;

	default:
	    result = e->name;
    }
    if (!result) switch (e->type)
    {
	case ET_STRING:
	    result = "string";
	    break;

	case ET_INTEGER:
	    result = "integer";
	    break;

	case ET_FLOAT:
	    result = "float";
	    break;

	case ET_BOOL:
	    result = e->name;
	    break;

	case ET_LIST:
	    result = argstr(e->element);
	    break;

	case ET_SECTION:
	case ET_SECTIONLIST:
	    result = "section";
	    break;

	default:
	    ;
    }
    if (!result) result = "(null)";
    return result;
}

SOEXPORT int PSC_ConfigParser_usage(const PSC_ConfigParser *self, FILE *out)
{
    const char *svname = 0;
    char *boolflags = 0;
    PSC_ConfigElement **optflags = 0;
    PSC_ConfigElement **reqposargs = 0;
    PSC_ConfigElement **optposargs = 0;
    PSC_ListIterator *i = 0;
    PSC_StringBuilder *s = 0;
    int nboolflags = 0;
    int noptflags = 0;
    int nreqposargs = 0;
    int noptposargs = 0;

    for (i = PSC_List_iterator(self->parsers); PSC_ListIterator_moveNext(i);)
    {
	ConcreteParser *p = PSC_ListIterator_current(i);
	if (p->type == PT_ARGS)
	{
	    ArgData *ad = p->parserData;
	    if (ad->argc > 0) svname = ad->argv[0];
	    else svname = ad->defname;
	    break;
	}
    }
    PSC_ListIterator_destroy(i);

    if (!svname) PSC_Service_panic(
	    "Can't print usage without a configured args parser");

    size_t elemcount = PSC_List_size(self->root->elements);
    boolflags = PSC_malloc(elemcount);
    optflags = PSC_malloc(elemcount * sizeof *optflags);
    reqposargs = PSC_malloc(elemcount * sizeof *reqposargs);
    optposargs = PSC_malloc(elemcount * sizeof *optposargs);
    for (i = PSC_List_iterator(self->root->elements);
	    PSC_ListIterator_moveNext(i);)
    {
	PSC_ConfigElement *e = PSC_ListIterator_current(i);
	if (e->type == ET_BOOL && e->flag > 0)
	{
	    boolflags[nboolflags++] = e->flag;
	}
	else if (e->type != ET_BOOL)
	{
	    if (e->flag > 0)
	    {
		optflags[noptflags++] = e;
	    }
	    else if (e->flag < 0)
	    {
		if (e->required) reqposargs[nreqposargs++] = e;
		else optposargs[noptposargs++] = e;
	    }
	}
    }
    PSC_ListIterator_destroy(i);
    boolflags[nboolflags] = 0;
    if (nboolflags) qsort(boolflags, nboolflags, 1, compareflags);
    if (noptflags) qsort(optflags, noptflags,
	    sizeof *optflags, compareelements);

    s = PSC_StringBuilder_create();
    PSC_StringBuilder_append(s, "Usage: ");
    PSC_StringBuilder_append(s, svname);
    if (nboolflags)
    {
	PSC_StringBuilder_append(s, " [-");
	PSC_StringBuilder_append(s, boolflags);
	PSC_StringBuilder_appendChar(s, ']');
    }
    for (int j = 0; j < noptflags; ++j)
    {
	PSC_ConfigElement *e = optflags[j];
	int req = e->required;
	PSC_StringBuilder_append(s, req ? " -" : " [-");
	PSC_StringBuilder_appendChar(s, e->flag);
	PSC_StringBuilder_appendChar(s, '\t');
	PSC_StringBuilder_append(s, argstr(e));
	if (e->type == ET_LIST || e->type == ET_SECTIONLIST)
	{
	    PSC_StringBuilder_append(s, "\t[-");
	    PSC_StringBuilder_appendChar(s, e->flag);
	    PSC_StringBuilder_append(s, "\t...]");
	}
	if (!req) PSC_StringBuilder_appendChar(s, ']');
    }
    for (int j = 0; j < nreqposargs; ++j)
    {
	PSC_ConfigElement *e = reqposargs[j];
	PSC_StringBuilder_appendChar(s, ' ');
	PSC_StringBuilder_append(s, argstr(e));
	if (e->type == ET_LIST || e->type == ET_SECTIONLIST)
	{
	    PSC_StringBuilder_append(s, " [");
	    PSC_StringBuilder_append(s, argstr(e));
	    PSC_StringBuilder_append(s, "\t...]");
	}
    }
    for (int j = 0; j < noptposargs; ++j)
    {
	PSC_ConfigElement *e = optposargs[j];
	PSC_StringBuilder_append(s, " [");
	PSC_StringBuilder_append(s, argstr(e));
	if (e->type == ET_LIST || e->type == ET_SECTIONLIST)
	{
	    PSC_StringBuilder_append(s, "\t[");
	    PSC_StringBuilder_append(s, argstr(e));
	    PSC_StringBuilder_append(s, "\t...]");
	}
	PSC_StringBuilder_appendChar(s, ']');
    }

    free(optposargs);
    free(reqposargs);
    free(optflags);
    free(boolflags);

    setoutputwidth(out);
    return printformatted(out, s, 0, 8);
}

static void setdescstr(PSC_StringBuilder *s, PSC_ConfigElement *e)
{
    if (e->description)
    {
	PSC_StringBuilder_append(s, e->description);
	return;
    }
    if (e->type == ET_BOOL) PSC_StringBuilder_append(s, "Enable ");
    else PSC_StringBuilder_append(s, "Set ");
    PSC_StringBuilder_append(s, argstr(e));
}

SOEXPORT int PSC_ConfigParser_help(const PSC_ConfigParser *self, FILE *out)
{
    const char *svname = 0;
    PSC_ConfigElement **shortflags = 0;
    PSC_ConfigElement **longflags = 0;
    PSC_ConfigElement **reqposargs = 0;
    PSC_ConfigElement **optposargs = 0;
    PSC_ListIterator *i = 0;
    PSC_StringBuilder *s = 0;
    int nshortflags = 0;
    int nlongflags = 0;
    int nreqposargs = 0;
    int noptposargs = 0;
    int rc = -1;

    for (i = PSC_List_iterator(self->parsers); PSC_ListIterator_moveNext(i);)
    {
	ConcreteParser *p = PSC_ListIterator_current(i);
	if (p->type == PT_ARGS)
	{
	    ArgData *ad = p->parserData;
	    if (ad->argc > 0) svname = ad->argv[0];
	    else svname = ad->defname;
	    break;
	}
    }
    PSC_ListIterator_destroy(i);

    if (!svname) PSC_Service_panic(
	    "Can't print help without a configured args parser");

    size_t elemcount = PSC_List_size(self->root->elements);
    shortflags = PSC_malloc(elemcount * sizeof *shortflags);
    longflags = PSC_malloc(elemcount * sizeof *longflags);
    reqposargs = PSC_malloc(elemcount * sizeof *reqposargs);
    optposargs = PSC_malloc(elemcount * sizeof *optposargs);
    for (i = PSC_List_iterator(self->root->elements);
	    PSC_ListIterator_moveNext(i);)
    {
	PSC_ConfigElement *e = PSC_ListIterator_current(i);
	if (e->flag > 0)
	{
	    shortflags[nshortflags++] = e;
	}
	else if (e->flag == 0)
	{
	    if (e->name) longflags[nlongflags++] = e;
	}
	else
	{
	    if (e->required) reqposargs[nreqposargs++] = e;
	    else optposargs[noptposargs++] = e;
	}
    }
    PSC_ListIterator_destroy(i);
    if (nshortflags) qsort(shortflags, nshortflags,
	    sizeof *shortflags, compareelements);
    if (nlongflags) qsort(longflags, nlongflags,
	    sizeof *longflags, compareelemnames);

    if (PSC_ConfigParser_usage(self, out) < 0) goto done;

    for (int j = 0; j < nshortflags; ++j)
    {
	PSC_ConfigElement *e = shortflags[j];
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_append(s, "\n-");
	PSC_StringBuilder_appendChar(s, e->flag);
	if (e->type != ET_BOOL)
	{
	    PSC_StringBuilder_appendChar(s, '\t');
	    PSC_StringBuilder_append(s, argstr(e));
	}
	if (e->name)
	{
	    PSC_StringBuilder_append(s, ",\t--");
	    PSC_StringBuilder_append(s, e->name);
	    if (e->type != ET_BOOL)
	    {
		PSC_StringBuilder_appendChar(s, '=');
		PSC_StringBuilder_append(s, argstr(e));
	    }
	}
	if (printformatted(out, s, 4, 4) < 0) goto done;
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	if (printformatted(out, s, 8, 8) < 0) goto done;
    }

    for (int j = 0; j < nlongflags; ++j)
    {
	PSC_ConfigElement *e = longflags[j];
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_append(s, "\n--");
	PSC_StringBuilder_append(s, e->name);
	if (e->type != ET_BOOL)
	{
	    PSC_StringBuilder_appendChar(s, '=');
	    PSC_StringBuilder_append(s, argstr(e));
	}
	if (printformatted(out, s, 4, 4) < 0) goto done;
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	if (printformatted(out, s, 8, 8) < 0) goto done;
    }

    for (int j = 0; j < nreqposargs; ++j)
    {
	PSC_ConfigElement *e = reqposargs[j];
	const char *str = argstr(e);
	if (fprintf(out, "\n%*s\n", (int)strlen(str)+4, str) <= 0) goto done;
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	if (printformatted(out, s, 8, 8) < 0) goto done;
    }

    for (int j = 0; j < noptposargs; ++j)
    {
	PSC_ConfigElement *e = optposargs[j];
	const char *str = argstr(e);
	if (fprintf(out, "\n%*s\n", (int)strlen(str)+4, str) <= 0) goto done;
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	if (printformatted(out, s, 8, 8) < 0) goto done;
    }

    rc = 0;

done:
    free(optposargs);
    free(reqposargs);
    free(longflags);
    free(shortflags);

    return rc;
}

SOEXPORT void PSC_ConfigParser_destroy(PSC_ConfigParser *self)
{
    if (!self) return;
    PSC_List_destroy(self->parsers);
    free(self);
}

