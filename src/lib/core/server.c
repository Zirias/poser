#define _DEFAULT_SOURCE

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
#include <threads.h>
#include <unistd.h>

#ifndef MAXSOCKS
#define MAXSOCKS 64
#endif

#define BINDCHUNK 8
#define CONNCHUNK 8

typedef struct PSC_TcpServerOpts
{
    const char **bindhosts;
    size_t bh_capa;
    size_t bh_count;
    PSC_Proto proto;
    int port;
    int numerichosts;
    int connwait;
} PSC_TcpServerOpts;

static thread_local PSC_TcpServerOpts tcpServerOpts;

static char hostbuf[NI_MAXHOST];
static char servbuf[NI_MAXSERV];

static struct sockaddr_in sain;
static struct sockaddr_in6 sain6;

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
    size_t conncapa;
    size_t connsize;
    size_t nsocks;
    int numericHosts;
    int connwait;
    SockInfo socks[];
};

static void acceptConnection(void *receiver, void *sender, void *args);
static void removeConnection(void *receiver, void *sender, void *args);

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
    int connfd = accept(*sockfd, sa, sl);
    if (connfd < 0)
    {
	PSC_Log_msg(PSC_L_WARNING, "server: failed to accept connection");
	return;
    }
    fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK);
    if (self->connsize == self->conncapa)
    {
	self->conncapa += CONNCHUNK;
	self->conn = PSC_realloc(self->conn,
		self->conncapa * sizeof *self->conn);
    }
    ConnOpts co = {
#ifdef WITH_TLS
	.tls_client_certfile = 0,
	.tls_client_keyfile = 0,
	.tls_client = 0,
#endif
	.createmode = self->connwait ? CCM_WAIT : CCM_NORMAL
    };
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
	PSC_Connection_setRemoteAddr(newconn, sa, salen, self->numericHosts);
    }
    PSC_Log_fmt(PSC_L_DEBUG, "server: client connected from %s",
	    PSC_Connection_remoteAddr(newconn));
    PSC_Event_raise(self->clientConnected, 0, newconn);
}

static PSC_Server *PSC_Server_create(size_t nsocks, SockInfo *socks,
	char *path)
{
    if (nsocks < 1)
    {
	free(path);
	return 0;
    }
    PSC_Server *self = PSC_malloc(sizeof *self + nsocks * sizeof *socks);
    self->clientConnected = PSC_Event_create(self);
    self->clientDisconnected = PSC_Event_create(self);
    self->conn = PSC_malloc(CONNCHUNK * sizeof *self->conn);
    self->path = path;
    self->conncapa = CONNCHUNK;
    self->connsize = 0;
    self->numericHosts = tcpServerOpts.numerichosts;
    self->connwait = tcpServerOpts.connwait;
    self->nsocks = nsocks;
    memcpy(self->socks, socks, nsocks * sizeof *socks);
    for (size_t i = 0; i < nsocks; ++i)
    {
	PSC_Event_register(PSC_Service_readyRead(), self,
		acceptConnection, socks[i].fd);
	PSC_Service_registerRead(socks[i].fd);
    }

    return self;
}

SOEXPORT void PSC_TcpServerOpts_init(int port)
{
    free(tcpServerOpts.bindhosts);
    memset(&tcpServerOpts, 0, sizeof tcpServerOpts);
    tcpServerOpts.port = port;
}

SOEXPORT void PSC_TcpServerOpts_bind(const char *bindhost)
{
    if (tcpServerOpts.bh_count == tcpServerOpts.bh_capa)
    {
	tcpServerOpts.bh_capa += BINDCHUNK;
	tcpServerOpts.bindhosts = PSC_realloc(tcpServerOpts.bindhosts,
		tcpServerOpts.bh_capa * sizeof *tcpServerOpts.bindhosts);
    }
    tcpServerOpts.bindhosts[tcpServerOpts.bh_count++] = bindhost;
}

SOEXPORT void PSC_TcpServerOpts_setProto(PSC_Proto proto)
{
    tcpServerOpts.proto = proto;
}

SOEXPORT void PSC_TcpServerOpts_numericHosts(void)
{
    tcpServerOpts.numerichosts = 1;
}

SOEXPORT void PSC_TcpServerOpts_connWait(void)
{
    tcpServerOpts.connwait = 1;
}

SOEXPORT PSC_Server *PSC_Server_createTcp(void)
{
    SockInfo socks[MAXSOCKS];

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG|AI_NUMERICSERV;
    char portstr[6];
    snprintf(portstr, 6, "%d", tcpServerOpts.port);

    struct addrinfo *res0;
    size_t nsocks = 0;
    size_t bi = 0;
    int opt_true = 1;
    do
    {
	res0 = 0;
	if (getaddrinfo(tcpServerOpts.bindhosts[bi],
		    portstr, &hints, &res0) < 0 || !res0)
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "server: cannot get address info for `%s'",
		    tcpServerOpts.bindhosts[bi]);
	    continue;
	}
	for (struct addrinfo *res = res0; res && nsocks < MAXSOCKS;
		res = res->ai_next)
	{
	    if (res->ai_family != AF_INET
		    && res->ai_family != AF_INET6) continue;
	    if (tcpServerOpts.proto == PSC_P_IPv4
		    && res->ai_family != AF_INET) continue;
	    if (tcpServerOpts.proto == PSC_P_IPv6
		    && res->ai_family != AF_INET6) continue;
	    socks[nsocks].fd = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
	    if (socks[nsocks].fd < 0)
	    {
		PSC_Log_msg(PSC_L_ERROR, "server: cannot create socket");
		continue;
	    }
	    fcntl(socks[nsocks].fd, F_SETFL,
		    fcntl(socks[nsocks].fd, F_GETFL, 0) | O_NONBLOCK);
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
    } while (++bi < tcpServerOpts.bh_count);
    if (!nsocks)
    {
	PSC_Log_msg(PSC_L_FATAL, "server: could not create any sockets for "
		"listening to incoming connections");
	return 0;
    }
    
    PSC_Server *self = PSC_Server_create(nsocks, socks, 0);
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
    free(self);
}

