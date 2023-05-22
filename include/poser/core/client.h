#ifndef POSER_CORE_CLIENT_H
#define POSER_CORE_CLIENT_H

#include <poser/decl.h>

#include <poser/core/proto.h>

C_CLASS_DECL(PSC_Connection);

typedef void (*PSC_ClientCreatedHandler)(
	void *receiver, PSC_Connection *connection);

DECLEXPORT void
PSC_TcpClientOpts_init(const char *remotehost, int port)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_TcpClientOpts_enableTls(const char *certfile, const char *keyfile);

DECLEXPORT void
PSC_TcpClientOpts_setProto(PSC_Proto proto);

DECLEXPORT void
PSC_TcpClientOpts_setNumericHosts(void);

DECLEXPORT void
PSC_TcpClientOpts_setBlacklistHits(int blacklistHits);

DECLEXPORT PSC_Connection *
PSC_Connection_createTcpClient(void);

DECLEXPORT int
PSC_Connection_createTcpClientAsync(void *receiver,
	PSC_ClientCreatedHandler callback)
    ATTR_NONNULL((2));

#endif
