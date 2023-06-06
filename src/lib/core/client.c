#define _DEFAULT_SOURCE

#include "client.h"
#include "connection.h"

#include <poser/core/event.h>
#include <poser/core/log.h>
#include <poser/core/service.h>
#include <poser/core/threadpool.h>
#include <poser/core/util.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef WITH_TLS
#include <openssl/pem.h>
#include <openssl/ssl.h>
#endif

#define BLACKLISTSIZE 32

struct PSC_TcpClientOpts
{
    size_t rdbufsz;
#ifdef WITH_TLS
    char *tls_certfile;
    char *tls_keyfile;
#endif
    PSC_Proto proto;
    int port;
    int numerichosts;
#ifdef WITH_TLS
    int tls;
    int noverify;
#endif
    int blacklisthits;
    int refcnt;
    char remotehost[];
};

struct PSC_UnixClientOpts
{
    size_t rdbufsz;
    char sockname[];
};

typedef struct BlacklistEntry
{
    socklen_t len;
    struct sockaddr_storage val;
    int hits;
} BlacklistEntry;

typedef struct ResolveJobData
{
    struct addrinfo *res0;
    void *receiver;
#ifdef WITH_TLS
    X509 *cert;
    EVP_PKEY *key;
#endif
    PSC_ClientCreatedHandler callback;
    PSC_TcpClientOpts *opts;
} ResolveJobData;

static BlacklistEntry blacklist[BLACKLISTSIZE];

#ifdef WITH_TLS
static SSL_CTX *tls_ctx;
static int tls_ctx_ref;

static SSL_CTX *gettlsctx(void)
{
    if (!tls_ctx)
    {
	tls_ctx = SSL_CTX_new(TLS_client_method());
	if (!tls_ctx)
	{
	    PSC_Log_msg(PSC_L_ERROR, "client: error creating TLS context");
	    return 0;
	}
	SSL_CTX_set_default_verify_paths(tls_ctx);
	SSL_CTX_set_verify(tls_ctx, SSL_VERIFY_PEER, 0);
    }
    ++tls_ctx_ref;
    return tls_ctx;
}

SOLOCAL void PSC_Connection_unreftlsctx(void)
{
    if (!--tls_ctx_ref)
    {
	SSL_CTX_free(tls_ctx);
	tls_ctx = 0;
    }
}
#endif

SOLOCAL void PSC_Connection_blacklistAddress(int hits,
	socklen_t len, struct sockaddr *addr)
{
    for (size_t i = 0; i < BLACKLISTSIZE; ++i)
    {
	if (blacklist[i].len) continue;
	memcpy(&blacklist[i].val, addr, len);
	blacklist[i].len = len;
	blacklist[i].hits = hits;
	return;
    }
}

static int blacklistcheck(socklen_t len, struct sockaddr *addr)
{
    for (size_t i = 0; i < BLACKLISTSIZE; ++i)
    {
	if (blacklist[i].len == len && !memcmp(&blacklist[i].val, addr, len))
	{
	    if (!--blacklist[i].hits) blacklist[i].len = 0;
	    return 0;
	}
    }
    return 1;
}

static PSC_Connection *createFromAddrinfo(
	const PSC_TcpClientOpts *opts, struct addrinfo *res0
#ifdef WITH_TLS
	, X509 *cert, EVP_PKEY *key
#endif
	)
{
    struct addrinfo *res;
    int fd = -1;

    for (res = res0; res; res = res->ai_next)
    {
	if (res->ai_family != AF_INET && res->ai_family != AF_INET6) continue;
	if (opts->proto == PSC_P_IPv4 && res->ai_family != AF_INET) continue;
	if (opts->proto == PSC_P_IPv6 && res->ai_family != AF_INET6) continue;
	if (!blacklistcheck(res->ai_addrlen, res->ai_addr)) continue;
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) continue;
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	errno = 0;
	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0
		&& errno != EINPROGRESS)
	{
	    close(fd);
	    fd = -1;
	}
	else break;
    }
    if (fd < 0)
    {
	freeaddrinfo(res0);
#ifdef WITH_TLS
	EVP_PKEY_free(key);
	X509_free(cert);
#endif
	PSC_Log_fmt(PSC_L_ERROR, "client: cannot connect to `%s'",
		opts->remotehost);
	return 0;
    }
    ConnOpts copts = {
	.rdbufsz = opts->rdbufsz,
#ifdef WITH_TLS
	.tls_ctx = opts->tls ? gettlsctx() : 0,
	.tls_cert = cert,
	.tls_key = key,
	.tls_hostname = opts->remotehost,
	.tls_mode = opts->tls ? TM_CLIENT : TM_NONE,
	.tls_noverify = opts->noverify,
#endif
	.createmode = CCM_CONNECTING,
	.blacklisthits = opts->blacklisthits
    };
    PSC_Connection *conn = PSC_Connection_create(fd, &copts);
    PSC_Connection_setRemoteAddr(conn, res->ai_addr, res->ai_addrlen,
	    opts->numerichosts);
    freeaddrinfo(res0);
    return conn;
}

static struct addrinfo *resolveAddress(const PSC_TcpClientOpts *opts)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG|AI_NUMERICSERV;
    char portstr[6];
    snprintf(portstr, 6, "%d", opts->port);
    struct addrinfo *res0;
    if (getaddrinfo(opts->remotehost, portstr, &hints, &res0) < 0 || !res0)
    {
	PSC_Log_fmt(PSC_L_ERROR, "client: error resolving %s",
		opts->remotehost);
	return 0;
    }
    return res0;
}

#ifdef WITH_TLS
static int fetchCert(X509 **cert, EVP_PKEY **key,
	const PSC_TcpClientOpts *opts)
{
    FILE *certfile = 0;
    FILE *keyfile = 0;
    *cert = 0;
    *key = 0;

    if (opts->tls_certfile && !opts->tls_keyfile)
    {
	PSC_Log_msg(PSC_L_ERROR,
		"client: certificate without private key, aborting");
	return -1;
    }
    if (opts->tls_keyfile && !opts->tls_certfile)
    {
	PSC_Log_msg(PSC_L_ERROR,
		"client: private key without certificate, aborting");
	return -1;
    }
    if (!opts->tls_certfile) return 0;
    if (!(certfile = fopen(opts->tls_certfile, "r")))
    {
	PSC_Log_fmt(PSC_L_ERROR,
		"client: cannot open certificate file `%s' for reading",
		opts->tls_certfile);
	goto error;
    }
    if (!(keyfile = fopen(opts->tls_keyfile, "r")))
    {
	PSC_Log_fmt(PSC_L_ERROR,
		"client: cannot open private key file `%s' for reading",
		opts->tls_keyfile);
	goto error;
    }
    if (!(*cert = PEM_read_X509(certfile, 0, 0, 0)))
    {
	PSC_Log_fmt(PSC_L_ERROR,
		"client: cannot read certificate from `%s'",
		opts->tls_certfile);
	goto error;
    }
    if (!(*key = PEM_read_PrivateKey(keyfile, 0, 0, 0)))
    {
	PSC_Log_fmt(PSC_L_ERROR,
		"client: cannot read private key from `%s'",
		opts->tls_keyfile);
	goto error;
    }
    fclose(keyfile);
    fclose(certfile);
    return 0;

error:
    EVP_PKEY_free(*key);
    X509_free(*cert);
    if (keyfile) fclose(keyfile);
    if (certfile) fclose(certfile);
    return -1;
}
#endif

static void doResolve(void *arg)
{
    ResolveJobData *data = arg;
#ifdef WITH_TLS
    if (fetchCert(&data->cert, &data->key, data->opts) < 0) return;
#endif
    data->res0 = resolveAddress(data->opts);
}

static void resolveDone(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    ResolveJobData *data = args;
    if (!data->res0)
    {
#ifdef WITH_TLS
	EVP_PKEY_free(data->key);
	X509_free(data->cert);
#endif
	data->callback(data->receiver, 0);
    }
#ifdef WITH_TLS
    else data->callback(data->receiver,
	    createFromAddrinfo(data->opts, data->res0,
		data->cert, data->key));
#else
    else data->callback(data->receiver,
	    createFromAddrinfo(data->opts, data->res0));
#endif
    PSC_TcpClientOpts_destroy(data->opts);
    free(data);
}

SOEXPORT PSC_TcpClientOpts *PSC_TcpClientOpts_create(
	const char *remotehost, int port)
{
    size_t remotehostsz = strlen(remotehost) + 1;
    PSC_TcpClientOpts *self = PSC_malloc(sizeof *self + remotehostsz);
    memset(self, 0, sizeof *self);
    self->rdbufsz = DEFRDBUFSZ;
    self->port = port;
    self->refcnt = 1;
    memcpy(self->remotehost, remotehost, remotehostsz);
    return self;
}

SOEXPORT void PSC_TcpClientOpts_readBufSize(PSC_TcpClientOpts *self,
	size_t sz)
{
    if (!sz) return;
    self->rdbufsz = sz;
}

SOEXPORT void PSC_TcpClientOpts_enableTls(PSC_TcpClientOpts *self,
	const char *certfile, const char *keyfile)
{
#ifdef WITH_TLS
    self->tls = 1;
    free(self->tls_certfile);
    self->tls_certfile = PSC_copystr(certfile);
    free(self->tls_keyfile);
    self->tls_keyfile = PSC_copystr(keyfile);
#else
    (void)certfile;
    (void)keyfile;
    PSC_Service_panic("This version of libposercore does not support TLS!");
#endif
}

SOEXPORT void PSC_TcpClientOpts_disableCertVerify(PSC_TcpClientOpts *self)
{
#ifdef WITH_TLS
    self->noverify = 1;
#else
    PSC_Service_panic("This version of libposercore does not support TLS!");
#endif
}

SOEXPORT void PSC_TcpClientOpts_setProto(PSC_TcpClientOpts *self,
	PSC_Proto proto)
{
    self->proto = proto;
}

SOEXPORT void PSC_TcpClientOpts_numericHosts(PSC_TcpClientOpts *self)
{
    self->numerichosts = 1;
}

SOEXPORT void PSC_TcpClientOpts_setBlacklistHits(PSC_TcpClientOpts *self,
	int blacklistHits)
{
    self->blacklisthits = blacklistHits;
}

SOEXPORT void PSC_TcpClientOpts_destroy(PSC_TcpClientOpts *self)
{
    if (!self) return;
    if (--self->refcnt) return;
#ifdef WITH_TLS
    free(self->tls_certfile);
    free(self->tls_keyfile);
#endif
    free(self);
}

SOEXPORT PSC_UnixClientOpts *PSC_UnixClientOpts_create(const char *sockname)
{
    size_t socknamesz = strlen(sockname) + 1;
    PSC_UnixClientOpts *self = PSC_malloc(sizeof *self + socknamesz);
    self->rdbufsz = DEFRDBUFSZ;
    memcpy(self->sockname, sockname, socknamesz);
    return self;
}

SOEXPORT void PSC_UnixClientOpts_readBufSize(PSC_UnixClientOpts *self,
	size_t sz)
{
    if (!sz) return;
    self->rdbufsz = sz;
}

SOEXPORT void PSC_UnixClientOpts_destroy(PSC_UnixClientOpts *self)
{
    free(self);
}

SOEXPORT PSC_Connection *PSC_Connection_createTcpClient(
	const PSC_TcpClientOpts *opts)
{
    struct addrinfo *res0;
#ifdef WITH_TLS
    X509 *cert;
    EVP_PKEY *key;
    if (fetchCert(&cert, &key, opts) < 0) return 0;
    if (!(res0 = resolveAddress(opts)))
    {
	EVP_PKEY_free(key);
	X509_free(cert);
	return 0;
    }
    return createFromAddrinfo(opts, res0, cert, key);
#else
    if (!(res0 = resolveAddress(opts))) return 0;
    return createFromAddrinfo(opts, res0);
#endif
}

SOEXPORT int PSC_Connection_createTcpClientAsync(const PSC_TcpClientOpts *opts,
	void *receiver, PSC_ClientCreatedHandler callback)
{
    if (!PSC_ThreadPool_active())
    {
	PSC_Log_msg(PSC_L_ERROR,
		"client: async creation requires active ThreadPool");
	return -1;
    }

    ResolveJobData *data = PSC_malloc(sizeof *data);
    data->res0 = 0;
    data->receiver = receiver;
    data->callback = callback;
    data->opts = (PSC_TcpClientOpts *)opts;
    ++data->opts->refcnt;
    PSC_ThreadJob *resolveJob = PSC_ThreadJob_create(doResolve, data, 0);
    PSC_Event_register(PSC_ThreadJob_finished(resolveJob), 0, resolveDone, 0);
    PSC_ThreadPool_enqueue(resolveJob);
    return 0;
}

SOEXPORT PSC_Connection *PSC_Connection_createUnixClient(
	const PSC_UnixClientOpts *opts)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "client: cannot create socket");
	return 0;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, opts->sockname, sizeof addr.sun_path - 1);

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    errno = 0;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0
	    && errno != EINPROGRESS)
    {
	PSC_Log_fmt(PSC_L_ERROR, "client: error connecting to `%s'",
		addr.sun_path);
	close(fd);
	return 0;
    }
    ConnOpts copts = {
	.rdbufsz = opts->rdbufsz,
	.createmode = CCM_CONNECTING
    };
    PSC_Connection *conn = PSC_Connection_create(fd, &copts);
    PSC_Connection_setRemoteAddrStr(conn, addr.sun_path);
    return conn;
}

