#ifndef POSER_CORE_INT_CONNECTION_H
#define POSER_CORE_INT_CONNECTION_H

#include <poser/core/connection.h>

#include <sys/socket.h>

#ifdef WITH_TLS
#include <openssl/evp.h>
#include <openssl/x509.h>
#endif

typedef enum ConnectionCreateMode
{
    CCM_NORMAL,
    CCM_CONNECTING
} ConnectionCreateMode;

typedef enum TlsMode
{
    TM_NONE,
    TM_CLIENT,
    TM_SERVER
} TlsMode;

typedef struct ConnOpts
{
#ifdef WITH_TLS
    X509 *tls_cert;
    EVP_PKEY *tls_key;
    const char *tls_hostname;
    TlsMode tls_mode;
    int tls_noverify;
#endif
    ConnectionCreateMode createmode;
    int blacklisthits;
} ConnOpts;

PSC_Connection *
PSC_Connection_create(int fd, const ConnOpts *opts)
    ATTR_RETNONNULL ATTR_NONNULL((2));

void
PSC_Connection_setRemoteAddr(PSC_Connection *self,
	struct sockaddr *addr, socklen_t addrlen, int numericOnly)
    CMETHOD ATTR_NONNULL((2));

void
PSC_Connection_setRemoteAddrStr(PSC_Connection *self, const char *addr)
    CMETHOD;

void
PSC_Connection_destroy(PSC_Connection *self);

#endif
