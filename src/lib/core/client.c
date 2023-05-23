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
#include <threads.h>
#include <unistd.h>

#define BLACKLISTSIZE 32

typedef struct PSC_TcpClientOpts
{
    const char *remotehost;
#ifdef WITH_TLS
    const char *tls_certfile;
    const char *tls_keyfile;
#endif
    PSC_Proto proto;
    int port;
    int numerichosts;
#ifdef WITH_TLS
    int tls;
    int noverify;
#endif
    int blacklisthits;
} PSC_TcpClientOpts;

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
    PSC_ClientCreatedHandler callback;
    PSC_TcpClientOpts opts;
} ResolveJobData;

static BlacklistEntry blacklist[BLACKLISTSIZE];
static thread_local PSC_TcpClientOpts tcpClientOpts;

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

static PSC_Connection *createFromAddrinfo(const PSC_TcpClientOpts *opts,
	struct addrinfo *res0)
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
	.tls_certfile = opts->tls_certfile,
	.tls_keyfile = opts->tls_keyfile,
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

static void doResolve(void *arg)
{
    ResolveJobData *data = arg;
    data->res0 = resolveAddress(&data->opts);
}

static void resolveDone(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    ResolveJobData *data = args;
    if (!data->res0) data->callback(data->receiver, 0);
    else data->callback(data->receiver,
	    createFromAddrinfo(&data->opts, data->res0));
    free(data);
}

SOEXPORT void PSC_TcpClientOpts_init(const char *remotehost, int port)
{
    memset(&tcpClientOpts, 0, sizeof tcpClientOpts);
    tcpClientOpts.remotehost = remotehost;
    tcpClientOpts.port = port;
}

SOEXPORT void PSC_TcpClientOpts_enableTls(
	const char *certfile, const char *keyfile)
{
#ifdef WITH_TLS
    tcpClientOpts.tls = 1;
    tcpClientOpts.tls_certfile = certfile;
    tcpClientOpts.tls_keyfile = keyfile;
#else
    (void)certfile;
    (void)keyfile;
    PSC_Service_panic("This version of libposercore does not support TLS!");
#endif
}

SOEXPORT void PSC_TcpClientOpts_disableCertVerify(void)
{
#ifdef WITH_TLS
    tcpClientOpts.noverify = 1;
#else
    PSC_Service_panic("This version of libposercore does not support TLS!");
#endif
}

SOEXPORT void PSC_TcpClientOpts_setProto(PSC_Proto proto)
{
    tcpClientOpts.proto = proto;
}

SOEXPORT void PSC_TcpClientOpts_numericHosts(void)
{
    tcpClientOpts.numerichosts = 1;
}

SOEXPORT void PSC_TcpClientOpts_setBlacklistHits(int blacklistHits)
{
    tcpClientOpts.blacklisthits = blacklistHits;
}

SOEXPORT PSC_Connection *PSC_Connection_createTcpClient(void)
{
    struct addrinfo *res0 = resolveAddress(&tcpClientOpts);
    if (!res0) return 0;
    return createFromAddrinfo(&tcpClientOpts, res0);
}

SOEXPORT int PSC_Connection_createTcpClientAsync(
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
    memcpy(&data->opts, &tcpClientOpts, sizeof tcpClientOpts);
    PSC_ThreadJob *resolveJob = PSC_ThreadJob_create(doResolve, data, 0);
    PSC_Event_register(PSC_ThreadJob_finished(resolveJob), 0, resolveDone, 0);
    PSC_ThreadPool_enqueue(resolveJob);
    return 0;
}

