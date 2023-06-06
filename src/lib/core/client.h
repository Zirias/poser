#ifndef POSER_CORE_INT_CLIENT_H
#define POSER_CORE_INT_CLIENT_H

#include <poser/core/client.h>
#include <sys/socket.h>

#ifdef WITH_TLS
void
PSC_Connection_unreftlsctx(void);
#endif

void
PSC_Connection_blacklistAddress(int hits, socklen_t len, struct sockaddr *addr)
    ATTR_NONNULL((3));

#endif
