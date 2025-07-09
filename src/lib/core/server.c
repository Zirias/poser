#define _GNU_SOURCE

#include "certinfo.h"
#include "connection.h"
#include "ipaddr.h"
#include "sharedobj.h"

#include <poser/core/event.h>
#include <poser/core/hash.h>
#include <poser/core/log.h>
#include <poser/core/service.h>
#include <poser/core/server.h>
#include <poser/core/timer.h>
#include <poser/core/util.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <semaphore.h>
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
#  include <openssl/pem.h>
#  include <openssl/ssl.h>
#endif

#ifdef NO_SHAREDOBJ
#  include <pthread.h>
#endif

#ifndef MAXSOCKS
#define MAXSOCKS 64
#endif

#define BINDCHUNK 8

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

typedef struct TlsConfig
{
#  ifndef NO_SHAREDOBJ
    SharedObj base;
#  endif
    PSC_CertValidator validator;
    void *validatorObj;
    SSL_CTX *tls_ctx;
    enum tlslevel tls;
} TlsConfig;
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

typedef struct AcceptRecord
{
    PSC_Server *srv;
    PSC_IpAddr *addr;
    const char *path;
    ConnOpts opts;
    int fd;
} AcceptRecord;

typedef struct ThreadRecord
{
#ifdef NO_SHAREDOBJ
    size_t nactive;
#else
    atomic_size_t nactive;
#endif
    ObjectPool *pool;
} ThreadRecord;

struct PSC_Server
{
    void *owner;
    PSC_ClientConnectedCallback clientConnected;
    void (*shutdownComplete)(void *);
    PSC_Timer *shutdownTimer;
    ThreadRecord *clients;
    char *path;
#ifdef NO_SHAREDOBJ
#  ifdef WITH_TLS
    TlsConfig *tlscfg;
    pthread_mutex_t tlslock;
#  endif
    pthread_mutex_t lock;
    size_t nconn;
#else
#  ifdef WITH_TLS
    TlsConfig *_Atomic tlscfg;
#  endif
    atomic_size_t nconn;
#endif
    sem_t allclosed;
    uint64_t bhash;
    size_t nsocks;
    size_t rdbufsz;
    PSC_Proto proto;
    int port;
    int disabled;
    int nthr;
    int nextthr;
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

#  ifdef NO_SHAREDOBJ
    pthread_mutex_lock(&self->tlslock);
    TlsConfig *tlscfg = self->tlscfg;
#  else
    TlsConfig *tlscfg = SOM_reserve((void *_Atomic *)&self->tlscfg);
#  endif

    int ok = 0;
    if (tlscfg->tls == TL_CLIENTCA && !preverify_ok) goto done;

    X509 *cert = X509_STORE_CTX_get_current_cert(ctx);
    PSC_CertInfo *ci = PSC_CertInfo_create(cert);
    ok = tlscfg->validator(tlscfg->validatorObj, ci);
    PSC_CertInfo_destroy(ci);

done:
#  ifdef NO_SHAREDOBJ
    pthread_mutex_unlock(&self->tlslock);
#  else
    SOM_release();
#  endif
    return ok;
}
#endif

static void removeConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Server *self = receiver;

    int thrno = PSC_Service_threadNo();
#ifdef NO_SHAREDOBJ
    pthread_mutex_lock(&self->lock);
    if (thrno >= 0) --self->clients[thrno].nactive;
    if (!--self->nconn) sem_post(&self->allclosed);
    pthread_mutex_unlock(&self->lock);
#else
    if (thrno >= 0) atomic_fetch_sub_explicit(&self->clients[thrno].nactive,
	    1, memory_order_acq_rel);
    if (atomic_fetch_sub_explicit(&self->nconn, 1, memory_order_acq_rel) == 1)
    {
	sem_post(&self->allclosed);
    }
#endif
}

static void doaccept(void *arg)
{
    AcceptRecord *rec = arg;
#ifdef WITH_TLS
#  ifdef NO_SHAREDOBJ
    pthread_mutex_lock(&rec->srv->tlslock);
    TlsConfig *tlscfg = rec->srv->tlscfg;
#  else
    TlsConfig *tlscfg = SOM_reserve((void *_Atomic *)&rec->srv->tlscfg);
#  endif
    rec->opts.tls_ctx = tlscfg->tls_ctx;
    rec->opts.tls_mode = tlscfg->tls != TL_NONE ? TM_SERVER : TM_NONE;
#endif
    PSC_Connection *newconn = PSC_Connection_create(rec->fd, &rec->opts);
#ifdef WITH_TLS
#  ifdef NO_SHAREDOBJ
    pthread_mutex_unlock(&rec->srv->tlslock);
#  else
    SOM_release();
#  endif
#endif
    if (!newconn)
    {
	int thrno = PSC_Service_threadNo();
	if (thrno >= 0)
	{
#ifdef NO_SHAREDOBJ
	    pthread_mutex_lock(&rec->srv->lock);
	    --rec->srv->clients[thrno].nactive;
	    pthread_mutex_unlock(&rec->srv->lock);
#else
	    atomic_fetch_sub_explicit(&rec->srv->clients[thrno].nactive, 1,
		    memory_order_acq_rel);
#endif
	}
	goto done;
    }
#ifdef NO_SHAREDOBJ
    pthread_mutex_lock(&rec->srv->lock);
    if (!rec->srv->nconn++) sem_wait(&rec->srv->allclosed);
    pthread_mutex_unlock(&rec->srv->lock);
#else
    if (atomic_fetch_add_explicit(&rec->srv->nconn, 1,
		memory_order_acq_rel) == 0)
    {
	sem_wait(&rec->srv->allclosed);
    }
#endif
    PSC_Event_register(PSC_Connection_closed(newconn), rec->srv,
	    removeConnection, 0);
    if (rec->path)
    {
	PSC_Connection_setRemoteAddrStr(newconn, rec->path);
    }
    else
    {
	PSC_Connection_setRemoteAddr(newconn, rec->addr);
    }
    PSC_Log_fmt(PSC_L_DEBUG, "server: client connected from %s",
	    PSC_Connection_remoteAddr(newconn));
    rec->srv->clientConnected(rec->srv->owner, newconn);
done:
    free(rec);
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
	PSC_Log_err(PSC_L_WARNING, "server: failed to accept connection");
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

    if (self->nthr < 0)
    {
	if ((self->nthr = PSC_Service_workers())) self->nextthr = 0;
	int npools = self->nthr;
	if (npools < 1) npools = 1;
	self->clients = PSC_malloc(npools * sizeof *self->clients);
	for (int i = 0; i < npools; ++i)
	{
	    self->clients[i].nactive = 0;
	    self->clients[i].pool = ObjectPool_create(
		    PSC_Connection_size(self->rdbufsz), 1024);
	}
    }

    if (self->nthr)
    {
	int nextthr = self->nextthr;
#ifdef NO_SHAREDOBJ
	pthread_mutex_lock(&self->lock);
	size_t minactive = self->clients[nextthr].nactive;
#else
	size_t minactive = atomic_load_explicit(
		&self->clients[nextthr].nactive, memory_order_relaxed);
#endif
	for (int i = 1; i < self->nthr; ++i)
	{
	    int thrno = (nextthr + i) % self->nthr;
#ifdef NO_SHAREDOBJ
	    size_t iactive = self->clients[thrno].nactive;
#else
	    size_t iactive = atomic_load_explicit(
		    &self->clients[thrno].nactive, memory_order_relaxed);
#endif
	    if (iactive < minactive)
	    {
		minactive = iactive;
		nextthr = thrno;
	    }
	}
#ifdef NO_SHAREDOBJ
	++self->clients[nextthr].nactive;
	pthread_mutex_unlock(&self->lock);
#else
	atomic_fetch_add_explicit(&self->clients[nextthr].nactive, 1,
		memory_order_acq_rel);
#endif
	self->nextthr = nextthr;
    }

    int poolno = self->nextthr;
    if (poolno < 0) poolno = 0;

    AcceptRecord *rec = PSC_malloc(sizeof *rec);
    memset(rec, 0, sizeof *rec);
    rec->srv = self;
    rec->addr = PSC_IpAddr_fromSockAddr(sa);
    rec->path = self->path;
    rec->opts.pool = self->clients[poolno].pool;
    rec->opts.rdbufsz = self->rdbufsz;
    rec->opts.createmode = CCM_NORMAL;
    rec->fd = connfd;

    PSC_Service_runOnThread(self->nextthr, doaccept, rec);
}

static int bindcmp(const void *a, const void *b)
{
    char *const *ba = a;
    char *const *bb = b;
    return strcmp(*ba, *bb);
}

static uint64_t bindhash(size_t nbinds, char **binds)
{
    if (!nbinds) return 0;
    char **blist = PSC_malloc((nbinds+1) * sizeof *blist);
    memcpy(blist, binds, nbinds * sizeof *blist);
    blist[nbinds] = 0;
    qsort(blist, nbinds, sizeof *blist, bindcmp);
    char *bjoined = PSC_joinstr(":", blist);
    free(blist);
    PSC_Hash *hasher = PSC_Hash_create(0, 0);
    uint64_t hash = PSC_Hash_string(hasher, bjoined);
    PSC_Hash_destroy(hasher);
    free(bjoined);
    return hash;
}

#ifdef WITH_TLS
static void destroyTlsCfg(void *obj)
{
    if (!obj) return;
    TlsConfig *self = obj;
    SSL_CTX_free(self->tls_ctx);
    free(self);
}

static TlsConfig *initTls(const PSC_TcpServerOpts *opts)
{
    FILE *certfile = 0;
    FILE *keyfile = 0;
    X509 *cert = 0;
    EVP_PKEY *key = 0;
    SSL_CTX *tls_ctx = 0;
    int fd = -1;
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
	if (!opts->certfile || !opts->keyfile)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "server: TLS requires both a server certificate file "
		    "and a corresponding key file");
	    goto error;
	}
	if ((fd = open(opts->certfile, O_RDONLY|O_CLOEXEC)) < 0 ||
		!(certfile = fdopen(fd, "r")))
	{
	    PSC_Log_errfmt(PSC_L_ERROR,
		    "server: cannot open certificate file `%s' for reading",
		    opts->certfile);
	    if (fd >= 0) close(fd);
	    goto error;
	}
	if ((fd = open(opts->keyfile, O_RDONLY|O_CLOEXEC)) < 0 ||
		!(keyfile = fdopen(fd, "r")))
	{
	    PSC_Log_errfmt(PSC_L_ERROR,
		    "server: cannot open private key file `%s' for reading",
		    opts->keyfile);
	    if (fd >= 0) close(fd);
	    goto error;
	}
	if (!(cert = PEM_read_X509(certfile, 0, 0, 0)))
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot read certificate from `%s'",
		    opts->certfile);
	    goto error;
	}
	if (!SSL_CTX_use_certificate(tls_ctx, cert))
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot use certificate from `%s'",
		    opts->certfile);
	    goto error;
	}
	X509_free(cert);
	while ((cert = PEM_read_X509(certfile, 0, 0, 0)))
	{
	    if (!SSL_CTX_add_extra_chain_cert(tls_ctx, cert))
	    {
		PSC_Log_fmt(PSC_L_ERROR, "server: cannot use intermediate "
			"certificate from `%s'", opts->certfile);
		goto error;
	    }
	}
	if (!(key = PEM_read_PrivateKey(keyfile, 0, 0, 0)))
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot read private key from `%s'",
		    opts->keyfile);
	    goto error;
	}
	if (!SSL_CTX_use_PrivateKey(tls_ctx, key))
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot read private key from `%s'",
		    opts->keyfile);
	    goto error;
	}
	EVP_PKEY_free(key);
	key = 0;
	fclose(keyfile);
	fclose(certfile);
	PSC_Log_fmt(PSC_L_INFO,
		"server: using certificate `%s'", opts->certfile);
    }

#  ifdef NO_SHAREDOBJ
    TlsConfig *tlscfg = PSC_malloc(sizeof *tlscfg);
#  else
    TlsConfig *tlscfg = SharedObj_create(sizeof *tlscfg, destroyTlsCfg);
#  endif
    tlscfg->validator = opts->validator;
    tlscfg->validatorObj = opts->validatorObj;
    tlscfg->tls_ctx = tls_ctx;
    tlscfg->tls = opts->tls
	? (opts->cafile ? TL_CLIENTCA : TL_NORMAL)
	: TL_NONE;
    return tlscfg;

error:
    SSL_CTX_free(tls_ctx);
    EVP_PKEY_free(key);
    X509_free(cert);
    if (keyfile) fclose(keyfile);
    if (certfile) fclose(certfile);
    return 0;
}
#endif

static PSC_Server *PSC_Server_create(const PSC_TcpServerOpts *opts,
	size_t nsocks, SockInfo *socks, char *path, void *owner,
	PSC_ClientConnectedCallback clientConnected,
	void (*shutdownComplete)(void *))
{
    if (nsocks < 1) goto error;
#ifdef WITH_TLS
    TlsConfig *tlscfg = initTls(opts);
    if (!tlscfg) goto error;
#endif
    PSC_Server *self = PSC_malloc(sizeof *self + nsocks * sizeof *socks);
    self->owner = owner;
    self->clientConnected = clientConnected;
    self->shutdownComplete = shutdownComplete;
    self->shutdownTimer = 0;
    self->clients = 0;
    self->path = path;
#ifdef NO_SHAREDOBJ
    pthread_mutex_init(&self->lock, 0);
#endif
    sem_init(&self->allclosed, 0, 1);
    self->bhash = bindhash(opts->bh_count, opts->bindhosts);
#ifdef NO_SHAREDOBJ
    self->nconn = 0;
#else
    atomic_store_explicit(&self->nconn, 0, memory_order_release);
#endif
    self->rdbufsz = opts->rdbufsz;
    self->proto = opts->proto;
    self->port = opts->port;
    self->disabled = 0;
    self->nthr = -1;
    self->nextthr = -1;
#ifdef WITH_TLS
#  ifdef NO_SHAREDOBJ
    pthread_mutex_init(&self->tlslock, 0);
    self->tlscfg = tlscfg;
#  else
    atomic_store_explicit(&self->tlscfg, tlscfg, memory_order_release);
#  endif
    if (tlscfg->tls_ctx && tlscfg->validator)
    {
	SSL_CTX_set_ex_data(tlscfg->tls_ctx, ctx_idx, self);
    }
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

SOEXPORT PSC_Server *PSC_Server_createTcp(const PSC_TcpServerOpts *opts,
	void *owner, PSC_ClientConnectedCallback clientConnected,
	void (*shutdownComplete)(void *))
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
	    PSC_Log_errfmt(PSC_L_ERROR,
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
		PSC_Log_err(PSC_L_ERROR, "server: cannot create socket");
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
		PSC_Log_err(PSC_L_ERROR, "server: cannot set socket option");
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
		PSC_Log_err(PSC_L_ERROR,
			"server: cannot bind to specified address");
		close(socks[nsocks].fd);
		continue;
	    }
	    if (listen(socks[nsocks].fd, 128) < 0)
	    {   
		PSC_Log_err(PSC_L_ERROR, "server: cannot listen on socket");
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
    
    PSC_Server *self = PSC_Server_create(opts, nsocks, socks, 0, owner,
	    clientConnected, shutdownComplete);
    if (!self) for (size_t i = 0; i < nsocks; ++i)
    {
	close(socks[i].fd);
    }
    return self;
}

SOEXPORT int PSC_Server_configureTcp(PSC_Server *self,
	const PSC_TcpServerOpts *opts)
{
    if (self->path) return -1;
    if (self->proto != opts->proto) return -1;
    if (self->port != opts->port) return -1;
    if (self->rdbufsz != opts->rdbufsz) return -1;
    if (self->bhash != bindhash(opts->bh_count, opts->bindhosts)) return -1;
#ifdef WITH_TLS
    TlsConfig *tlscfg = initTls(opts);
    if (!tlscfg) return -1;
    if (tlscfg->tls_ctx && tlscfg->validator)
    {
	SSL_CTX_set_ex_data(tlscfg->tls_ctx, ctx_idx, self);
    }
#  ifdef NO_SHAREDOBJ
    pthread_mutex_lock(&self->tlslock);
    destroyTlsCfg(self->tlscfg);
    self->tlscfg = tlscfg;
    pthread_mutex_unlock(&self->tlslock);
#  else
    TlsConfig *oldcfg = atomic_exchange_explicit(&self->tlscfg, tlscfg,
	    memory_order_acq_rel);
    SharedObj_retire(oldcfg);
#  endif
#endif
    return 0;
}

SOEXPORT PSC_Server *PSC_Server_createUnix(const PSC_UnixServerOpts *opts,
	void *owner, PSC_ClientConnectedCallback clientConnected,
	void (*shutdownComplete)(void *))
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
        PSC_Log_err(PSC_L_ERROR, "server: cannot create socket");
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
            PSC_Log_errfmt(PSC_L_ERROR,
		    "server: cannot remove stale socket `%s'", addr.sun_path);
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
        PSC_Log_errfmt(PSC_L_ERROR, "server: cannot access `%s'",
		addr.sun_path);
        close(sock.fd);
        return 0;
    }

    if (bind(sock.fd, (struct sockaddr *)&addr, sizeof addr) < 0)
    {
        PSC_Log_errfmt(PSC_L_ERROR, "server: cannot bind to `%s'",
		addr.sun_path);
        close(sock.fd);
        return 0;
    }

    if (listen(sock.fd, 8) < 0)
    {
        PSC_Log_errfmt(PSC_L_ERROR, "server: cannot listen on `%s'",
		addr.sun_path);
        close(sock.fd);
	unlink(addr.sun_path);
        return 0;
    }
    PSC_Log_fmt(PSC_L_INFO, "server: listening on %s", addr.sun_path);

    if (chmod(addr.sun_path, opts->mode) < 0)
    {
	PSC_Log_err(PSC_L_ERROR,
		"server: cannot set desired socket permissions");
    }
    if (opts->uid != -1 || opts->gid != -1)
    {
	if (chown(addr.sun_path, opts->uid, opts->gid) < 0)
	{
	    PSC_Log_err(PSC_L_ERROR,
		    "server: cannot set desired socket ownership");
	}
    }
    PSC_TcpServerOpts tcpopts = {
	.rdbufsz = opts->rdbufsz
    };
    PSC_Server *self = PSC_Server_create(&tcpopts, 1, &sock,
	    PSC_copystr(addr.sun_path), owner,
	    clientConnected, shutdownComplete);
    return self;
}

SOEXPORT void PSC_Server_disable(PSC_Server *self)
{
    self->disabled = 1;
}

SOEXPORT void PSC_Server_enable(PSC_Server *self)
{
    self->disabled = 0;
}

static void forceDestroy(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Server_destroy(receiver);
}

SOEXPORT size_t PSC_Server_connections(const PSC_Server *self)
{
#ifdef NO_SHAREDOBJ
    size_t n;
    pthread_mutex_lock(&((PSC_Server *)self)->lock);
    n = self->nconn;
    pthread_mutex_unlock(&((PSC_Server *)self)->lock);
    return n;
#else
    return atomic_load_explicit(&self->nconn, memory_order_acquire);
#endif
}

SOEXPORT void PSC_Server_shutdown(PSC_Server *self, unsigned timeout)
{
    if (!self) return;

    if (!self->nconn)
    {
	PSC_Server_destroy(self);
	return;
    }

    for (uint8_t i = 0; i < self->nsocks; ++i)
    {
	PSC_Service_unregisterRead(self->socks[i].fd);
	PSC_Event_unregister(PSC_Service_readyRead(), self, acceptConnection,
		self->socks[i].fd);
	close(self->socks[i].fd);
    }
    self->nsocks = 0;

    PSC_Event_register(PSC_Service_shutdown(), self, forceDestroy, 0);
    if (timeout)
    {
	self->shutdownTimer = PSC_Timer_create();
	if (!self->shutdownTimer)
	{
	    PSC_Log_msg(PSC_L_WARNING, "server: cannot create timer for "
		    "shutdown, closing all connections now.");
	    PSC_Server_destroy(self);
	    return;
	}
	PSC_Timer_setMs(self->shutdownTimer, timeout);
	PSC_Event_register(PSC_Timer_expired(self->shutdownTimer), self,
		forceDestroy, 0);
	PSC_Timer_start(self->shutdownTimer, 0);
    }
}

static void destroyPoolConn(void *obj)
{
    PSC_Connection_destroy(obj);
}

static void destroyPool(void *arg)
{
    ObjectPool_destroy(arg, destroyPoolConn);
}

SOEXPORT void PSC_Server_destroy(PSC_Server *self)
{
    if (!self) return;

    PSC_Timer_destroy(self->shutdownTimer);
    if (self->nconn)
    {
	if (self->nthr) for (int thr = 0; thr < self->nthr; ++thr)
	{
	    PSC_Service_runOnThread(thr, destroyPool, self->clients[thr].pool);
	}
	else ObjectPool_destroy(self->clients->pool, destroyPoolConn);
	sem_wait(&self->allclosed);
	free(self->clients);
    }
    if (!self->nsocks)
    {
	PSC_Event_unregister(PSC_Service_shutdown(), self, forceDestroy, 0);
    }
    else for (uint8_t i = 0; i < self->nsocks; ++i)
    {
	PSC_Service_unregisterRead(self->socks[i].fd);
	PSC_Event_unregister(PSC_Service_readyRead(), self, acceptConnection,
		self->socks[i].fd);
	close(self->socks[i].fd);
    }
    sem_destroy(&self->allclosed);
#ifdef NO_SHAREDOBJ
    pthread_mutex_destroy(&self->lock);
#endif
    if (self->shutdownComplete) self->shutdownComplete(self->owner);
    if (self->path)
    {
	unlink(self->path);
	free(self->path);
    }
#ifdef WITH_TLS
#  ifdef NO_SHAREDOBJ
    pthread_mutex_destroy(&self->tlslock);
    destroyTlsCfg(self->tlscfg);
#  else
    SharedObj_retire(self->tlscfg);
#  endif
#endif
    free(self);
}

