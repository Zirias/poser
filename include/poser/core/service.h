#ifndef POSER_CORE_SERVICE_H
#define POSER_CORE_SERVICE_H

#include <poser/decl.h>

#define MAXPANICHANDLERS 8

C_CLASS_DECL(PSC_EAStartup);
C_CLASS_DECL(PSC_Event);

typedef void (*PSC_PanicHandler)(const char *msg) ATTR_NONNULL((1));

DECLEXPORT PSC_Event *
PSC_Service_readyRead(void)
    ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Service_readyWrite(void)
    ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Service_prestartup(void)
    ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Service_startup(void)
    ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Service_shutdown(void)
    ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Service_tick(void)
    ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Service_eventsDone(void)
    ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT void
PSC_Service_registerRead(int id);

DECLEXPORT void
PSC_Service_unregisterRead(int id);

DECLEXPORT void
PSC_Service_registerWrite(int id);

DECLEXPORT void
PSC_Service_unregisterWrite(int id);

DECLEXPORT void
PSC_Service_registerPanic(PSC_PanicHandler handler)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_Service_unregisterPanic(PSC_PanicHandler handler)
    ATTR_NONNULL((1));

DECLEXPORT int
PSC_Service_setTickInterval(unsigned msec);

DECLEXPORT int
PSC_Service_loop(void);

DECLEXPORT int
PSC_Service_run(void);

DECLEXPORT void
PSC_Service_quit(void);

DECLEXPORT void
PSC_Service_shutdownLock(void);

DECLEXPORT void
PSC_Service_shutdownUnlock(void);

DECLEXPORT void
PSC_Service_panic(const char *msg)
    ATTR_NONNULL((1)) ATTR_NORETURN;

DECLEXPORT void
PSC_EAStartup_return(PSC_EAStartup *self, int rc)
    CMETHOD;

#endif
