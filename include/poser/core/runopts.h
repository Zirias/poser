#ifndef POSER_CORE_RUNOPTS_H
#define POSER_CORE_RUNOPTS_H

#include <poser/decl.h>

typedef int (*PSC_main)(void *data);

DECLEXPORT void
PSC_RunOpts_init(PSC_main rmain, void *data, const char *pidfile)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_RunOpts_runas(long uid, long gid);

DECLEXPORT void
PSC_RunOpts_foreground(void);

DECLEXPORT void
PSC_RunOpts_nowait(void);

#endif
