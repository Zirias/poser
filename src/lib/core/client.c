#define _DEFAULT_SOURCE

#include "connection.h"

#include <poser/core/client.h>
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
#include <unistd.h>

#ifdef WITH_TLS
#include <openssl/pem.h>
#endif

#define BLACKLISTSIZE 32

struct PSC_TcpClientOpts
{
    char *remotehost;
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
	PSC_Log_fmt(PSC_L_ERROR, "client: cannot connect to `%s'",
		opts->remotehost);
	return 0;
    }
    ConnOpts copts = {
#ifdef WITH_TLS
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
static void fetchCert(X509 **cert, EVP_PKEY **key,
	const PSC_TcpClientOpts *opts)
{
    FILE *certfile = 0;
    FILE *keyfile = 0;
    *cert = 0;
    *key = 0;

    if (opts->tls_certfile && !opts->tls_keyfile)
    {
	PSC_Log_msg(PSC_L_ERROR,
		"client: certificate without private key, ignoring");
	return;
    }
    if (opts->tls_keyfile && !opts->tls_certfile)
    {
	PSC_Log_msg(PSC_L_ERROR,
		"client: private key without certificate, ignoring");
	return;
    }
    if (!opts->tls_certfile) return;
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
    return;

error:
    EVP_PKEY_free(*key);
    X509_free(*cert);
    if (keyfile) fclose(keyfile);
    if (certfile) fclose(certfile);
}
#endif

static void doResolve(void *arg)
{
    ResolveJobData *data = arg;
    data->res0 = resolveAddress(data->opts);
#ifdef WITH_TLS
    fetchCert(&data->cert, &data->key, data->opts);
#endif
}

static void resolveDone(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    ResolveJobData *data = args;
    if (!data->res0) data->callback(data->receiver, 0);
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
    PSC_TcpClientOpts *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->remotehost = PSC_copystr(remotehost);
    self->port = port;
    self->refcnt = 1;
    return self;
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
    free(self->remotehost);
#ifdef WITH_TLS
    free(self->tls_certfile);
    free(self->tls_keyfile);
#endif
    free(self);
}

SOEXPORT PSC_Connection *PSC_Connection_createTcpClient(
	const PSC_TcpClientOpts *opts)
{
    struct addrinfo *res0 = resolveAddress(opts);
    if (!res0) return 0;
#ifdef WITH_TLS
    X509 *cert;
    EVP_PKEY *key;
    fetchCert(&cert, &key, opts);
    return createFromAddrinfo(opts, res0, cert, key);
#else
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

