#ifndef POSER_CORE_INT_CONNECTION_H
#define POSER_CORE_INT_CONNECTION_H

#include <poser/core/connection.h>

#include <sys/socket.h>

#ifdef WITH_TLS
#include <openssl/evp.h>
#include <openssl/x509.h>
#endif

#ifndef WRBUFSZ
#define WRBUFSZ (16*1024)
#endif

#ifndef DEFRDBUFSZ
#define DEFRDBUFSZ WRBUFSZ
#endif

typedef enum ConnectionCreateMode
{
    CCM_NORMAL,
    CCM_CONNECTING,
    CCM_PIPERD,
    CCM_PIPEWR
} ConnectionCreateMode;

typedef enum TlsMode
{
    TM_NONE,
    TM_CLIENT,
    TM_SERVER
} TlsMode;

typedef struct ConnOpts
{
    size_t rdbufsz;
#ifdef WITH_TLS
    SSL_CTX *tls_ctx;
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
PSC_Connection_setRemoteAddr(PSC_Connection *self, struct sockaddr *addr)
    CMETHOD ATTR_NONNULL((2));

void
PSC_Connection_setRemoteAddrStr(PSC_Connection *self, const char *addr)
    CMETHOD;

void
PSC_Connection_destroy(PSC_Connection *self);

#endif
