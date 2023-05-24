#ifndef POSER_CORE_RUNOPTS_H
#define POSER_CORE_RUNOPTS_H

#include <poser/decl.h>

DECLEXPORT void
PSC_RunOpts_init(const char *pidfile);

DECLEXPORT void
PSC_RunOpts_runas(long uid, long gid);

DECLEXPORT void
PSC_RunOpts_enableDefaultLogging(const char *logident);

DECLEXPORT void
PSC_RunOpts_foreground(void);

DECLEXPORT void
PSC_RunOpts_nowait(void);

#endif
