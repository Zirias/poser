#ifndef POSER_CORE_SERVER_H
#define POSER_CORE_SERVER_H

#include <poser/decl.h>

#include <poser/core/proto.h>

C_CLASS_DECL(PSC_Event);
C_CLASS_DECL(PSC_Server);
C_CLASS_DECL(PSC_TcpServerOpts);
C_CLASS_DECL(PSC_UnixServerOpts);

DECLEXPORT PSC_TcpServerOpts *
PSC_TcpServerOpts_create(int port)
    ATTR_RETNONNULL;

DECLEXPORT void
PSC_TcpServerOpts_bind(PSC_TcpServerOpts *self, const char *bindhost)
    CMETHOD ATTR_NONNULL((2));

DECLEXPORT void
PSC_TcpServerOpts_enableTls(PSC_TcpServerOpts *self,
	const char *certfile, const char *keyfile)
    CMETHOD ATTR_NONNULL((2)) ATTR_NONNULL((3));

DECLEXPORT void
PSC_TcpServerOpts_setProto(PSC_TcpServerOpts *self, PSC_Proto proto)
    CMETHOD;

DECLEXPORT void
PSC_TcpServerOpts_numericHosts(PSC_TcpServerOpts *self)
    CMETHOD;

DECLEXPORT void
PSC_TcpServerOpts_connWait(PSC_TcpServerOpts *self)
    CMETHOD;

DECLEXPORT void
PSC_TcpServerOpts_destroy(PSC_TcpServerOpts *self);

DECLEXPORT PSC_UnixServerOpts *
PSC_UnixServerOpts_create(const char *name)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT void
PSC_UnixServerOpts_owner(PSC_UnixServerOpts *self, int uid, int gid)
    CMETHOD;

DECLEXPORT void
PSC_UnixServerOpts_mode(PSC_UnixServerOpts *self, int mode)
    CMETHOD;

DECLEXPORT void
PSC_UnixServerOpts_connWait(PSC_UnixServerOpts *self)
    CMETHOD;

DECLEXPORT void
PSC_UnixServerOpts_destroy(PSC_UnixServerOpts *self);

DECLEXPORT PSC_Server *
PSC_Server_createTcp(const PSC_TcpServerOpts *opts)
    ATTR_NONNULL((1));

DECLEXPORT PSC_Server *
PSC_Server_createUnix(const PSC_UnixServerOpts *opts)
    ATTR_NONNULL((1));

DECLEXPORT PSC_Event *
PSC_Server_clientConnected(PSC_Server *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Server_clientDisconnected(PSC_Server *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT void
PSC_Server_destroy(PSC_Server *self);

#endif
