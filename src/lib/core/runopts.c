#include "runopts.h"

#include <string.h>

static PSC_RunOpts opts;
static int initialized;

SOLOCAL PSC_RunOpts *runOpts(void)
{
    if (!initialized) PSC_RunOpts_init(0);
    return &opts;
}

SOEXPORT void PSC_RunOpts_init(const char *pidfile)
{
    opts.pidfile = pidfile;
    opts.uid = -1;
    opts.gid = -1;
    opts.daemonize = 1;
    opts.waitLaunched = 1;
    initialized = 1;
}

SOEXPORT void PSC_RunOpts_runas(long uid, long gid)
{
    if (!initialized) PSC_RunOpts_init(0);
    opts.uid = uid;
    opts.gid = gid;
}

SOEXPORT void PSC_RunOpts_foreground(void)
{
    if (!initialized) PSC_RunOpts_init(0);
    opts.daemonize = 0;
}

SOEXPORT void PSC_RunOpts_nowait(void)
{
    if (!initialized) PSC_RunOpts_init(0);
    opts.waitLaunched = 0;
}
