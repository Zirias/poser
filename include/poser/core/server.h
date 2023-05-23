#ifndef POSER_CORE_SERVER_H
#define POSER_CORE_SERVER_H

#include <poser/decl.h>

#include <poser/core/proto.h>

C_CLASS_DECL(PSC_Event);
C_CLASS_DECL(PSC_Server);

DECLEXPORT void
PSC_TcpServerOpts_init(int port);

DECLEXPORT void
PSC_TcpServerOpts_bind(const char *bindhost)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_TcpServerOpts_enableTls(const char *certfile, const char *keyfile)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));

DECLEXPORT void
PSC_TcpServerOpts_setProto(PSC_Proto proto);

DECLEXPORT void
PSC_TcpServerOpts_numericHosts(void);

DECLEXPORT void
PSC_TcpServerOpts_connWait(void);

DECLEXPORT PSC_Server *
PSC_Server_createTcp(void);

DECLEXPORT PSC_Event *
PSC_Server_clientConnected(PSC_Server *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Server_clientDisconnected(PSC_Server *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT void
PSC_Server_destroy(PSC_Server *self);

#endif
