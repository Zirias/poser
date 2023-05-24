#ifndef POSER_CORE_INT_RUNOPTS_H
#define POSER_CORE_INT_RUNOPTS_H

#include <poser/core/runopts.h>

typedef struct PSC_RunOpts
{
    const char *pidfile;
    long uid;
    long gid;
    int daemonize;
    int waitLaunched;
} PSC_RunOpts;

PSC_RunOpts *runOpts(void) ATTR_RETNONNULL;

#endif
