#define _GNU_SOURCE

#include "certinfo.h"
#include "connection.h"

#include <poser/core/event.h>
#include <poser/core/log.h>
#include <poser/core/service.h>
#include <poser/core/server.h>
#include <poser/core/util.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef WITH_TLS
#include <openssl/pem.h>
#include <openssl/ssl.h>
#endif

#ifndef MAXSOCKS
#define MAXSOCKS 64
#endif

#define BINDCHUNK 8
#define CONNCHUNK 8

#ifdef WITH_TLS
enum ccertmode
{
    TCCM_NONE,
    TCCM_ENABLE,
    TCCM_REQUIRE
};
#endif

struct PSC_TcpServerOpts
{
    char **bindhosts;
#ifdef WITH_TLS
    char *certfile;
    char *keyfile;
    char *cafile;
    void *validatorObj;
    PSC_CertValidator validator;
#endif
    size_t bh_capa;
    size_t bh_count;
    size_t rdbufsz;
    PSC_Proto proto;
#ifdef WITH_TLS
    int tls;
    enum ccertmode tls_client_cert;
#endif
    int port;
};

struct PSC_UnixServerOpts
{
    char *name;
    size_t rdbufsz;
    int uid;
    int gid;
    int mode;
};

static char hostbuf[NI_MAXHOST];
static char servbuf[NI_MAXSERV];

static struct sockaddr_in sain;
static struct sockaddr_in6 sain6;

#ifdef WITH_TLS
static int have_ctx_idx;
static int ctx_idx;

enum tlslevel
{
    TL_NONE,
    TL_NORMAL,
    TL_CLIENTCA
};
#endif

enum saddrt
{
    ST_UNIX,
    ST_INET,
    ST_INET6
};

typedef struct SockInfo
{
    int fd;
    enum saddrt st;
} SockInfo;

struct PSC_Server
{
    PSC_Event *clientConnected;
    PSC_Event *clientDisconnected;
    PSC_Connection **conn;
    char *path;
#ifdef WITH_TLS
    X509 *cert;
    EVP_PKEY *key;
    SSL_CTX *tls_ctx;
    void *validatorObj;
    PSC_CertValidator validator;
#endif
    size_t conncapa;
    size_t connsize;
    size_t nsocks;
    size_t rdbufsz;
    int disabled;
#ifdef WITH_TLS
    enum tlslevel tls;
#endif
    SockInfo socks[];
};

static void acceptConnection(void *receiver, void *sender, void *args);
static void removeConnection(void *receiver, void *sender, void *args);

#ifdef WITH_TLS
static int ctxverifycallback(int preverify_ok, X509_STORE_CTX *ctx)
{
    if (X509_STORE_CTX_get_error_depth(ctx) > 0) return preverify_ok;

    PSC_Server *self = SSL_CTX_get_ex_data(SSL_get_SSL_CTX(
		X509_STORE_CTX_get_ex_data(ctx,
		    SSL_get_ex_data_X509_STORE_CTX_idx())), ctx_idx);

    if (self->tls == TL_CLIENTCA && !preverify_ok) return 0;

    X509 *cert = X509_STORE_CTX_get_current_cert(ctx);
    PSC_CertInfo *ci = PSC_CertInfo_create(cert);
    int ok = self->validator(self->validatorObj, ci);
    PSC_CertInfo_destroy(ci);

    return ok;
}
#endif

static void removeConnection(void *receiver, void *sender, void *args)
{
    (void)args;

    PSC_Server *self = receiver;
    PSC_Connection *conn = sender;
    for (size_t pos = 0; pos < self->connsize; ++pos)
    {
	if (self->conn[pos] == conn)
	{
	    PSC_Log_fmt(PSC_L_DEBUG, "server: client disconnected from %s",
		    PSC_Connection_remoteAddr(conn));
	    memmove(self->conn+pos, self->conn+pos+1,
		    (self->connsize - pos) * sizeof *self->conn);
	    --self->connsize;
	    PSC_Event_raise(self->clientDisconnected, 0, conn);
	    return;
	}
    }
    PSC_Log_msg(PSC_L_ERROR,
	    "server: trying to remove non-existing connection");
}

static void acceptConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Server *self = receiver;
    int *sockfd = args;
    enum saddrt st = ST_UNIX;
    for (size_t n = 0; n < self->nsocks; ++n)
    {
	if (self->socks[n].fd == *sockfd)
	{
	    st = self->socks[n].st;
	    break;
	}
    }
    socklen_t salen;
    struct sockaddr *sa = 0;
    socklen_t *sl = 0;
    if (st == ST_INET)
    {
	sa = (struct sockaddr *)&sain;
	salen = sizeof sain;
	sl = &salen;
    }
    else if (st == ST_INET6)
    {
	sa = (struct sockaddr *)&sain6;
	salen = sizeof sain6;
	sl = &salen;
    }
#ifdef HAVE_ACCEPT4
    int connfd = accept4(*sockfd, sa, sl, SOCK_NONBLOCK|SOCK_CLOEXEC);
#else
    int connfd = accept(*sockfd, sa, sl);
#endif
    if (connfd < 0)
    {
	PSC_Log_msg(PSC_L_WARNING, "server: failed to accept connection");
	return;
    }
#ifndef HAVE_ACCEPT4
    fcntl(connfd, F_SETFD, FD_CLOEXEC);
    fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (self->disabled || !PSC_Service_isValidFd(connfd, "server"))
    {
	struct linger l = { 1, 0 };
	setsockopt(connfd, SOL_SOCKET, SO_LINGER, &l, sizeof l);
	close(connfd);
	if (self->disabled) PSC_Log_msg(PSC_L_DEBUG,
		"server: rejected connection while disabled");
	return;
    }
    if (self->connsize == self->conncapa)
    {
	self->conncapa += CONNCHUNK;
	self->conn = PSC_realloc(self->conn,
		self->conncapa * sizeof *self->conn);
    }
    ConnOpts co = {
	.rdbufsz = self->rdbufsz,
#ifdef WITH_TLS
	.tls_ctx = self->tls_ctx,
	.tls_cert = self->cert,
	.tls_key = self->key,
	.tls_mode = self->tls != TL_NONE ? TM_SERVER : TM_NONE,
#endif
	.createmode = CCM_NORMAL
    };
#ifdef WITH_TLS
    if (self->tls != TL_NONE)
    {
	X509_up_ref(self->cert);
	EVP_PKEY_up_ref(self->key);
    }
#endif
    PSC_Connection *newconn = PSC_Connection_create(connfd, &co);
    self->conn[self->connsize++] = newconn;
    PSC_Event_register(PSC_Connection_closed(newconn), self,
	    removeConnection, 0);
    if (self->path)
    {
	PSC_Connection_setRemoteAddrStr(newconn, self->path);
    }
    else if (sa)
    {
	PSC_Connection_setRemoteAddr(newconn, sa);
    }
    PSC_Log_fmt(PSC_L_DEBUG, "server: client connected from %s",
	    PSC_Connection_remoteAddr(newconn));
    PSC_Event_raise(self->clientConnected, 0, newconn);
}

static PSC_Server *PSC_Server_create(const PSC_TcpServerOpts *opts,
	size_t nsocks, SockInfo *socks, char *path)
{
#ifdef WITH_TLS
    FILE *certfile = 0;
    FILE *keyfile = 0;
    X509 *cert = 0;
    EVP_PKEY *key = 0;
    SSL_CTX *tls_ctx = 0;
#endif
    if (nsocks < 1) goto error;
#ifdef WITH_TLS
    if (opts->tls)
    {
	if (!(tls_ctx = SSL_CTX_new(TLS_server_method())))
	{
	    PSC_Log_msg(PSC_L_ERROR, "server: error creating TLS context");
	    goto error;
	}
	SSL_CTX_set_min_proto_version(tls_ctx, TLS1_2_VERSION);
	if (opts->tls_client_cert != TCCM_NONE)
	{
	    int vmode = SSL_VERIFY_PEER;
	    if (opts->tls_client_cert == TCCM_REQUIRE)
	    {
		vmode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
	    }
	    if (opts->validator)
	    {
		if (!have_ctx_idx)
		{
		    ctx_idx = SSL_CTX_get_ex_new_index(0, 0, 0, 0, 0);
		    have_ctx_idx = 1;
		}
		SSL_CTX_set_verify(tls_ctx, vmode, ctxverifycallback);
	    }
	    else SSL_CTX_set_verify(tls_ctx, vmode, 0);
	    if (opts->cafile)
	    {
		if (!SSL_CTX_load_verify_locations(tls_ctx, opts->cafile, 0))
		{
		    PSC_Log_fmt(PSC_L_ERROR, "server: cannot load CA "
			    "certificate(s) from `%s'", opts->cafile);
		    goto error;
		}
	    }
	}
	if (!(certfile = fopen(opts->certfile, "r")))
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot open certificate file `%s' for reading",
		    opts->certfile);
	    goto error;
	}
	if (!(keyfile = fopen(opts->keyfile, "r")))
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot open private key file `%s' for reading",
		    opts->keyfile);
	    goto error;
	}
	if (!(cert = PEM_read_X509(certfile, 0, 0, 0)))
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot read certificate from `%s'",
		    opts->certfile);
	    goto error;
	}
	if (!(key = PEM_read_PrivateKey(keyfile, 0, 0, 0)))
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot read private key from `%s'",
		    opts->keyfile);
	    goto error;
	}
	fclose(keyfile);
	fclose(certfile);
	PSC_Log_fmt(PSC_L_INFO,
		"server: using certificate `%s'", opts->certfile);
    }
#endif
    PSC_Server *self = PSC_malloc(sizeof *self + nsocks * sizeof *socks);
    self->clientConnected = PSC_Event_create(self);
    self->clientDisconnected = PSC_Event_create(self);
    self->conn = PSC_malloc(CONNCHUNK * sizeof *self->conn);
    self->path = path;
    self->conncapa = CONNCHUNK;
    self->connsize = 0;
    self->rdbufsz = opts->rdbufsz;
    self->disabled = 0;
#ifdef WITH_TLS
    self->tls = opts->tls ? (opts->cafile ? TL_CLIENTCA : TL_NORMAL) : TL_NONE;
    self->cert = cert;
    self->key = key;
    self->tls_ctx = tls_ctx;
    self->validatorObj = opts->validatorObj;
    self->validator = opts->validator;
    if (self->validator) SSL_CTX_set_ex_data(tls_ctx, ctx_idx, self);
#endif
    self->nsocks = nsocks;
    memcpy(self->socks, socks, nsocks * sizeof *socks);
    for (size_t i = 0; i < nsocks; ++i)
    {
	PSC_Event_register(PSC_Service_readyRead(), self,
		acceptConnection, socks[i].fd);
	PSC_Service_registerRead(socks[i].fd);
    }

    return self;

error:
#ifdef WITH_TLS
    SSL_CTX_free(tls_ctx);
    EVP_PKEY_free(key);
    X509_free(cert);
    if (keyfile) fclose(keyfile);
    if (certfile) fclose(certfile);
#endif
    free(path);
    return 0;
}

SOEXPORT PSC_TcpServerOpts *PSC_TcpServerOpts_create(int port)
{
    PSC_TcpServerOpts *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->rdbufsz = DEFRDBUFSZ;
    self->port = port;
    return self;
}

SOEXPORT void PSC_TcpServerOpts_bind(PSC_TcpServerOpts *self,
	const char *bindhost)
{
    if (self->bh_count == self->bh_capa)
    {
	self->bh_capa += BINDCHUNK;
	self->bindhosts = PSC_realloc(self->bindhosts,
		self->bh_capa * sizeof *self->bindhosts);
    }
    self->bindhosts[self->bh_count++] = PSC_copystr(bindhost);
}

SOEXPORT void PSC_TcpServerOpts_readBufSize(PSC_TcpServerOpts *self,
	size_t sz)
{
    if (!sz) return;
    self->rdbufsz = sz;
}

SOEXPORT void PSC_TcpServerOpts_enableTls(PSC_TcpServerOpts *self,
	const char *certfile, const char *keyfile)
{
#ifdef WITH_TLS
    self->tls = 1;
    free(self->certfile);
    self->certfile = PSC_copystr(certfile);
    free(self->keyfile);
    self->keyfile = PSC_copystr(keyfile);
#else
    (void)self;
    (void)certfile;
    (void)keyfile;
    PSC_Service_panic("This version of libposercore does not support TLS!");
#endif
}

SOEXPORT void PSC_TcpServerOpts_enableClientCert(PSC_TcpServerOpts *self,
	const char *cafile)
{
#ifdef WITH_TLS
    self->tls_client_cert = TCCM_ENABLE;
    free(self->cafile);
    self->cafile = PSC_copystr(cafile);
#else
    (void)self;
    (void)cafile;
    PSC_Service_panic("This version of libposercore does not support TLS!");
#endif
}

SOEXPORT void PSC_TcpServerOpts_requireClientCert(PSC_TcpServerOpts *self,
	const char *cafile)
{
#ifdef WITH_TLS
    self->tls_client_cert = TCCM_REQUIRE;
    free(self->cafile);
    self->cafile = PSC_copystr(cafile);
#else
    (void)self;
    (void)cafile;
    PSC_Service_panic("This version of libposercore does not support TLS!");
#endif
}

SOEXPORT void PSC_TcpServerOpts_validateClientCert(PSC_TcpServerOpts *self,
	void *receiver, PSC_CertValidator validator)
{
#ifdef WITH_TLS
    self->validatorObj = receiver;
    self->validator = validator;
#else
    (void)self;
    (void)receiver;
    (void)validator;
    PSC_Service_panic("This version of libposercore does not support TLS!");
#endif
}

SOEXPORT void PSC_TcpServerOpts_setProto(PSC_TcpServerOpts *self,
	PSC_Proto proto)
{
    self->proto = proto;
}

SOEXPORT void PSC_TcpServerOpts_numericHosts(PSC_TcpServerOpts *self)
{
    (void) self;
}

SOEXPORT void PSC_TcpServerOpts_destroy(PSC_TcpServerOpts *self)
{
    if (!self) return;
    for (size_t i = 0; i < self->bh_count; ++i) free(self->bindhosts[i]);
    free(self->bindhosts);
#ifdef WITH_TLS
    free(self->cafile);
    free(self->certfile);
    free(self->keyfile);
#endif
    free(self);
}

SOEXPORT PSC_UnixServerOpts *PSC_UnixServerOpts_create(const char *name)
{
    PSC_UnixServerOpts *self = PSC_malloc(sizeof *self);
    self->name = PSC_copystr(name);
    self->rdbufsz = DEFRDBUFSZ;
    self->uid = -1;
    self->gid = -1;
    self->mode = 0600;
    return self;
}

SOEXPORT void PSC_UnixServerOpts_readBufSize(PSC_UnixServerOpts *self,
	size_t sz)
{
    if (!sz) return;
    self->rdbufsz = sz;
}

SOEXPORT void PSC_UnixServerOpts_owner(PSC_UnixServerOpts *self,
	int uid, int gid)
{
    self->uid = uid;
    self->gid = gid;
}

SOEXPORT void PSC_UnixServerOpts_mode(PSC_UnixServerOpts *self, int mode)
{
    self->mode = mode;
}

SOEXPORT void PSC_UnixServerOpts_destroy(PSC_UnixServerOpts *self)
{
    if (!self) return;
    free(self->name);
    free(self);
}

SOEXPORT PSC_Server *PSC_Server_createTcp(const PSC_TcpServerOpts *opts)
{
    SockInfo socks[MAXSOCKS];

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG|AI_NUMERICSERV;
    char portstr[6];
    snprintf(portstr, 6, "%d", opts->port);

    struct addrinfo *res0;
    size_t nsocks = 0;
    size_t bi = 0;
    int opt_true = 1;
    do
    {
	res0 = 0;
	if (getaddrinfo(opts->bh_count ? opts->bindhosts[bi] : 0,
		    portstr, &hints, &res0) < 0 || !res0)
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot get address info for `%s'",
		    opts->bindhosts[bi]);
	    continue;
	}
	for (struct addrinfo *res = res0; res && nsocks < MAXSOCKS;
		res = res->ai_next)
	{
	    if (res->ai_family != AF_INET
		    && res->ai_family != AF_INET6) continue;
	    if (opts->proto == PSC_P_IPv4
		    && res->ai_family != AF_INET) continue;
	    if (opts->proto == PSC_P_IPv6
		    && res->ai_family != AF_INET6) continue;
#ifdef HAVE_ACCEPT4
	    socks[nsocks].fd = socket(res->ai_family,
		    res->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
		    res->ai_protocol);
#else
	    socks[nsocks].fd = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
#endif
	    if (socks[nsocks].fd < 0)
	    {
		PSC_Log_msg(PSC_L_ERROR, "server: cannot create socket");
		continue;
	    }
#ifndef HAVE_ACCEPT4
	    fcntl(socks[nsocks].fd, F_SETFD, FD_CLOEXEC);
	    fcntl(socks[nsocks].fd, F_SETFL,
		    fcntl(socks[nsocks].fd, F_GETFL, 0) | O_NONBLOCK);
#endif
	    if (!PSC_Service_isValidFd(socks[nsocks].fd, "server"))
	    {
		close(socks[nsocks].fd);
		break;
	    }

	    if (setsockopt(socks[nsocks].fd, SOL_SOCKET, SO_REUSEADDR,
			&opt_true, sizeof opt_true) < 0)
	    {
		PSC_Log_msg(PSC_L_ERROR, "server: cannot set socket option");
		close(socks[nsocks].fd);
		continue;
	    }
#ifdef IPV6_V6ONLY
	    if (res->ai_family == AF_INET6)
	    {
		setsockopt(socks[nsocks].fd, IPPROTO_IPV6, IPV6_V6ONLY,
			&opt_true, sizeof opt_true);
	    }
#endif
	    if (bind(socks[nsocks].fd, res->ai_addr, res->ai_addrlen) < 0)
	    {
		PSC_Log_msg(PSC_L_ERROR,
			"server: cannot bind to specified address");
		close(socks[nsocks].fd);
		continue;
	    }
	    if (listen(socks[nsocks].fd, 8) < 0)
	    {   
		PSC_Log_msg(PSC_L_ERROR, "server: cannot listen on socket");
		close(socks[nsocks].fd);
		continue;
	    }
	    const char *addrstr = "<unknown>";
	    if (getnameinfo(res->ai_addr, res->ai_addrlen,
			hostbuf, sizeof hostbuf,
			servbuf, sizeof servbuf,
			NI_NUMERICHOST|NI_NUMERICSERV) >= 0)
	    {
		addrstr = hostbuf;
	    }
	    PSC_Log_fmt(PSC_L_INFO, "server: listening on %s port %s",
		    addrstr, portstr);
	    socks[nsocks++].st = res->ai_family == AF_INET ?
		ST_INET : ST_INET6;
	}
	freeaddrinfo(res0);
    } while (++bi < opts->bh_count);
    if (!nsocks)
    {
	PSC_Log_msg(PSC_L_FATAL, "server: could not create any sockets for "
		"listening to incoming connections");
	return 0;
    }
    
    PSC_Server *self = PSC_Server_create(opts, nsocks, socks, 0);
    return self;
}

SOEXPORT PSC_Server *PSC_Server_createUnix(const PSC_UnixServerOpts *opts)
{
    SockInfo sock = {
#ifdef HAVE_ACCEPT4
	.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0),
#else
	.fd = socket(AF_UNIX, SOCK_STREAM, 0),
#endif
	.st = ST_UNIX
    };
    if (sock.fd < 0)
    {
        PSC_Log_msg(PSC_L_ERROR, "server: cannot create socket");
        return 0;
    }
#ifndef HAVE_ACCEPT4
    fcntl(sock.fd, F_SETFD, FD_CLOEXEC);
    fcntl(sock.fd, F_SETFL, fcntl(sock.fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (!PSC_Service_isValidFd(sock.fd, "server"))
    {
	close(sock.fd);
	return 0;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, opts->name, sizeof addr.sun_path - 1);

    struct stat st;
    errno = 0;
    if (stat(addr.sun_path, &st) >= 0)
    {
        if (!S_ISSOCK(st.st_mode))
        {
            PSC_Log_fmt(PSC_L_ERROR, "server: `%s' exists and is not a socket",
                    addr.sun_path);
            close(sock.fd);
            return 0;
        }

	fd_set wfds;
	FD_ZERO(&wfds);
	FD_SET(sock.fd, &wfds);
	struct timeval tv;
	memset(&tv, 0, sizeof tv);
	tv.tv_usec = 300000U;
	int sockerr = 0;
	socklen_t sockerrlen = sizeof sockerr;
	errno = 0;
	if (connect(sock.fd, (struct sockaddr *)&addr, sizeof addr) >= 0
		|| (errno == EINPROGRESS
		    && select(sock.fd + 1, 0, &wfds, 0, &tv) > 0
		    && getsockopt(sock.fd, SOL_SOCKET, SO_ERROR,
			&sockerr, &sockerrlen) >= 0
		    && !sockerr))
        {
            PSC_Log_fmt(PSC_L_WARNING,
		    "server: `%s' is already opened for listening",
		    addr.sun_path);
            close(sock.fd);
            return 0;
        }
	close(sock.fd);

        if (unlink(addr.sun_path) < 0)
        {
            PSC_Log_fmt(PSC_L_ERROR, "server: cannot remove stale socket `%s'",
                    addr.sun_path);
            return 0;
        }

        PSC_Log_fmt(PSC_L_WARNING, "server: removed stale socket `%s'",
                addr.sun_path);
#ifdef HAVE_ACCEPT4
	sock.fd = socket(AF_UNIX,
		SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
	sock.fd = socket(AF_UNIX, SOCK_STREAM, 0);
	fcntl(sock.fd, F_SETFD, FD_CLOEXEC);
	fcntl(sock.fd, F_SETFL, fcntl(sock.fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    }
    else if (errno != ENOENT)
    {
        PSC_Log_fmt(PSC_L_ERROR, "server: cannot access `%s'", addr.sun_path);
        close(sock.fd);
        return 0;
    }

    if (bind(sock.fd, (struct sockaddr *)&addr, sizeof addr) < 0)
    {
        PSC_Log_fmt(PSC_L_ERROR, "server: cannot bind to `%s'", addr.sun_path);
        close(sock.fd);
        return 0;
    }

    if (listen(sock.fd, 8) < 0)
    {
        PSC_Log_fmt(PSC_L_ERROR, "server: cannot listen on `%s'",
		addr.sun_path);
        close(sock.fd);
	unlink(addr.sun_path);
        return 0;
    }
    PSC_Log_fmt(PSC_L_INFO, "server: listening on %s", addr.sun_path);

    if (chmod(addr.sun_path, opts->mode) < 0)
    {
	PSC_Log_fmt(PSC_L_ERROR,
		"server: cannot set desired socket permissions");
    }
    if (opts->uid != -1 || opts->gid != -1)
    {
	if (chown(addr.sun_path, opts->uid, opts->gid) < 0)
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot set desired socket ownership");
	}
    }
    PSC_TcpServerOpts tcpopts = {
	.rdbufsz = opts->rdbufsz
    };
    PSC_Server *self = PSC_Server_create(&tcpopts, 1, &sock,
	    PSC_copystr(addr.sun_path));
    return self;
}

SOEXPORT PSC_Event *PSC_Server_clientConnected(PSC_Server *self)
{
    return self->clientConnected;
}

SOEXPORT PSC_Event *PSC_Server_clientDisconnected(PSC_Server *self)
{
    return self->clientDisconnected;
}

SOEXPORT void PSC_Server_disable(PSC_Server *self)
{
    self->disabled = 1;
}

SOEXPORT void PSC_Server_enable(PSC_Server *self)
{
    self->disabled = 0;
}

SOEXPORT void PSC_Server_destroy(PSC_Server *self)
{
    if (!self) return;

    for (size_t pos = 0; pos < self->connsize; ++pos)
    {
	PSC_Event_raise(self->clientDisconnected, 0, self->conn[pos]);
	PSC_Connection_destroy(self->conn[pos]);
    }
    free(self->conn);
    for (uint8_t i = 0; i < self->nsocks; ++i)
    {
	PSC_Service_unregisterRead(self->socks[i].fd);
	PSC_Event_unregister(PSC_Service_readyRead(), self, acceptConnection,
		self->socks[i].fd);
	close(self->socks[i].fd);
    }
    PSC_Event_destroy(self->clientDisconnected);
    PSC_Event_destroy(self->clientConnected);
    if (self->path)
    {
	unlink(self->path);
	free(self->path);
    }
#ifdef WITH_TLS
    EVP_PKEY_free(self->key);
    X509_free(self->cert);
    SSL_CTX_free(self->tls_ctx);
#endif
    free(self);
}

