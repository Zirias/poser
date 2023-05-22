#define _DEFAULT_SOURCE

#include "connection.h"

#include <poser/core/client.h>
#include <poser/core/event.h>
#include <poser/core/log.h>
#include <poser/core/service.h>
#include <poser/core/threadpool.h>
#include <poser/core/util.h>

#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef WITH_TLS
#include <openssl/ssl.h>
#endif

#define CONNBUFSZ 16*1024
#define NWRITERECS 16
#define CONNTICKS 6
#define RESOLVTICKS 6

struct PSC_EADataReceived
{
    uint8_t *buf;
    int handling;
    uint16_t size;
};

static char hostbuf[NI_MAXHOST];
static char servbuf[NI_MAXSERV];

typedef struct WriteRecord
{
    const uint8_t *wrbuf;
    void *id;
    uint16_t wrbuflen;
    uint16_t wrbufpos;
} WriteRecord;

typedef struct RemoteAddrResolveArgs
{
    union {
	struct sockaddr sa;
	struct sockaddr_storage ss;
    };
    socklen_t addrlen;
    int rc;
    char name[NI_MAXHOST];
} RemoteAddrResolveArgs;

#ifdef WITH_TLS
static SSL_CTX *tls_ctx = 0;
static int tls_nconn = 0;
#endif

struct PSC_Connection
{
    PSC_Event *connected;
    PSC_Event *closed;
    PSC_Event *dataReceived;
    PSC_Event *dataSent;
    PSC_Event *nameResolved;
    PSC_ThreadJob *resolveJob;
#ifdef WITH_TLS
    SSL *tls;
#endif
    char *addr;
    char *name;
    void *data;
    void (*deleter)(void *);
    WriteRecord writerecs[NWRITERECS];
    PSC_EADataReceived args;
    RemoteAddrResolveArgs resolveArgs;
    int fd;
    int connecting;
    int port;
#ifdef WITH_TLS
    int tls_connect_st;
    int tls_connect_ticks;
    int tls_read_st;
    int tls_write_st;
#endif
    int blacklisthits;
    uint8_t deleteScheduled;
    uint8_t nrecs;
    uint8_t baserecidx;
    uint8_t rdbuf[CONNBUFSZ];
};

void PSC_Connection_blacklistAddress(int hits,
	socklen_t len, struct sockaddr *addr) ATTR_NONNULL((3));

static void checkPendingConnection(void *receiver, void *sender, void *args);
static void wantreadwrite(PSC_Connection *self) CMETHOD;
#ifdef WITH_TLS
static void checkPendingTls(void *receiver, void *sender, void *args);
static void dohandshake(PSC_Connection *self) CMETHOD;
#endif
static void dowrite(PSC_Connection *self) CMETHOD;
static void deleteConnection(void *receiver, void *sender, void *args);
static void deleteLater(PSC_Connection *self);
static void doread(PSC_Connection *self) CMETHOD;
static void readConnection(void *receiver, void *sender, void *args);
static void resolveRemoteAddrFinished(
	void *receiver, void *sender, void *args);
static void resolveRemoteAddrProc(void *arg);
static void writeConnection(void *receiver, void *sender, void *args);

static void checkPendingConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    if (self->connecting && !--self->connecting)
    {
	PSC_Log_fmt(PSC_L_INFO, "connection: timeout connecting to %s",
		PSC_Connection_remoteAddr(self));
	PSC_Service_unregisterWrite(self->fd);
	PSC_Connection_close(self, 1);
    }
}

static void wantreadwrite(PSC_Connection *self)
{
    if (self->connecting ||
#ifdef WITH_TLS
	    self->tls_connect_st == SSL_ERROR_WANT_WRITE ||
	    self->tls_read_st == SSL_ERROR_WANT_WRITE ||
	    self->tls_write_st == SSL_ERROR_WANT_WRITE ||
#endif
	    self->nrecs)
    {
	PSC_Service_registerWrite(self->fd);
    }
    else
    {
	PSC_Service_unregisterWrite(self->fd);
    }

    if (
#ifdef WITH_TLS
	    self->tls_connect_st == SSL_ERROR_WANT_READ ||
	    self->tls_read_st == SSL_ERROR_WANT_READ ||
	    self->tls_write_st == SSL_ERROR_WANT_READ ||
#endif
	    !self->args.handling)
    {
	PSC_Service_registerRead(self->fd);
    }
    else
    {
	PSC_Service_unregisterRead(self->fd);
    }
}

#ifdef WITH_TLS
static void checkPendingTls(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    if (self->tls_connect_ticks && !--self->tls_connect_ticks)
    {
	PSC_Log_fmt(PSC_L_INFO, "connection: TLS handshake timeout with %s",
		PSC_Connection_remoteAddr(self));
	PSC_Connection_close(self, 1);
    }
}

static void dohandshake(PSC_Connection *self)
{
    PSC_Log_fmt(PSC_L_DEBUG, "connection: handshake with %s",
	    PSC_Connection_remoteAddr(self));
    int rc = SSL_connect(self->tls);
    if (rc > 0)
    {
	self->tls_connect_st = 0;
	PSC_Log_fmt(PSC_L_DEBUG, "connection: connected to %s",
		PSC_Connection_remoteAddr(self));
	PSC_Event_unregister(PSC_Service_tick(), self, checkPendingTls, 0);
	self->tls_connect_ticks = 0;
	PSC_Event_raise(self->connected, 0, 0);
    }
    else
    {
	rc = SSL_get_error(self->tls, rc);
	if (rc == SSL_ERROR_WANT_READ || rc == SSL_ERROR_WANT_WRITE)
	{
	    self->tls_connect_st = rc;
	}
	else
	{
	    PSC_Log_fmt(PSC_L_ERROR,
		    "connection: TLS handshake failed with %s",
		    PSC_Connection_remoteAddr(self));
	    PSC_Event_unregister(PSC_Service_tick(), self, checkPendingTls, 0);
	    PSC_Connection_close(self, 1);
	    return;
	}
    }
    wantreadwrite(self);
}
#endif

static void dowrite(PSC_Connection *self)
{
    PSC_Log_fmt(PSC_L_DEBUG, "connection: writing to %s",
	    PSC_Connection_remoteAddr(self));
    WriteRecord *rec = self->writerecs + self->baserecidx;
    void *id = 0;
#ifdef WITH_TLS
    if (self->tls)
    {
	size_t writesz = 0;
	int rc = SSL_write_ex(self->tls, rec->wrbuf + rec->wrbufpos,
		rec->wrbuflen - rec->wrbufpos, &writesz);
	if (rc > 0)
	{
	    self->tls_write_st = 0;
	    if (writesz < (size_t)(rec->wrbuflen - rec->wrbufpos))
	    {
		rec->wrbufpos += writesz;
		wantreadwrite(self);
		return;
	    }
	    else id = rec->id;
	    if (++self->baserecidx == NWRITERECS) self->baserecidx = 0;
	    --self->nrecs;
	    if (id)
	    {
		PSC_Event_raise(self->dataSent, 0, id);
	    }
	}
	else
	{
	    rc = SSL_get_error(self->tls, rc);
	    if (rc == SSL_ERROR_WANT_READ || rc == SSL_ERROR_WANT_WRITE)
	    {
		self->tls_write_st = rc;
	    }
	    else
	    {
		PSC_Log_fmt(PSC_L_WARNING, "connection: error writing to %s",
			PSC_Connection_remoteAddr(self));
		PSC_Connection_close(self, 0);
		return;
	    }
	}
	wantreadwrite(self);
    }
    else
#endif
    {
	errno = 0;
	int rc = write(self->fd, rec->wrbuf + rec->wrbufpos,
		rec->wrbuflen - rec->wrbufpos);
	if (rc >= 0)
	{
	    if (rc < rec->wrbuflen - rec->wrbufpos)
	    {
		rec->wrbufpos += rc;
		return;
	    }
	    else id = rec->id;
	    if (++self->baserecidx == NWRITERECS) self->baserecidx = 0;
	    --self->nrecs;
	    wantreadwrite(self);
	    if (id)
	    {
		PSC_Event_raise(self->dataSent, 0, id);
	    }
	}
	else if (errno == EWOULDBLOCK || errno == EAGAIN)
	{
	    PSC_Log_fmt(PSC_L_INFO, "connection: not ready for writing to %s",
		    PSC_Connection_remoteAddr(self));
	    return;
	}
	else
	{
	    PSC_Log_fmt(PSC_L_WARNING, "connection: error writing to %s",
		    PSC_Connection_remoteAddr(self));
	    PSC_Connection_close(self, 0);
	}
    }
}

static void writeConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    if (self->connecting)
    {
	PSC_Event_unregister(PSC_Service_tick(), self,
		checkPendingConnection, 0);
	int err = 0;
	socklen_t errlen = sizeof err;
	if (getsockopt(self->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0
		|| err)
	{
	    PSC_Log_fmt(PSC_L_INFO, "connection: failed to connect to %s",
		    PSC_Connection_remoteAddr(self));
	    PSC_Connection_close(self, 1);
	    return;
	}
	self->connecting = 0;
#ifdef WITH_TLS
	if (self->tls)
	{
	    self->tls_connect_ticks = CONNTICKS;
	    PSC_Event_register(PSC_Service_tick(), self, checkPendingTls, 0);
	    dohandshake(self);
	    return;
	}
#endif
	wantreadwrite(self);
	PSC_Log_fmt(PSC_L_DEBUG, "connection: connected to %s",
		PSC_Connection_remoteAddr(self));
	PSC_Event_raise(self->connected, 0, 0);
	return;
    }
    PSC_Log_fmt(PSC_L_DEBUG, "connection: ready to write to %s",
	PSC_Connection_remoteAddr(self));
#ifdef WITH_TLS
    if (self->tls_connect_st == SSL_ERROR_WANT_WRITE) dohandshake(self);
    else if (self->tls_read_st == SSL_ERROR_WANT_WRITE) doread(self);
    else
#endif
    {
	if (!self->nrecs)
	{
#ifdef WITH_TLS
	    self->tls_write_st = 0;
#endif
	    PSC_Log_fmt(PSC_L_ERROR,
		    "connection: ready to send to %s with empty buffer",
		    PSC_Connection_remoteAddr(self));
	    wantreadwrite(self);
	    return;
	}
	dowrite(self);
    }
}

static void doread(PSC_Connection *self)
{
    PSC_Log_fmt(PSC_L_DEBUG, "connection: reading from %s",
	    PSC_Connection_remoteAddr(self));
#ifdef WITH_TLS
    if (self->tls)
    {
	int checkagain;
	do
	{
	    checkagain = 0;
	    size_t readsz = 0;
	    int ret = SSL_read_ex(self->tls, self->rdbuf, CONNBUFSZ, &readsz);
	    if (ret > 0)
	    {
		self->tls_read_st = 0;
		self->args.size = readsz;
		PSC_Event_raise(self->dataReceived, 0, &self->args);
		if (self->args.handling)
		{
		    PSC_Log_fmt(PSC_L_DEBUG,
			    "connection: blocking reads from %s",
			    PSC_Connection_remoteAddr(self));
		}
		else
		{
		    if (readsz == CONNBUFSZ) checkagain = 1;
		    PSC_Log_fmt(PSC_L_DEBUG,
			    "connection: done reading from %s",
			    PSC_Connection_remoteAddr(self));
		}
	    }
	    else
	    {
		int rc = SSL_get_error(self->tls, ret);
		if (rc == SSL_ERROR_WANT_READ || rc == SSL_ERROR_WANT_WRITE)
		{
		    self->tls_read_st = rc;
		    PSC_Log_fmt(PSC_L_DEBUG,
			    "connection: reading from %s incomplete: %d",
			    PSC_Connection_remoteAddr(self), rc);
		}
		else
		{
		    if (ret < 0)
		    {
			PSC_Log_fmt(PSC_L_WARNING,
				"connection: error reading from %s",
				PSC_Connection_remoteAddr(self));
		    }
		    PSC_Connection_close(self, 0);
		    return;
		}
	    }
	} while (checkagain);
	wantreadwrite(self);
    }
    else
#endif
    {
	errno = 0;
	int rc = read(self->fd, self->rdbuf, CONNBUFSZ);
	if (rc > 0)
	{
	    self->args.size = rc;
	    PSC_Event_raise(self->dataReceived, 0, &self->args);
	    if (self->args.handling)
	    {
		PSC_Log_fmt(PSC_L_DEBUG, "connection: blocking reads from %s",
			PSC_Connection_remoteAddr(self));
	    }
	    wantreadwrite(self);
	}
	else if (errno == EWOULDBLOCK || errno == EAGAIN)
	{
	    PSC_Log_fmt(PSC_L_DEBUG,
		    "connection: ignoring spurious read from %s",
		    PSC_Connection_remoteAddr(self));
	}
	else
	{
	    if (rc < 0)
	    {
		PSC_Log_fmt(PSC_L_WARNING, "connection: error reading from %s",
			PSC_Connection_remoteAddr(self));
	    }
	    PSC_Connection_close(self, 0);
	}
    }
}

static void readConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    PSC_Log_fmt(PSC_L_DEBUG, "connection: ready to read from %s",
	    PSC_Connection_remoteAddr(self));

#ifdef WITH_TLS
    if (self->tls_connect_st == SSL_ERROR_WANT_READ) dohandshake(self);
    else if (self->tls_write_st == SSL_ERROR_WANT_READ) dowrite(self);
    else
#endif
    {
	if (self->args.handling)
	{
#ifdef WITH_TLS
	    self->tls_read_st = 0;
#endif
	    PSC_Log_fmt(PSC_L_WARNING,
		    "connection: new data while read buffer from %s "
		    "still handled", PSC_Connection_remoteAddr(self));
	    wantreadwrite(self);
	    return;
	}
	doread(self);
    }
}

static void deleteConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    self->deleteScheduled = 2;
    PSC_Connection_destroy(self);
}

SOLOCAL PSC_Connection *PSC_Connection_create(int fd, const ConnOpts *opts)
{
    PSC_Connection *self = PSC_malloc(sizeof *self);
    self->connected = PSC_Event_create(self);
    self->closed = PSC_Event_create(self);
    self->dataReceived = PSC_Event_create(self);
    self->dataSent = PSC_Event_create(self);
    self->nameResolved = PSC_Event_create(self);
    self->resolveJob = 0;
    self->fd = fd;
    self->connecting = 0;
    self->port = 0;
    self->addr = 0;
    self->name = 0;
    self->data = 0;
    self->deleter = 0;
#ifdef WITH_TLS
    if (opts->tls_client)
    {
	if (!tls_ctx) tls_ctx = SSL_CTX_new(TLS_client_method());
	++tls_nconn;
	self->tls = SSL_new(tls_ctx);
	SSL_set_fd(self->tls, fd);
	if (opts->tls_client_certfile)
	{
	    if (opts->tls_client_keyfile)
	    {
		if (SSL_use_certificate_file(self->tls,
			    opts->tls_client_certfile, SSL_FILETYPE_PEM) > 0)
		{
		    if (SSL_use_PrivateKey_file(self->tls,
				opts->tls_client_keyfile,
				SSL_FILETYPE_PEM) <= 0)
		    {
			PSC_Log_fmt(PSC_L_ERROR,
				"connection: error loading private key %s.",
				opts->tls_client_keyfile);
		    }
		    else
		    {
			PSC_Log_fmt(PSC_L_INFO, "connection: using client "
				"certificate %s.", opts->tls_client_certfile);
		    }
		}
		else
		{
		    PSC_Log_fmt(PSC_L_ERROR, "connection: error loading "
			    "certificate %s.", opts->tls_client_certfile);
		}
	    }
	    else
	    {
		PSC_Log_msg(PSC_L_ERROR, "connection: certificate without "
			"private key, ignoring.");
	    }
	}
	else if (opts->tls_client_keyfile)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "connection: private key without certificate, ignoring.");
	}
    }
    else
    {
	self->tls = 0;
    }
    self->tls_connect_st = 0;
    self->tls_read_st = 0;
    self->tls_write_st = 0;
#endif
    self->blacklisthits = opts->blacklisthits;
    self->args.buf = self->rdbuf;
    self->args.handling = 0;
    self->deleteScheduled = 0;
    self->nrecs = 0;
    self->baserecidx = 0;
    PSC_Event_register(PSC_Service_readyRead(), self, readConnection, fd);
    PSC_Event_register(PSC_Service_readyWrite(), self, writeConnection, fd);
    if (opts->createmode == CCM_CONNECTING)
    {
	self->connecting = CONNTICKS;
	PSC_Event_register(PSC_Service_tick(), self,
		checkPendingConnection, 0);
	PSC_Service_registerWrite(fd);
    }
    else if (opts->createmode == CCM_NORMAL)
    {
	PSC_Service_registerRead(fd);
    }
    return self;
}

SOEXPORT PSC_Event *PSC_Connection_connected(PSC_Connection *self)
{
    return self->connected;
}

SOEXPORT PSC_Event *PSC_Connection_closed(PSC_Connection *self)
{
    return self->closed;
}

SOEXPORT PSC_Event *PSC_Connection_dataReceived(PSC_Connection *self)
{
    return self->dataReceived;
}

SOEXPORT PSC_Event *PSC_Connection_dataSent(PSC_Connection *self)
{
    return self->dataSent;
}

SOEXPORT PSC_Event *PSC_Connection_nameResolved(PSC_Connection *self)
{
    return self->nameResolved;
}

SOEXPORT const char *PSC_Connection_remoteAddr(const PSC_Connection *self)
{
    if (!self->addr) return "<unknown>";
    return self->addr;
}

SOEXPORT const char *PSC_Connection_remoteHost(const PSC_Connection *self)
{
    return self->name;
}

SOEXPORT int PSC_Connection_remotePort(const PSC_Connection *self)
{
    return self->port;
}

static void resolveRemoteAddrProc(void *arg)
{
    RemoteAddrResolveArgs *rara = arg;
    RemoteAddrResolveArgs tmp;
    memcpy(&tmp, rara, sizeof tmp);
    char buf[NI_MAXSERV];
    tmp.rc = getnameinfo(&tmp.sa, tmp.addrlen,
	    tmp.name, sizeof tmp.name, buf, sizeof buf, NI_NUMERICSERV);
    if (!PSC_ThreadJob_canceled()) memcpy(rara, &tmp, sizeof *rara);
}

static void resolveRemoteAddrFinished(void *receiver, void *sender, void *args)
{
    PSC_Connection *self = receiver;
    PSC_ThreadJob *job = sender;
    RemoteAddrResolveArgs *rara = args;

    if (PSC_ThreadJob_hasCompleted(job))
    {
	if (rara->rc >= 0 && strcmp(rara->name, self->addr) != 0)
	{
	    PSC_Log_fmt(PSC_L_DEBUG, "connection: %s is %s",
		    self->addr, rara->name);
	    self->name = PSC_copystr(rara->name);
	}
	else
	{
	    PSC_Log_fmt(PSC_L_DEBUG, "connection: error resolving name for %s",
		    self->addr);
	}
    }
    else
    {
	PSC_Log_fmt(PSC_L_DEBUG, "connection: timeout resolving name for %s",
		self->addr);
    }
    self->resolveJob = 0;
    PSC_Event_raise(self->nameResolved, 0, 0);
}

SOLOCAL void PSC_Connection_setRemoteAddr(PSC_Connection *self,
	struct sockaddr *addr, socklen_t addrlen, int numericOnly)
{
    free(self->addr);
    free(self->name);
    self->addr = 0;
    self->name = 0;
    if (getnameinfo(addr, addrlen, hostbuf, sizeof hostbuf,
		servbuf, sizeof servbuf, NI_NUMERICHOST|NI_NUMERICSERV) >= 0)
    {
	self->addr = PSC_copystr(hostbuf);
	sscanf(servbuf, "%d", &self->port);
	if (!self->resolveJob && !numericOnly && PSC_ThreadPool_active())
	{
	    memcpy(&self->resolveArgs.sa, addr, addrlen);
	    self->resolveArgs.addrlen = addrlen;
	    self->resolveJob = PSC_ThreadJob_create(resolveRemoteAddrProc,
		    &self->resolveArgs, RESOLVTICKS);
	    PSC_Event_register(PSC_ThreadJob_finished(self->resolveJob), self,
		    resolveRemoteAddrFinished, 0);
	    PSC_ThreadPool_enqueue(self->resolveJob);
	}
    }
}

SOLOCAL void PSC_Connection_setRemoteAddrStr(PSC_Connection *self,
	const char *addr)
{
    free(self->addr);
    free(self->name);
    self->addr = PSC_copystr(addr);
    self->name = 0;
}

SOEXPORT int PSC_Connection_write(PSC_Connection *self,
	const uint8_t *buf, uint16_t sz, void *id)
{
    if (self->nrecs == NWRITERECS) return -1;
    WriteRecord *rec = self->writerecs +
	((self->baserecidx + self->nrecs++) % NWRITERECS);
    rec->wrbuflen = sz;
    rec->wrbufpos = 0;
    rec->wrbuf = buf;
    rec->id = id;
    wantreadwrite(self);
    return 0;
}

SOEXPORT void PSC_Connection_activate(PSC_Connection *self)
{
    if (self->args.handling) return;
    PSC_Log_fmt(PSC_L_DEBUG, "connection: unblocking reads from %s",
	    PSC_Connection_remoteAddr(self));
    wantreadwrite(self);
}

SOEXPORT int PSC_Connection_confirmDataReceived(PSC_Connection *self)
{
    if (!self->args.handling) return -1;
    if (--self->args.handling) return 0;
    PSC_Connection_activate(self);
#ifdef WITH_TLS
    if (self->tls && self->args.size == CONNBUFSZ) doread(self);
#endif
    return 0;
}

SOEXPORT void PSC_Connection_close(PSC_Connection *self, int blacklist)
{
#ifdef WITH__TLS
    if (self->tls && !self->connecting && !self->tls_connect_st)
    {
	SSL_shutdown(self->tls);
    }
#endif
    if (blacklist && self->blacklisthits && self->resolveArgs.addrlen)
    {
	PSC_Connection_blacklistAddress(self->blacklisthits,
		self->resolveArgs.addrlen, &self->resolveArgs.sa);
    }
    PSC_Event_raise(self->closed, 0, self->connecting ? 0 : self);
    deleteLater(self);
}

SOEXPORT void PSC_Connection_setData(PSC_Connection *self,
	void *data, void (*deleter)(void *))
{
    if (self->deleter) self->deleter(self->data);
    self->data = data;
    self->deleter = deleter;
}

SOEXPORT void *PSC_Connection_data(const PSC_Connection *self)
{
    return self->data;
}

static void cleanForDelete(PSC_Connection *self)
{
    PSC_Service_unregisterRead(self->fd);
    PSC_Service_unregisterWrite(self->fd);
    close(self->fd);
    if (self->resolveJob)
    {
	PSC_Event_unregister(PSC_ThreadJob_finished(self->resolveJob), self,
		resolveRemoteAddrFinished, 0);
	PSC_ThreadPool_cancel(self->resolveJob);
    }
}

static void deleteLater(PSC_Connection *self)
{
    if (!self) return;
    if (!self->deleteScheduled)
    {
	cleanForDelete(self);
	PSC_Event_register(PSC_Service_eventsDone(), self,
		deleteConnection, 0);
	self->deleteScheduled = 1;
    }
}

SOLOCAL void PSC_Connection_destroy(PSC_Connection *self)
{
    if (!self) return;
    if (self->deleteScheduled)
    {
	if (self->deleteScheduled == 1) return;
	PSC_Event_unregister(PSC_Service_eventsDone(), self,
		deleteConnection, 0);
    }
    else cleanForDelete(self);

    for (; self->nrecs; --self->nrecs)
    {
	WriteRecord *rec = self->writerecs + self->baserecidx;
	if (rec->id)
	{
	    PSC_Event_raise(self->dataSent, 0, rec->id);
	}
	if (++self->baserecidx == NWRITERECS) self->baserecidx = 0;
    }
#ifdef WITH_TLS
    SSL_free(self->tls);
    if (tls_nconn && !--tls_nconn)
    {
	SSL_CTX_free(tls_ctx);
	tls_ctx = 0;
    }
    PSC_Event_unregister(PSC_Service_tick(), self, checkPendingTls, 0);
#endif
    PSC_Event_unregister(PSC_Service_tick(), self, checkPendingConnection, 0);
    PSC_Event_unregister(PSC_Service_readyRead(), self,
	    readConnection, self->fd);
    PSC_Event_unregister(PSC_Service_readyWrite(), self,
	    writeConnection, self->fd);
    if (self->deleter) self->deleter(self->data);
    free(self->addr);
    free(self->name);
    PSC_Event_destroy(self->nameResolved);
    PSC_Event_destroy(self->dataSent);
    PSC_Event_destroy(self->dataReceived);
    PSC_Event_destroy(self->closed);
    PSC_Event_destroy(self->connected);
    free(self);
}

SOEXPORT const uint8_t *PSC_EADataReceived_buf(const PSC_EADataReceived *self)
{
    return self->buf;
}

SOEXPORT uint16_t PSC_EADataReceived_size(const PSC_EADataReceived *self)
{
    return self->size;
}

SOEXPORT void PSC_EADataReceived_markHandling(PSC_EADataReceived *self)
{
    ++self->handling;
}

