#ifndef POSER_CORE_CLIENT_H
#define POSER_CORE_CLIENT_H

#include <poser/decl.h>

#include <poser/core/proto.h>

C_CLASS_DECL(PSC_Connection);
C_CLASS_DECL(PSC_TcpClientOpts);

typedef void (*PSC_ClientCreatedHandler)(
	void *receiver, PSC_Connection *connection);

DECLEXPORT PSC_TcpClientOpts *
PSC_TcpClientOpts_create(const char *remotehost, int port)
    ATTR_RETNONNULL ATTR_NONNULL((1));

DECLEXPORT void
PSC_TcpClientOpts_enableTls(PSC_TcpClientOpts *self,
	const char *certfile, const char *keyfile)
    CMETHOD;

DECLEXPORT void
PSC_TcpClientOpts_disableCertVerify(PSC_TcpClientOpts *self)
    CMETHOD;

DECLEXPORT void
PSC_TcpClientOpts_setProto(PSC_TcpClientOpts *self, PSC_Proto proto)
    CMETHOD;

DECLEXPORT void
PSC_TcpClientOpts_numericHosts(PSC_TcpClientOpts *self)
    CMETHOD;

DECLEXPORT void
PSC_TcpClientOpts_setBlacklistHits(PSC_TcpClientOpts *self, int blacklistHits)
    CMETHOD;

DECLEXPORT void
PSC_TcpClientOpts_destroy(PSC_TcpClientOpts *self);

DECLEXPORT PSC_Connection *
PSC_Connection_createTcpClient(const PSC_TcpClientOpts *opts)
    ATTR_NONNULL((1));

DECLEXPORT int
PSC_Connection_createTcpClientAsync(const PSC_TcpClientOpts *opts,
	void *receiver, PSC_ClientCreatedHandler callback)
    ATTR_NONNULL((1)) ATTR_NONNULL((3));

#endif
