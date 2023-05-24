#ifndef POSER_CORE_DAEMON_H
#define POSER_CORE_DAEMON_H

#include <poser/decl.h>

typedef int (*PSC_Daemon_main)(void *data);

DECLEXPORT int
PSC_Daemon_run(PSC_Daemon_main dmain, void *data)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_Daemon_launched(void);

#endif
