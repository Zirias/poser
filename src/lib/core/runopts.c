#include "runopts.h"

#include <string.h>
#include <threads.h>

static thread_local PSC_RunOpts opts;

SOLOCAL PSC_RunOpts *runOpts(void)
{
    return &opts;
}

SOEXPORT void PSC_RunOpts_init(PSC_main rmain, void *data, const char *pidfile)
{
    opts.rmain = rmain;
    opts.data = data;
    opts.pidfile = pidfile;
    opts.uid = -1;
    opts.gid = -1;
    opts.daemonize = 1;
    opts.waitLaunched = 1;
}

SOEXPORT void PSC_RunOpts_runas(long uid, long gid)
{
    opts.uid = uid;
    opts.gid = gid;
}

SOEXPORT void PSC_RunOpts_foreground(void)
{
    opts.daemonize = 0;
}

SOEXPORT void PSC_RunOpts_nowait(void)
{
    opts.waitLaunched = 0;
}
