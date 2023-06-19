#define _DEFAULT_SOURCE

#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <poser/core/config.h>
#include <poser/core/hashtable.h>
#include <poser/core/list.h>
#include <poser/core/queue.h>
#include <poser/core/service.h>
#include <poser/core/stringbuilder.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define DEFLINELEN 80
#define MINLINELEN 24
#define MAXLINELEN 512
#define MAXLINES 512

static size_t currlinelen = DEFLINELEN;
static unsigned short currlines = 0;

static pid_t pagerpid;
static volatile sig_atomic_t pagerexited;

static int boolval = 1;

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

typedef struct ArgSectItem
{
    char *key;
    char *val;
} ArgSectItem;

typedef struct ArgContext
{
    PSC_Queue *needsarg;
    PSC_Queue *positional;
    PSC_HashTable *flags;
    PSC_HashTable *onceflags;
    PSC_HashTable *seenflags;
    PSC_ConfigElement *listposarg;
} ArgContext;

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
    int autopage;
};

struct PSC_Config
{
    PSC_HashTable *values;
};

static void handlepagersig(int sig)
{
    if (sig != SIGCHLD) return;
    int st;
    if (waitpid(pagerpid, &st, WNOHANG) == pagerpid)
    {
	if (WIFEXITED(st) || WIFSIGNALED(st))
	{
	    pagerexited = 1;
	}
    }
}

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

static void deleteList(void *list)
{
    PSC_List_destroy(list);
}

static void deleteConfig(void *config)
{
    PSC_Config_destroy(config);
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
    self->autopage = 0;
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

SOEXPORT void PSC_ConfigParser_autoPage(PSC_ConfigParser *self)
{
    self->autopage = 1;
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

static int compareanyflag(const void *a, const void *b)
{
    char lf[2] = {0};
    char rf[2] = {0};
    const char *lfstr;
    const char *rfstr;
    const PSC_ConfigElement **l = (void *)a;
    if ((*lf = (*l)->flag) > 0) lfstr = lf;
    else lfstr = (*l)->name;
    const PSC_ConfigElement **r = (void *)b;
    if ((*rf = (*r)->flag) > 0) rfstr = rf;
    else rfstr = (*r)->name;
    return strcmp(lfstr, rfstr);
}

static void setoutputdims(FILE *out)
{
    int outfd = fileno(out);
    if (!isatty(outfd))
    {
	currlinelen = DEFLINELEN;
	currlines = 0;
	return;
    }
    const char *envlines = getenv("LINES");
    if (envlines)
    {
	errno = 0;
	char *endptr = 0;
	long val = strtol(envlines, &endptr, 10);
	if (endptr != envlines && !*endptr && !errno)
	{
	    currlines = val > MAXLINES ? MAXLINES : (unsigned short)val;
	}
    }
    const char *envcols = getenv("COLUMNS");
    if (envcols)
    {
	errno = 0;
	char *endptr = 0;
	long val = strtol(envcols, &endptr, 10);
	if (endptr != envcols && !*endptr && !errno)
	{
	    if (val < MINLINELEN) currlinelen = MINLINELEN;
	    else if (val > MAXLINELEN) currlinelen = MAXLINELEN;
	    else currlinelen = (size_t)val;
	    return;
	}
    }
#if defined(TIOCGWINSZ)
    struct winsize sz;
    if (ioctl(outfd, TIOCGWINSZ, &sz) >= 0)
    {
	if (sz.ws_col < MINLINELEN) currlinelen = MINLINELEN;
	else if (sz.ws_col > MAXLINELEN) currlinelen = MAXLINELEN;
	else currlinelen = (size_t)sz.ws_col;
	if (!currlines) currlines = sz.ws_row > MAXLINES
	    ? MAXLINES : (unsigned short) sz.ws_row;
	return;
    }
#elif defined(TIOCGSIZE)
    struct ttysize sz;
    if (ioctl(outfd, TIOCGSIZE, &sz) >= 0)
    {
	if (sz.ts_cols < MINLINELEN) currlinelen = MINLINELEN;
	else if (sz.ts_cols > MAXLINELEN) currlinelen = MAXLINELEN;
	else currlinelen = (size_t)sz.ts_cols;
	if (!currlines) currlines = sz.ts_lines > MAXLINES
	    ? MAXLINES : (unsigned short) ts_lines;
	return;
    }
#endif
    currlinelen = DEFLINELEN;
    currlines = 0;
}

static void formatlines(PSC_List *lines, PSC_StringBuilder *str,
	int indentfirst, int indentrest, int compact)
{
    char line[MAXLINELEN];
    int linepos = 0;
    
    const char *cstr = PSC_StringBuilder_str(str);
    int indent = indentfirst;

    if (!*cstr) return;

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
		if (!compact) PSC_List_append(lines, "", 0);
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
	    PSC_List_append(lines, longtok, free);
	    while (*cstr == ' ') ++cstr;
	    if (!*cstr) break;
	}
	else if (linepos > indent)
	{
	    line[linepos] = 0;
	    PSC_List_append(lines, PSC_copystr(line), free);
	}
	indent = indentrest;
    }

    PSC_StringBuilder_destroy(str);
}

static int printlines(FILE *out, PSC_List *lines, int autopage)
{
    int rc = -1;
    const char *pager = 0;

    if (autopage && isatty(fileno(out))
	    && PSC_List_size(lines) + 2 >= currlines)
    {
	pager = getenv("PAGER");
    }

    if (pager && *pager)
    {
	int pagerfailed = 0;
	int pfd[2];
	if (pipe(pfd) < 0)
	{
	    pagerfailed = 1;
	    goto pgdone;
	}
	int errpfd[2];
	if (pipe(errpfd) < 0)
	{
	    pagerfailed = 1;
	    close(pfd[0]);
	    close(pfd[1]);
	    goto pgdone;
	}
	struct sigaction handler;
	struct sigaction ohandler;
	memset(&handler, 0, sizeof handler);
	sigemptyset(&handler.sa_mask);
	handler.sa_handler = handlepagersig;
	pagerexited = 0;
	pagerpid = -1;
	if (sigaction(SIGCHLD, &handler, &ohandler) >= 0)
	{
	    if ((pagerpid = fork()) >= 0)
	    {
		if (pagerpid == 0)
		{
		    close(pfd[1]);
		    close(errpfd[0]);
		    fcntl(errpfd[1], F_SETFD, FD_CLOEXEC);
		    dup2(pfd[0], STDIN_FILENO);
		    close(pfd[0]);
		    int ttyfd = open("/dev/tty", O_WRONLY|O_NONBLOCK);
		    if (ttyfd >= 0)
		    {
			dup2(ttyfd, STDOUT_FILENO);
			dup2(ttyfd, STDERR_FILENO);
			close(ttyfd);
		    }
		    PSC_List *pagercmd = PSC_List_fromString(pager, " ");
		    if (pagercmd)
		    {
			size_t pagerargc = PSC_List_size(pagercmd);
			char **cmd = PSC_malloc((pagerargc+1) * sizeof *cmd);
			cmd[pagerargc] = 0;
			for (size_t i = 0; i < pagerargc; ++i)
			{
			    cmd[i] = PSC_List_at(pagercmd, i);
			}
			execvp(cmd[0], cmd);
		    }
		    FILE *errout = fdopen(errpfd[1], "a");
		    if (errout) fputs("\n", errout);
		    exit(EXIT_FAILURE);
		}
		close(pfd[0]);
		close(errpfd[1]);
		FILE *pgin = fdopen(errpfd[0], "r");
		char errmsg[2];
		if (pgin && !fgets(errmsg, 2, pgin))
		{
		    FILE *pgout = fdopen(pfd[1], "a");
		    if (pgout)
		    {
			fcntl(pfd[1], F_SETFL,
				fcntl(pfd[1], F_GETFL, 0) | O_NONBLOCK);
			PSC_ListIterator *i = PSC_List_iterator(lines);
			while (!pagerfailed && PSC_ListIterator_moveNext(i))
			{
			    const char *line = PSC_ListIterator_current(i);
			    while (!pagerfailed)
			    {
				if (pagerexited)
				{
				    pagerfailed = 1;
				    break;
				}
				if (fprintf(pgout, "%s\n", line) > 0) break;
				if (errno != EAGAIN && errno != EINTR)
				{
				    pagerfailed = 1;
				}
			    }
			}
			PSC_ListIterator_destroy(i);
			fclose(pgout);
		    }
		}
		else pagerfailed = 1;
		fclose(pgin);
	    }
	    else pagerfailed = 1;
	    sigaction(SIGCHLD, &ohandler, 0);
	}
	else pagerfailed = 1;

pgdone:
	if (pagerfailed)
	{
	    fprintf(stderr, "Error piping to PAGER: %s\n", pager);
	}
	else
	{
	    int pgrc;
	    waitpid(pagerpid, &pgrc, 0);
	    if ((WIFSIGNALED(pgrc)
			&& WTERMSIG(pgrc) != SIGINT
			&& WTERMSIG(pgrc) != SIGTERM)
		    || (WIFEXITED(pgrc) && WEXITSTATUS(pgrc) != 0))
	    {
		fprintf(stderr, "Error piping to PAGER: %s\n", pager);
	    }
	    else
	    {
		PSC_List_destroy(lines);
		return 0;
	    }
	}
    }

    PSC_ListIterator *i = PSC_List_iterator(lines);
    while (PSC_ListIterator_moveNext(i))
    {
	const char *line = PSC_ListIterator_current(i);
	if (fprintf(out, "%s\n", line) <= 0) goto done;
    }
    rc = 0;
done:
    PSC_ListIterator_destroy(i);
    PSC_List_destroy(lines);
    return rc;
}

static const char *namestr(const PSC_ConfigElement *e)
{
    switch (e->type)
    {
	case ET_LIST:
	    return e->element->name;
	case ET_SECTION:
	case ET_SECTIONLIST:
	    return e->section->name;
	default:
	    return e->name;
    }
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
	    result = e->section->name;
	    if (!result) result = "section";
	    break;

	default:
	    ;
    }
    if (!result) result = "(null)";
    return result;
}

static PSC_List *usagelines(const PSC_ConfigParser *self, FILE *out)
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
    if (nboolflags > 1) qsort(boolflags, nboolflags, 1, compareflags);
    if (noptflags > 1) qsort(optflags, noptflags,
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

    setoutputdims(out);
    PSC_List *lines = PSC_List_create();
    formatlines(lines, s, 0, 8, 0);
    return lines;
}

SOEXPORT int PSC_ConfigParser_usage(const PSC_ConfigParser *self, FILE *out)
{
    return printlines(out, usagelines(self, out), 0);
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

static void subsectionhelp(const PSC_ConfigSection *sect, PSC_List *lines)
{
    PSC_ConfigElement **reqposargs = 0;
    PSC_ConfigElement **optposargs = 0;
    PSC_ConfigElement **flags = 0;
    PSC_ListIterator *i = 0;
    PSC_StringBuilder *s = 0;
    int nreqposargs = 0;
    int noptposargs = 0;
    int nflags = 0;
    int nreqflags = 0;

    size_t elemcount = PSC_List_size(sect->elements);
    reqposargs = PSC_malloc(elemcount * sizeof *reqposargs);
    optposargs = PSC_malloc(elemcount * sizeof *optposargs);
    flags  = PSC_malloc(elemcount * sizeof *flags);

    for (i = PSC_List_iterator(sect->elements);
	    PSC_ListIterator_moveNext(i);)
    {
	PSC_ConfigElement *e = PSC_ListIterator_current(i);
	if (e->type == ET_SECTION || e->type == ET_SECTIONLIST) continue;
	if (e->flag >= 0)
	{
	    flags[nflags++] = e;
	    if (e->required) ++nreqflags;
	}
	else if (namestr(e))
	{
	    if (e->required) reqposargs[nreqposargs++] = e;
	    else optposargs[noptposargs++] = e;
	}
    }
    PSC_ListIterator_destroy(i);
    if (nflags > 1) qsort(flags, nflags, sizeof *flags, compareanyflag);

    if (!(nreqposargs + noptposargs + nflags)) goto done;

    s = PSC_StringBuilder_create();
    PSC_StringBuilder_append(s, "\nFormat: ");
    int fmtpos = 0;
    for (int j = 0; j < nreqposargs; ++j)
    {
	if (fmtpos) PSC_StringBuilder_appendChar(s, ':');
	const PSC_ConfigElement *e = reqposargs[j];
	PSC_StringBuilder_append(s, argstr(e));
	++fmtpos;
    }
    if (noptposargs > 1 && nflags)
    {
	for (int j = 0; j < noptposargs; ++j)
	{
	    if (fmtpos) PSC_StringBuilder_appendChar(s, ':');
	    const PSC_ConfigElement *e = optposargs[j];
	    PSC_StringBuilder_appendChar(s, '[');
	    PSC_StringBuilder_append(s, argstr(e));
	    PSC_StringBuilder_appendChar(s, ']');
	    ++fmtpos;
	}
    }
    else if (noptposargs > 1 && !nflags)
    {
	for (int j = 0; j < noptposargs; ++j)
	{
	    PSC_StringBuilder_appendChar(s, '[');
	    if (fmtpos) PSC_StringBuilder_appendChar(s, ':');
	    const PSC_ConfigElement *e = optposargs[j];
	    PSC_StringBuilder_append(s, argstr(e));
	    ++fmtpos;
	}
	for (int j = 0; j < noptposargs; ++j)
	{
	    PSC_StringBuilder_appendChar(s, ']');
	}
    }
    else if (noptposargs == 1)
    {
	PSC_StringBuilder_appendChar(s, '[');
	if (fmtpos) PSC_StringBuilder_appendChar(s, ':');
	PSC_StringBuilder_append(s, argstr(*optposargs));
	PSC_StringBuilder_appendChar(s, ']');
	++fmtpos;
    }
    if (nflags)
    {
	if (nreqflags)
	{
	    PSC_StringBuilder_append(s, fmtpos ? ":k=v" : "k=v");
	    if (nreqflags > 1) PSC_StringBuilder_append(s, ":...");
	    ++fmtpos;
	}
	if (nreqflags < nflags)
	{
	    PSC_StringBuilder_append(s, fmtpos ? "[:k=v" : "[k=v");
	    if (nflags - nreqflags > 1) PSC_StringBuilder_append(s, "[:...]");
	    PSC_StringBuilder_appendChar(s, ']');
	}
    }
    formatlines(lines, s, 6, 6, 0);

    for (int j = 0; j < nreqposargs; ++j)
    {
	PSC_ConfigElement *e = reqposargs[j];
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_append(s, argstr(e));
	formatlines(lines, s, 6, 6, 0);
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	formatlines(lines, s, 10, 10, 1);
    }

    for (int j = 0; j < noptposargs; ++j)
    {
	PSC_ConfigElement *e = optposargs[j];
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_append(s, argstr(e));
	formatlines(lines, s, 6, 6, 0);
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	formatlines(lines, s, 10, 10, 1);
    }

    if (nflags)
    {
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_append(s,
		"k=v: key-value pair, any of the following:");
	formatlines(lines, s, 6, 6, 0);
	for (int j = 0; j < nflags; ++j)
	{
	    PSC_ConfigElement *e = flags[j];
	    s = PSC_StringBuilder_create();
	    if (e->flag > 0) PSC_StringBuilder_appendChar(s, e->flag);
	    else PSC_StringBuilder_append(s, namestr(e));
	    PSC_StringBuilder_appendChar(s, '=');
	    if (e->type == ET_BOOL) PSC_StringBuilder_append(s, "[0|1]");
	    else PSC_StringBuilder_append(s, argstr(e));
	    if (e->required) PSC_StringBuilder_append(s, " {required}");
	    if (e->type == ET_LIST) PSC_StringBuilder_append(s, " {multiple}");
	    formatlines(lines, s, 8, 8, 0);
	    s = PSC_StringBuilder_create();
	    setdescstr(s, e);
	    formatlines(lines, s, 12, 12, 1);
	}
    }

done:
    free(flags);
    free(optposargs);
    free(reqposargs);
}

SOEXPORT int PSC_ConfigParser_help(const PSC_ConfigParser *self, FILE *out)
{
    const char *svname = 0;
    PSC_ConfigElement **shortflags = 0;
    PSC_ConfigElement **longflags = 0;
    PSC_ConfigElement **reqposargs = 0;
    PSC_ConfigElement **optposargs = 0;
    PSC_List *lines = 0;
    PSC_ListIterator *i = 0;
    PSC_StringBuilder *s = 0;
    int nshortflags = 0;
    int nlongflags = 0;
    int nreqposargs = 0;
    int noptposargs = 0;
    int havesubsect = 0;

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
	    if (namestr(e)) longflags[nlongflags++] = e;
	}
	else
	{
	    if (e->required) reqposargs[nreqposargs++] = e;
	    else optposargs[noptposargs++] = e;
	}
    }
    PSC_ListIterator_destroy(i);
    if (nshortflags > 1) qsort(shortflags, nshortflags,
	    sizeof *shortflags, compareelements);
    if (nlongflags > 1) qsort(longflags, nlongflags,
	    sizeof *longflags, compareelemnames);

    lines = usagelines(self, out);

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
	if (namestr(e))
	{
	    PSC_StringBuilder_append(s, ", --");
	    if (e->type == ET_BOOL)
	    {
		PSC_StringBuilder_append(s, "[no-]");
	    }
	    PSC_StringBuilder_append(s, namestr(e));
	    if (e->type != ET_BOOL)
	    {
		PSC_StringBuilder_appendChar(s, '=');
		PSC_StringBuilder_append(s, argstr(e));
	    }
	}
	formatlines(lines, s, 4, 4, 0);
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	formatlines(lines, s, 8, 8, 0);
	if (e->type == ET_SECTION || e->type == ET_SECTIONLIST)
	{
	    ++havesubsect;
	    subsectionhelp(e->section, lines);
	}
    }

    for (int j = 0; j < nlongflags; ++j)
    {
	PSC_ConfigElement *e = longflags[j];
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_append(s, "\n--");
	if (e->type == ET_BOOL)
	{
	    PSC_StringBuilder_append(s, "[no-]");
	}
	PSC_StringBuilder_append(s, namestr(e));
	if (e->type != ET_BOOL)
	{
	    PSC_StringBuilder_appendChar(s, '=');
	    PSC_StringBuilder_append(s, argstr(e));
	}
	formatlines(lines, s, 4, 4, 0);
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	formatlines(lines, s, 8, 8, 0);
	if (e->type == ET_SECTION || e->type == ET_SECTIONLIST)
	{
	    ++havesubsect;
	    subsectionhelp(e->section, lines);
	}
    }

    for (int j = 0; j < nreqposargs; ++j)
    {
	PSC_ConfigElement *e = reqposargs[j];
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_appendChar(s, '\n');
	PSC_StringBuilder_append(s, argstr(e));
	formatlines(lines, s, 4, 4, 0);
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	formatlines(lines, s, 8, 8, 0);
	if (e->type == ET_SECTION || e->type == ET_SECTIONLIST)
	{
	    ++havesubsect;
	    subsectionhelp(e->section, lines);
	}
    }

    for (int j = 0; j < noptposargs; ++j)
    {
	PSC_ConfigElement *e = optposargs[j];
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_appendChar(s, '\n');
	PSC_StringBuilder_append(s, argstr(e));
	formatlines(lines, s, 4, 4, 0);
	s = PSC_StringBuilder_create();
	setdescstr(s, e);
	formatlines(lines, s, 8, 8, 0);
	if (e->type == ET_SECTION || e->type == ET_SECTIONLIST)
	{
	    ++havesubsect;
	    subsectionhelp(e->section, lines);
	}
    }

    if (havesubsect)
    {
	s = PSC_StringBuilder_create();
	PSC_StringBuilder_append(s, "\nFor sub-section arguments delimited by "
		"a colon (:), any colons and equals signs (=) contained in "
		"values must be escaped with a backslash (\\). Alternatively, "
		"values may be quoted in a pair of brackets (between `[' and "
		"`]') if they don't contain those themselves.");
	formatlines(lines, s, 4, 4, 0);
    }

    free(optposargs);
    free(reqposargs);
    free(longflags);
    free(shortflags);

    return printlines(out, lines, self->autopage);
}

static ArgContext *createargcontext(const PSC_ConfigSection *sect)
{
    ArgContext *ctx = PSC_malloc(sizeof *ctx);
    ctx->needsarg = PSC_Queue_create();
    ctx->positional = PSC_Queue_create();
    ctx->flags = PSC_HashTable_create(5);
    ctx->onceflags = PSC_HashTable_create(5);
    ctx->seenflags = PSC_HashTable_create(5);
    ctx->listposarg = 0;

    PSC_ListIterator *i = PSC_List_iterator(sect->elements);
    while (PSC_ListIterator_moveNext(i))
    {
	PSC_ConfigElement *e = PSC_ListIterator_current(i);
	if (e->flag < 0)
	{
	    PSC_Queue_enqueue(ctx->positional, e, 0);
	}
	else
	{
	    PSC_HashTable_set(ctx->flags, namestr(e), e, 0);
	    if (e->flag)
	    {
		char flagstr[2] = {0};
		*flagstr = e->flag;
		PSC_HashTable_set(ctx->flags, flagstr, e, 0);
	    }
	    if (e->type != ET_LIST && e->type != ET_SECTIONLIST)
	    {
		PSC_HashTable_set(ctx->onceflags, namestr(e), e, 0);
	    }
	}
    }
    PSC_ListIterator_destroy(i);

    return ctx;
}

static void deleteargcontext(ArgContext *ctx)
{
    if (!ctx) return;
    PSC_HashTable_destroy(ctx->seenflags);
    PSC_HashTable_destroy(ctx->onceflags);
    PSC_HashTable_destroy(ctx->flags);
    PSC_Queue_destroy(ctx->positional);
    PSC_Queue_destroy(ctx->needsarg);
    free(ctx);
}

static int  parseshortflag(PSC_ConfigElement **e, PSC_Config *cfg,
	ArgContext *ctx, char flag)
{
    char flagstr[] = { flag, 0 };
    *e = PSC_HashTable_get(ctx->flags, flagstr);
    if (!*e) return -1;
    if (PSC_HashTable_get(ctx->onceflags, namestr(*e)))
    {
	if (PSC_HashTable_get(ctx->seenflags, namestr(*e)))
	{
	    return -1;
	}
	PSC_HashTable_set(ctx->seenflags, namestr(*e), *e, 0);
    }
    if ((*e)->type == ET_BOOL)
    {
	PSC_HashTable_set(cfg->values, namestr(*e), &boolval, 0);
	*e = 0;
    }
    return 0;
}

static void deleteargsectitem(void *item)
{
    if (!item) return;
    ArgSectItem *i = item;
    free(i->val);
    free(i->key);
    free(i);
}

static PSC_Queue *argsectionparts(const char *str)
{
    PSC_Queue *parts = PSC_Queue_create();

    int ok = 1;
    while (ok && *str)
    {
	ArgSectItem *item = PSC_malloc(sizeof *item);
	item->key = 0;
	item->val = 0;
	for (;;)
	{
	    char *tmp = PSC_malloc(strlen(str)+1);
	    int escape = 0;
	    int quote = 0;
	    if (*str == '[')
	    {
		quote = 1;
		++str;
	    }
	    char *tp = tmp;
	    for (;
		    ok && *str &&
		    (escape || quote || (*str != ':' && *str != '='));
		    ++str)
	    {
		if (escape)
		{
		    *tp++ = *str;
		    escape = 0;
		    continue;
		}
		if (quote)
		{
		    if (*str == ']')
		    {
			if (str[1] && str[1] != ':' && str[1] != '=')
			{
			    free(tmp);
			    ok = 0;
			}
			else quote = 0;
		    }
		    else if (*str == '[')
		    {
			free(tmp);
			ok = 0;
		    }
		    else *tp++ = *str;
		}
		else if (*str == '\\')
		{
		    escape = 1;
		}
		else *tp++ = *str;
	    }
	    *tp = 0;
	    if (!ok) break;
	    if (*str == '=')
	    {
		if (item->key)
		{
		    free(tmp);
		    ok = 0;
		    break;
		}
		item->key = PSC_realloc(tmp, strlen(tmp)+1);
		++str;
	    }
	    else if (!*str || *str == ':')
	    {
		item->val = PSC_realloc(tmp, strlen(tmp)+1);
		if (*str) ++str;
		break;
	    }
	}
	if (ok) PSC_Queue_enqueue(parts, item, deleteargsectitem);
	else deleteargsectitem(item);
    }
    if (ok && !*str) return parts;
    PSC_Queue_destroy(parts);
    return 0;
}

static void (*parseargsvalue(void **val, PSC_ConfigElement *e,
	const char *str, int recurse))(void *);

static PSC_Config *parseargssection(const PSC_ConfigSection *sect,
	const char *str)
{
    int ok = 0;
    PSC_Config *cfg = PSC_malloc(sizeof *cfg);
    cfg->values = PSC_HashTable_create(5);
    ArgContext *ctx = createargcontext(sect);
    PSC_Queue *parts = argsectionparts(str);
    if (!parts) goto done;

    ok = 1;
    ArgSectItem *item;
    while (ok && (item = PSC_Queue_dequeue(parts)))
    {
	PSC_ConfigElement *e = 0;
	if (item->key)
	{
	    e = PSC_HashTable_get(ctx->flags, item->key);
	    if (e && PSC_HashTable_get(ctx->onceflags, namestr(e)))
	    {
		if (PSC_HashTable_get(ctx->seenflags, namestr(e))) e = 0;
		else PSC_HashTable_set(ctx->seenflags, namestr(e), e, 0);
	    }
	}
	else e = PSC_Queue_dequeue(ctx->positional);
	if (e)
	{
	    void *val = 0;
	    void (*deleter)(void *) = parseargsvalue(&val, e, item->val, 0);
	    if (!val) ok = 0;
	    else if (e->type == ET_LIST || e->type == ET_SECTIONLIST)
	    {
		PSC_List *list = PSC_HashTable_get(cfg->values, namestr(e));
		if (!list)
		{
		    list = PSC_List_create();
		    PSC_HashTable_set(cfg->values, namestr(e),
			    list, deleteList);
		}
		PSC_List_append(list, val, deleter);
	    }
	    else
	    {
		PSC_HashTable_set(cfg->values, namestr(e), val, deleter);
	    }
	} else ok = 0;
	deleteargsectitem(item);
    }

done:
    if (!ok)
    {
	PSC_Config_destroy(cfg);
	cfg = 0;
    }
    PSC_Queue_destroy(parts);
    deleteargcontext(ctx);
    return cfg;
}

static void (*parseargsvalue(void **val, PSC_ConfigElement *e,
	const char *str, int recurse))(void *)
{
    ElemType t = e->type;
    if (t == ET_LIST) t = e->element->type;
    switch (t)
    {
	long lv;
	double fv;
	char *endptr;

	case ET_STRING:
	    *val = PSC_copystr(str);
	    return free;

	case ET_INTEGER:
	    errno = 0;
	    lv = strtol(str, &endptr, 10);
	    if (!errno && endptr != str && !*endptr)
	    {
		long *v = PSC_malloc(sizeof *v);
		*v = lv;
		*val = v;
		return free;
	    }
	    return 0;

	case ET_FLOAT:
	    errno = 0;
	    fv = strtod(str, &endptr);
	    if (!errno && endptr != str && !*endptr)
	    {
		double *v = PSC_malloc(sizeof *v);
		*v = fv;
		*val = v;
		return free;
	    }
	    return 0;

	case ET_SECTION:
	case ET_SECTIONLIST:
	    if (!recurse) return 0;
	    *val = parseargssection(e->section, str);
	    return deleteConfig;

	default:
	    return 0;
    }
}

static int parseflagarg(PSC_Config *cfg, ArgContext *ctx, const char *str,
	PSC_ConfigElement *e)
{
    if (!e) e = PSC_Queue_dequeue(ctx->needsarg);
    if (!e) return 0;
    if (e->type == ET_BOOL) return -1;
    void *val = 0;
    void (*deleter)(void *) = parseargsvalue(&val, e, str, 1);
    if (!val) return -1;
    if (e->type == ET_LIST || e->type == ET_SECTIONLIST)
    {
	PSC_List *list = PSC_HashTable_get(cfg->values, namestr(e));
	if (!list)
	{
	    list = PSC_List_create();
	    PSC_HashTable_set(cfg->values, namestr(e), list, deleteList);
	}
	PSC_List_append(list, val, deleter);
    }
    else
    {
	PSC_HashTable_set(cfg->values, namestr(e), val, deleter);
    }
    return 1;
}

static int parselongflag(PSC_Config *cfg, ArgContext *ctx, const char *flag)
{
    PSC_ConfigElement *e = 0;
    const char *arg = 0;
    size_t eqpos = strcspn(flag, "=");
    if (flag[eqpos])
    {
	char *flagstr = PSC_malloc(eqpos+1);
	memcpy(flagstr, flag, eqpos);
	flagstr[eqpos] = 0;
	e = PSC_HashTable_get(ctx->flags, flagstr);
	free(flagstr);
	arg = flag+eqpos+1;
    }
    else
    {
	if (!strncmp(flag, "no-", 3))
	{
	    e = PSC_HashTable_get(ctx->flags, flag+3);
	    if (e && e->type == ET_BOOL)
	    {
		if (PSC_HashTable_get(ctx->seenflags, namestr(e))) return -1;
		PSC_HashTable_set(ctx->seenflags, namestr(e), e, 0);
		PSC_HashTable_delete(cfg->values, namestr(e));
		return 0;
	    }
	    e = 0;
	}
	e = PSC_HashTable_get(ctx->flags, flag);
	if (e && PSC_HashTable_get(ctx->onceflags, namestr(e)))
	{
	    if (PSC_HashTable_get(ctx->seenflags, namestr(e))) return -1;
	    PSC_HashTable_set(ctx->seenflags, namestr(e), e, 0);
	}
    }
    if (!e) return -1;
    if (e->type == ET_BOOL)
    {
	if (arg) return -1;
	PSC_HashTable_set(cfg->values, namestr(e), &boolval, 0);
	return 0;
    }
    if (arg)
    {
	if (parseflagarg(cfg, ctx, arg, e) < 0) return -1;
    }
    else
    {
	PSC_Queue_enqueue(ctx->needsarg, e, 0);
    }
    return 0;
}

static int parsefromargs(const ArgData *self, PSC_Config *cfg,
    const PSC_ConfigSection *sect)
{
    int endflags = 0;
    int escapedash = 0;
    int rc = -1;

    ArgContext *ctx = createargcontext(sect);
    for (int arg = 1; arg < self->argc; ++arg)
    {
	char *o = self->argv[arg];

	if (!escapedash && *o == '-' && o[1] == '-' && !o[2])
	{
	    escapedash = 1;
	    continue;
	}

	if (!endflags && !escapedash && *o == '-' && o[1])
	{
	    if (o[1] == '-')
	    {
		if (parselongflag(cfg, ctx, o+2) < 0) goto done;
	    }
	    else
	    {
		PSC_ConfigElement *e;
		if (parseshortflag(&e, cfg, ctx, *++o) < 0) goto done;
		int multiflags = 1;
		for (char *co = ++o; *co; ++co)
		{
		    char flagstr[] = { *co, 0 };
		    if (!PSC_HashTable_get(ctx->flags, flagstr))
		    {
			multiflags = 0;
			break;
		    }
		}
		if (multiflags)
		{
		    if (e) PSC_Queue_enqueue(ctx->needsarg, e, 0);
		    for (; *o; ++o)
		    {
			if (parseshortflag(&e, cfg, ctx, *o) < 0) goto done;
			if (e) PSC_Queue_enqueue(ctx->needsarg, e, 0);
		    }
		}
		else
		{
		    if (!e || parseflagarg(cfg, ctx, o, e) < 0) goto done;
		}
	    }
	}
	else
	{
	    int haveflagarg = parseflagarg(cfg, ctx, o, 0);
	    if (haveflagarg < 0) goto done;
	    if (!haveflagarg)
	    {
		endflags = 1;
		PSC_ConfigElement *e = ctx->listposarg;
		if (!e) e = PSC_Queue_dequeue(ctx->positional);
		if (!e) goto done;
		if (e->type == ET_LIST || e->type == ET_SECTIONLIST)
		{
		    ctx->listposarg = e;
		}
		void *val = 0;
		void (*deleter)(void *) = parseargsvalue(&val, e, o, 1);
		if (!val) goto done;
		if (e->type == ET_LIST || e->type == ET_SECTIONLIST)
		{
		    PSC_List *list = PSC_HashTable_get(
			    cfg->values, namestr(e));
		    if (!list)
		    {
			list = PSC_List_create();
			PSC_HashTable_set(cfg->values, namestr(e),
				list, deleteList);
		    }
		    PSC_List_append(list, val, deleter);
		}
		else
		{
		    PSC_HashTable_set(cfg->values, namestr(e), val, deleter);
		}
	    }
	}
    }
    rc = 0;

done:
    deleteargcontext(ctx);
    return rc;
}

SOEXPORT int PSC_ConfigParser_parse(const PSC_ConfigParser *self,
	PSC_Config **config)
{
    if (PSC_List_size(self->parsers) == 0)
    {
	PSC_Service_panic(
		"Tried to parse config without any configured parsers");
    }

    PSC_ListIterator *i = 0;
    int rc = 0;
    PSC_Config *cfg = PSC_malloc(sizeof *cfg);
    cfg->values = PSC_HashTable_create(5);

    for (i = PSC_List_iterator(self->parsers); PSC_ListIterator_moveNext(i);)
    {
	ConcreteParser *p = PSC_ListIterator_current(i);
	if (p->type == PT_ARGS)
	{
	    const ArgData *ad = p->parserData;
	    rc = parsefromargs(ad, cfg, self->root);
	}
	if (rc != 0) break;
    }
    PSC_ListIterator_destroy(i);

    if (rc != 0) PSC_Config_destroy(cfg);
    else *config = cfg;
    return rc < 0 ? -1 : 0;
}

SOEXPORT void PSC_ConfigParser_destroy(PSC_ConfigParser *self)
{
    if (!self) return;
    PSC_List_destroy(self->parsers);
    free(self);
}

DECLEXPORT const void *PSC_Config_get(const PSC_Config *self, const char *name)
{
    return PSC_HashTable_get(self->values, name);
}

DECLEXPORT void PSC_Config_destroy(PSC_Config *self)
{
    if (!self) return;
    PSC_HashTable_destroy(self->values);
    free(self);
}

