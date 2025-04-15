#ifndef POSER_CORE_INT_CLIENT_H
#define POSER_CORE_INT_CLIENT_H

#include <poser/core/client.h>
#include <sys/socket.h>

C_CLASS_DECL(PSC_IpAddr);

#ifdef WITH_TLS
void
PSC_Connection_unreftlsctx(void);
#endif

void
PSC_Connection_blacklistAddress(int hits, const PSC_IpAddr *addr)
    ATTR_NONNULL((2));

#endif
