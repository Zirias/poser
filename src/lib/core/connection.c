#define _DEFAULT_SOURCE

#include "client.h"
#include "connection.h"
#include "event.h"
#include "ipaddr.h"
#include "service.h"

#include <poser/core/log.h>
#include <poser/core/threadpool.h>
#include <poser/core/timer.h>
#include <poser/core/util.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef WITH_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

#define NWRITERECS 16
#define CONNTIMEOUT 5000

struct PSC_EADataReceived
{
    size_t size;
    uint8_t *buf;
    char *text;
    int handling;
};

typedef struct WriteRecord
{
    const uint8_t *wrbuf;
    void *id;
    size_t wrbuflen;
    size_t wrbufpos;
} WriteRecord;

typedef struct WriteNotifyRecord
{
    void *id;
    uint16_t wrbufpos;
} WriteNotifyRecord;

typedef enum ConnectionType
{
    CT_SOCKET,
    CT_PIPERD,
    CT_PIPEWR
} ConnectionType;

struct PSC_Connection
{
    PSC_MessageEndLocator rdlocator;
    PSC_Event *connected;
    PSC_Event *closed;
    PSC_Event *dataReceived;
    PSC_Event *dataSent;
    PSC_Timer *connectTimer;
#ifdef WITH_TLS
    PSC_Timer *tlsConnectTimer;
    SSL *tls;
#endif
    PSC_IpAddr *ipAddr;
    char *addr;
    char *name;
    void *data;
    void (*deleter)(void *);
    size_t rdbufsz;
    size_t rdbufused;
    size_t rdbufpos;
    size_t rdexpect;
    WriteRecord writerecs[NWRITERECS];
    WriteNotifyRecord writenotify[NWRITERECS];
    PSC_EADataReceived args;
    int fd;
    int paused;
    int port;
    int rdreg;
    int wrreg;
#ifdef WITH_TLS
    int tls_is_client;
    int tls_connect_st;
    int tls_read_st;
    int tls_write_st;
    int tls_shutdown_st;
    int tls_readagain;
    int tls_noverify;
#endif
    int blacklisthits;
    ConnectionType type;
    uint16_t wrbuflen;
    uint16_t wrbufpos;
    uint8_t deleteScheduled;
    uint8_t nrecs;
    uint8_t nnotify;
    char rdtextsave;
    uint8_t wrbuf[WRBUFSZ];
    uint8_t rdbuf[];
};

static void connectionTimeout(void *receiver, void *sender, void *args);
static void wantreadwrite(PSC_Connection *self) CMETHOD;
#ifdef WITH_TLS
static void tlsHandshakeTimeout(void *receiver, void *sender, void *args);
static void dohandshake(PSC_Connection *self) CMETHOD;
static void handshakenow(void *receiver, void *sender, void *args);
#endif
static void dowrite(PSC_Connection *self) CMETHOD;
static void deleteConnection(void *receiver, void *sender, void *args);
static void deleteLater(PSC_Connection *self);
static void doread(PSC_Connection *self) CMETHOD;
static void readConnection(void *receiver, void *sender, void *args);
static void writeConnection(void *receiver, void *sender, void *args);
static void tryWrite(void *receiver, void *sender, void *args);
static const char *locateeol(const char *str) ATTR_NONNULL((1));
static void raisereceivedevents(PSC_Connection *self) CMETHOD;

static void connectionTimeout(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    PSC_Log_fmt(PSC_L_INFO, "connection: timeout connecting to %s",
	    PSC_Connection_remoteAddr(self));
    PSC_Connection_close(self, 1);
}

static void wantreadwrite(PSC_Connection *self)
{
    if (self->connectTimer ||
#ifdef WITH_TLS
	    self->tls_connect_st == SSL_ERROR_WANT_WRITE ||
	    self->tls_read_st == SSL_ERROR_WANT_WRITE ||
	    self->tls_write_st == SSL_ERROR_WANT_WRITE ||
	    ((self->wrbuflen || self->nrecs)
	    && !self->tlsConnectTimer && !self->tls_shutdown_st)
#else
	    self->wrbuflen || self->nrecs
#endif
       )
    {
	if (!self->wrreg) PSC_Service_registerWrite(self->fd);
	self->wrreg = 1;
    }
    else
    {
	if (self->wrreg) PSC_Service_unregisterWrite(self->fd);
	self->wrreg = 0;
    }

    if (!self->deleteScheduled && (
#ifdef WITH_TLS
		self->tls_connect_st == SSL_ERROR_WANT_READ ||
		self->tls_read_st == SSL_ERROR_WANT_READ ||
		self->tls_write_st == SSL_ERROR_WANT_READ ||
#endif
		(!self->paused && !self->args.handling)))
    {
	if (!self->rdreg) PSC_Service_registerRead(self->fd);
	self->rdreg = 1;
    }
    else
    {
	if (self->rdreg) PSC_Service_unregisterRead(self->fd);
	self->rdreg = 0;
    }
}

#ifdef WITH_TLS
static void tlsHandshakeTimeout(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    PSC_Log_fmt(PSC_L_INFO, "connection: TLS handshake timeout with %s",
	    PSC_Connection_remoteAddr(self));
    PSC_Connection_close(self, 1);
}

static void dohandshake(PSC_Connection *self)
{
    PSC_Log_fmt(PSC_L_DEBUG, "connection: handshake with %s",
	    PSC_Connection_remoteAddr(self));
    int rc = self->tls_is_client ?
	SSL_connect(self->tls) : SSL_accept(self->tls);
    if (rc > 0)
    {
	self->tls_connect_st = 0;
	PSC_Timer_destroy(self->tlsConnectTimer);
	self->tlsConnectTimer = 0;
	if (self->tls_is_client)
	{
	    long vres;
	    if ((vres = SSL_get_verify_result(self->tls)) != X509_V_OK)
	    {
		if (self->tls_noverify)
		{
		    PSC_Log_fmt(PSC_L_INFO,
			    "connection: peer verification failed: %s "
			    "[ignored]", X509_verify_cert_error_string(vres));
		}
		else
		{
		    PSC_Log_fmt(PSC_L_WARNING,
			    "connection: peer verification failed: %s",
			    X509_verify_cert_error_string(vres));
		    PSC_Connection_close(self, 1);
		    return;
		}
	    }
	    PSC_Log_fmt(PSC_L_DEBUG, "connection: connected to %s",
		    PSC_Connection_remoteAddr(self));
	    PSC_Event_raise(self->connected, 0, 0);
	}
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
		    "connection: TLS handshake failed with %s: %s",
		    PSC_Connection_remoteAddr(self),
		    ERR_error_string(ERR_get_error(), 0));
	    PSC_Timer_destroy(self->tlsConnectTimer);
	    self->tlsConnectTimer = 0;
	    PSC_Connection_close(self, 1);
	    return;
	}
    }
    wantreadwrite(self);
}

static void handshakenow(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;

    PSC_Event_unregister(PSC_Service_eventsDone(), self, handshakenow, 0);
    dohandshake(self);
}
#endif

static void dowrite(PSC_Connection *self)
{
    PSC_Log_fmt(PSC_L_DEBUG, "connection: writing to %s",
	    PSC_Connection_remoteAddr(self));
    uint8_t notno = 0;
    if (self->nrecs && !self->wrbuflen)
    {
	uint8_t recno = 0;
	for (; recno < self->nrecs && self->wrbuflen < WRBUFSZ; ++recno)
	{
	    WriteRecord *rec = self->writerecs + recno;
	    size_t chunklen = rec->wrbuflen - rec->wrbufpos;
	    if (chunklen + self->wrbuflen > WRBUFSZ)
	    {
		chunklen = WRBUFSZ - self->wrbuflen;
	    }
	    memcpy(self->wrbuf + self->wrbuflen, rec->wrbuf + rec->wrbufpos,
		    chunklen);
	    self->wrbuflen += chunklen;
	    rec->wrbufpos += chunklen;
	    if (rec->wrbufpos != rec->wrbuflen) break;
	    if (rec->id)
	    {
		self->writenotify[notno].id = rec->id;
		self->writenotify[notno].wrbufpos = self->wrbuflen;
		++notno;
	    }
	}
	if (recno && recno < self->nrecs)
	{
	    memmove(self->writerecs, self->writerecs + recno,
		    (self->nrecs - recno) * sizeof *self->writerecs);
	}
	self->nrecs -= recno;
	self->nnotify = notno;
    }
    for (notno = 0; notno < self->nnotify
	    && !self->writenotify[notno].id; ++notno)
	;

#ifdef WITH_TLS
    if (self->tls)
    {
	size_t writesz = 0;
	int rc = SSL_write_ex(self->tls, self->wrbuf + self->wrbufpos,
		self->wrbuflen - self->wrbufpos, &writesz);
	if (rc > 0)
	{
	    self->tls_write_st = 0;
	    self->wrbufpos += writesz;
	    for (; notno < self->nnotify
		    && self->writenotify[notno].wrbufpos <= self->wrbufpos;
		    ++notno)
	    {
		PSC_Event_raise(self->dataSent, 0,
			self->writenotify[notno].id);
		self->writenotify[notno].id = 0;
	    }
	    if (self->wrbufpos < self->wrbuflen)
	    {
		wantreadwrite(self);
		return;
	    }
	    else
	    {
		self->wrbuflen = 0;
		self->wrbufpos = 0;
		self->nnotify = 0;
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
		self->nrecs = 0;
		self->wrbuflen = 0;
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
	int rc = write(self->fd, self->wrbuf + self->wrbufpos,
		self->wrbuflen - self->wrbufpos);
	if (rc >= 0)
	{
	    self->wrbufpos += rc;
	    for (; notno < self->nnotify
		    && self->writenotify[notno].wrbufpos <= self->wrbufpos;
		    ++notno)
	    {
		PSC_Event_raise(self->dataSent, 0,
			self->writenotify[notno].id);
	    }
	    if (self->wrbufpos < self->wrbuflen) return;
	    else
	    {
		self->wrbuflen = 0;
		self->wrbufpos = 0;
		self->nnotify = 0;
	    }
	}
	else if (errno == EWOULDBLOCK || errno == EAGAIN)
	{
	    PSC_Log_fmt(PSC_L_DEBUG,
		    "connection: not ready for writing to %s",
		    PSC_Connection_remoteAddr(self));
	}
	else
	{
	    PSC_Log_fmt(PSC_L_WARNING, "connection: error writing to %s",
		    PSC_Connection_remoteAddr(self));
	    self->nrecs = 0;
	    self->wrbuflen = 0;
	    PSC_Connection_close(self, 0);
	}
	wantreadwrite(self);
    }
}

static void writeConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    if (self->connectTimer)
    {
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
#ifdef WITH_TLS
	if (self->tls)
	{
	    PSC_Event_unregister(PSC_Timer_expired(self->connectTimer), self,
		    connectionTimeout, 0);
	    self->tlsConnectTimer = self->connectTimer;
	    PSC_Timer_setMs(self->tlsConnectTimer, CONNTIMEOUT);
	    PSC_Event_register(PSC_Timer_expired(self->tlsConnectTimer), self,
		    tlsHandshakeTimeout, 0);
	    dohandshake(self);
	    return;
	}
	else
#endif
	{
	    PSC_Timer_destroy(self->connectTimer);
	}
	self->connectTimer = 0;
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
	if (!self->nrecs && !self->wrbuflen)
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

static void tryWrite(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection *self = receiver;
    if (!self->wrreg && (self->nrecs || self->wrbuflen)) dowrite(self);
}

static const char *locateeol(const char *str)
{
    const char *result = strstr(str, "\r\n");
    if (result) return result + 2;
    result = strchr(str, '\n');
    if (!result) result = strchr(str, '\r');
    if (result) return result + 1;
    return 0;
}

static void raisereceivedevents(PSC_Connection *self)
{
    uint8_t *rdbuf;
    if (self->type == CT_PIPERD) rdbuf = self->wrbuf;
    else rdbuf = self->rdbuf;

    while (!self->args.handling && self->rdbufused)
    {
	if (self->rdtextsave)
	{
	    rdbuf[self->rdbufpos] = self->rdtextsave;
	    self->rdtextsave = 0;
	}
	size_t len = 0;
	if (self->rdlocator)
	{
	    while (self->rdbufpos < self->rdbufused &&
		    !rdbuf[self->rdbufpos]) ++self->rdbufpos;
	    if (self->rdbufpos == self->rdbufused)
	    {
		self->rdbufused = 0;
		self->rdbufpos = 0;
		break;
	    }
	    char *str = (char *)(rdbuf + self->rdbufpos);
	    const char *end = self->rdlocator(str);
	    if (end)
	    {
		len = end - str;
		if (len > self->rdbufused - self->rdbufpos)
		{
		    len = self->rdbufused - self->rdbufpos;
		}
	    }
	    else if (self->rdbufpos || self->rdbufused < self->rdbufsz) break;
	    else
	    {
		len = strlen(str);
	    }
	    self->rdtextsave = str[len];
	    str[len] = 0;
	    self->args.size = 0;
	    self->args.buf = 0;
	    self->args.text = str;
	}
	else
	{
	    if (self->rdexpect)
	    {
		len = self->rdexpect;
		if (len > (self->rdbufused - self->rdbufpos)) break;
	    }
	    else len = self->rdbufused - self->rdbufpos;
	    self->args.size = len;
	    self->args.buf = rdbuf + self->rdbufpos;
	    self->args.text = 0;
	}
	PSC_Event_raise(self->dataReceived, 0, &self->args);
	self->rdbufpos += len;
	if (self->rdbufpos == self->rdbufused)
	{
	    self->rdbufused = 0;
	    self->rdbufpos = 0;
	}
    }
    if (self->rdbufused && (self->rdlocator
		|| (self->rdbufused - self->rdbufpos) < self->rdexpect))
    {
	memmove(rdbuf, rdbuf + self->rdbufpos,
		self->rdbufused - self->rdbufpos);
	self->rdbufused -= self->rdbufpos;
	self->rdbufpos = 0;
	rdbuf[self->rdbufused] = 0;
    }
}

static void doread(PSC_Connection *self)
{
    PSC_Log_fmt(PSC_L_DEBUG, "connection: reading from %s",
	    PSC_Connection_remoteAddr(self));
#ifdef WITH_TLS
    if (self->tls)
    {
	do
	{
	    self->tls_readagain = 0;
	    size_t readsz = 0;
	    size_t wantsz = self->rdbufsz - self->rdbufused;
	    int ret = SSL_read_ex(self->tls, self->rdbuf + self->rdbufused,
		    wantsz, &readsz);
	    if (ret > 0)
	    {
		self->tls_read_st = 0;
		self->rdbufused += readsz;
		self->rdbuf[self->rdbufused] = 0;
		raisereceivedevents(self);
		if (readsz == wantsz) self->tls_readagain = 1;
		PSC_Log_fmt(PSC_L_DEBUG,
			"connection: done reading from %s",
			PSC_Connection_remoteAddr(self));
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
		    self->nrecs = 0;
		    self->wrbuflen = 0;
		    PSC_Connection_close(self, 0);
		    return;
		}
	    }
	} while (self->tls_readagain && !self->args.handling);
	wantreadwrite(self);
    }
    else
#endif
    {
	errno = 0;
	uint8_t *rdbuf;
	if (self->type == CT_PIPEWR) goto doclose;
	if (self->type == CT_PIPERD) rdbuf = self->wrbuf;
	else rdbuf = self->rdbuf;

	ssize_t rc = read(self->fd, rdbuf + self->rdbufused,
		self->rdbufsz - self->rdbufused);
	if (rc > 0)
	{
	    self->rdbufused += rc;
	    rdbuf[self->rdbufused] = 0;
	    raisereceivedevents(self);
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
doclose:
	    self->nrecs = 0;
	    self->wrbuflen = 0;
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
    if (self->tls_shutdown_st == SSL_ERROR_WANT_READ)
	deleteConnection(self, 0, 0);
    else if (self->tls_connect_st == SSL_ERROR_WANT_READ) dohandshake(self);
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
    if (self->wrbuflen || self->nrecs) return;
#ifdef WITH_TLS
    if (self->tls && !self->connectTimer && !self->tls_connect_st)
    {
	self->tls_shutdown_st = 0;
	int rc = SSL_shutdown(self->tls);
	if (!PSC_Service_shutsdown())
	{
	    if (rc == 0) rc = SSL_shutdown(self->tls);
	    long err = 0;
	    if (rc < 0 && (err = SSL_get_error(self->tls, rc))
		    == SSL_ERROR_WANT_READ)
	    {
		self->tls_shutdown_st = err;
		if (!self->rdreg) PSC_Service_registerRead(self->fd);
		self->rdreg = 1;
		return;
	    }
	}
    }
#endif
    self->deleteScheduled = 2;
    PSC_Connection_destroy(self);
}

SOLOCAL PSC_Connection *PSC_Connection_create(int fd, const ConnOpts *opts)
{
    PSC_Connection *self;

    ConnectionType type = CT_SOCKET;
    if (opts->createmode == CCM_PIPERD) type = CT_PIPERD;
    else if (opts->createmode == CCM_PIPEWR) type = CT_PIPEWR;
    size_t connsz = sizeof *self;
    switch (type)
    {
	case CT_SOCKET:
	    connsz += opts->rdbufsz + 1;
	    break;

	case CT_PIPERD:
	    connsz = connsz - WRBUFSZ + opts->rdbufsz + 1;
	    break;

	default:
	    break;
    }

    self = PSC_malloc(connsz);
    self->rdlocator = 0;
    self->connected = PSC_Event_create(self);
    self->closed = PSC_Event_create(self);
    self->dataReceived = PSC_Event_create(self);
    self->dataSent = PSC_Event_create(self);
    self->connectTimer = 0;
    self->rdbufsz = opts->rdbufsz;
    self->rdbufused = 0;
    self->rdbufpos = 0;
    self->rdexpect = 0;
    self->fd = fd;
    self->paused = 0;
    self->port = 0;
    self->rdreg = 0;
    self->wrreg = 0;
    self->ipAddr = 0;
    self->addr = 0;
    self->name = 0;
    self->data = 0;
    self->deleter = 0;
#ifdef WITH_TLS
    self->tls_is_client = 0;
    if (opts->tls_mode != TM_NONE)
    {
	self->tls = SSL_new(opts->tls_ctx);
	if (opts->tls_mode == TM_CLIENT)
	{
	    self->tls_is_client = 1;
	    if (opts->tls_noverify)
	    {
		SSL_set_verify(self->tls, SSL_VERIFY_NONE, 0);
	    }
	    else
	    {
		SSL_set1_host(self->tls, opts->tls_hostname);
	    }
	}
	SSL_set_fd(self->tls, fd);
	if (opts->tls_cert)
	{
	    if (!SSL_use_certificate(self->tls, opts->tls_cert))
	    {
		PSC_Log_msg(PSC_L_ERROR, "Error using certificate");
	    }
	    if (!SSL_use_PrivateKey(self->tls, opts->tls_key))
	    {
		PSC_Log_msg(PSC_L_ERROR, "Error using private key");
	    }
	    EVP_PKEY_free(opts->tls_key);
	    X509_free(opts->tls_cert);
	}
    }
    else
    {
	self->tls = 0;
    }
    self->tlsConnectTimer = 0;
    self->tls_connect_st = 0;
    self->tls_read_st = 0;
    self->tls_write_st = 0;
    self->tls_shutdown_st = 0;
    self->tls_readagain = 0;
    self->tls_noverify = opts->tls_noverify;
#endif
    self->blacklisthits = opts->blacklisthits;
    self->type = type;
    self->args.handling = 0;
    self->deleteScheduled = 0;
    self->wrbuflen = 0;
    self->wrbufpos = 0;
    self->nrecs = 0;
    self->nnotify = 0;
    self->rdtextsave = 0;
    if (type != CT_PIPEWR)
    {
	uint8_t *rdbuf = type == CT_PIPERD ? self->wrbuf : self->rdbuf;
	rdbuf[self->rdbufsz] = 0; // for receiving in text mode
    }
    PSC_Event_register(PSC_Service_readyRead(), self, readConnection, fd);
    if (type != CT_PIPERD)
    {
	PSC_Event_register(PSC_Service_readyWrite(), self,
		writeConnection, fd);
	PSC_Event_register(PSC_Service_eventsDone(), self,
		tryWrite, 0);
    }
    if (opts->createmode == CCM_CONNECTING)
    {
	self->connectTimer = PSC_Timer_create();
	PSC_Timer_setMs(self->connectTimer, CONNTIMEOUT);
	PSC_Event_register(PSC_Timer_expired(self->connectTimer), self,
		connectionTimeout, 0);
	PSC_Service_registerWrite(fd);
	PSC_Timer_start(self->connectTimer, 0);
	self->wrreg = 1;
    }
#ifdef WITH_TLS
    else if (self->tls && !self->tls_is_client)
    {
	self->tlsConnectTimer = PSC_Timer_create();
	PSC_Timer_setMs(self->tlsConnectTimer, CONNTIMEOUT);
	PSC_Event_register(PSC_Timer_expired(self->tlsConnectTimer), self,
		tlsHandshakeTimeout, 0);
	PSC_Event_register(PSC_Service_eventsDone(), self, handshakenow, 0);
	PSC_Timer_start(self->tlsConnectTimer, 0);
    }
#endif
    else
    {
	PSC_Service_registerRead(fd);
	self->rdreg = 1;
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

SOEXPORT const PSC_IpAddr *PSC_Connection_remoteIpAddr(
	const PSC_Connection *self)
{
    return self->ipAddr;
}

SOEXPORT const char *PSC_Connection_remoteAddr(const PSC_Connection *self)
{
    if (self->type == CT_PIPERD || self->type == CT_PIPEWR) return "pipe";
    if (self->ipAddr) return PSC_IpAddr_string(self->ipAddr);
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

SOEXPORT int PSC_Connection_receiveBinary(PSC_Connection *self,
	size_t expected)
{
    if (expected > self->rdbufsz) return -1;
    self->rdlocator = 0;
    self->rdexpect = expected;
    return 0;
}

SOEXPORT void PSC_Connection_receiveText(PSC_Connection *self,
	PSC_MessageEndLocator locator)
{
    self->rdlocator = locator;
}

SOEXPORT void PSC_Connection_receiveLine(PSC_Connection *self)
{
    self->rdlocator = locateeol;
}

SOLOCAL void PSC_Connection_setRemoteAddr(PSC_Connection *self,
	struct sockaddr *addr)
{
    PSC_IpAddr_destroy(self->ipAddr);
    free(self->addr);
    free(self->name);
    self->ipAddr = 0;
    self->addr = 0;
    self->name = 0;
    self->port = 0;
    PSC_IpAddr *ipAddr = PSC_IpAddr_fromSockAddr(addr);
    if (ipAddr)
    {
	self->ipAddr = ipAddr;
	self->port = PSC_IpAddr_port(ipAddr);
    }
}

SOLOCAL void PSC_Connection_setRemoteAddrStr(PSC_Connection *self,
	const char *addr)
{
    PSC_IpAddr_destroy(self->ipAddr);
    free(self->addr);
    free(self->name);
    self->ipAddr = 0;
    self->addr = PSC_copystr(addr);
    self->name = 0;
    self->port = 0;
}

SOEXPORT int PSC_Connection_sendAsync(PSC_Connection *self,
	const uint8_t *buf, size_t sz, void *id)
{
    if (self->type == CT_PIPERD) return -1;
    if (self->deleteScheduled) return -1;
    if (self->connectTimer) return -1;
#ifdef WITH_TLS
    if (self->tlsConnectTimer) return -1;
    if (self->tls_shutdown_st) return -1;
#endif
    if (self->nrecs == NWRITERECS)
    {
	PSC_Log_fmt(PSC_L_DEBUG, "connection: send queue overflow to %s",
		PSC_Connection_remoteAddr(self));
	return -1;
    }
    WriteRecord *rec = self->writerecs + self->nrecs++;
    PSC_Log_fmt(PSC_L_DEBUG, "connection: added send request to %s, "
	    "queue len: %hhu", PSC_Connection_remoteAddr(self), self->nrecs);
    rec->wrbuflen = sz;
    rec->wrbufpos = 0;
    rec->wrbuf = buf;
    rec->id = id;
    return 0;
}

SOEXPORT int PSC_Connection_sendTextAsync(PSC_Connection *self,
	const char *text, void *id)
{
    return PSC_Connection_sendAsync(self, (const uint8_t *)text,
	    strlen(text), id);
}

SOEXPORT void PSC_Connection_pause(PSC_Connection *self)
{
    ++self->paused;
    wantreadwrite(self);
}

SOEXPORT int PSC_Connection_resume(PSC_Connection *self)
{
    if (!self->paused) return -1;
    if (--self->paused) return 0;
    wantreadwrite(self);
    return 1;
}

SOEXPORT int PSC_Connection_confirmDataReceived(PSC_Connection *self)
{
    if (!self->args.handling) return -1;
    if (--self->args.handling) return 0;
    raisereceivedevents(self);
    wantreadwrite(self);
#ifdef WITH_TLS
    if (self->tls_readagain) doread(self);
#endif
    return 1;
}

SOEXPORT void PSC_Connection_close(PSC_Connection *self, int blacklist)
{
    if (self->deleteScheduled) return;
    if (blacklist && self->blacklisthits && self->ipAddr)
    {
	PSC_Connection_blacklistAddress(self->blacklisthits, self->ipAddr);
    }
    PSC_Event_raise(self->closed, 0, self->connectTimer ? 0 : self);
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

static void deleteLater(PSC_Connection *self)
{
    if (!self) return;
    if (!self->deleteScheduled)
    {
	if (self->rdreg) PSC_Service_unregisterRead(self->fd);
	self->rdreg = 0;
	PSC_Event_register(PSC_Service_eventsDone(), self,
		deleteConnection, 0);
	self->deleteScheduled = 1;
    }
}

SOLOCAL void PSC_Connection_destroy(PSC_Connection *self)
{
    if (!self) return;

    for (uint8_t notno = 0; notno < self->nnotify; ++notno)
    {
	if (self->writenotify[notno].id)
	{
	    PSC_Event_raise(self->dataSent, 0, self->writenotify[notno].id);
	}
    }
    for (uint8_t recno = 0; recno < self->nrecs; ++recno)
    {
	if (self->writerecs[recno].id)
	{
	    PSC_Event_raise(self->dataSent, 0, self->writerecs[recno].id);
	}
    }

    if (self->deleteScheduled)
    {
	if (self->deleteScheduled == 1) return;
	if (self->wrreg) PSC_Service_unregisterWrite(self->fd);
	self->wrreg = 0;
	close(self->fd);
	PSC_Event_unregister(PSC_Service_eventsDone(), self,
		deleteConnection, 0);
    }
    else
    {
#ifdef WITH_TLS
	if (self->tls) SSL_shutdown(self->tls);
#endif
	PSC_Event_raise(self->closed, 0, self->connectTimer ? 0 : self);
	if (self->rdreg) PSC_Service_unregisterRead(self->fd);
	if (self->wrreg) PSC_Service_unregisterWrite(self->fd);
	self->rdreg = 0;
	self->wrreg = 0;
	close(self->fd);
    }

#ifdef WITH_TLS
    SSL_free(self->tls);
    PSC_Timer_destroy(self->tlsConnectTimer);
    if (self->tls_is_client) PSC_Connection_unreftlsctx();
#endif
    PSC_Event_unregister(PSC_Service_eventsDone(), self,
	    tryWrite, 0);
    PSC_Event_unregister(PSC_Service_readyRead(), self,
	    readConnection, self->fd);
    PSC_Event_unregister(PSC_Service_readyWrite(), self,
	    writeConnection, self->fd);
    if (self->deleter) self->deleter(self->data);
    PSC_IpAddr_destroy(self->ipAddr);
    free(self->addr);
    free(self->name);
    PSC_Timer_destroy(self->connectTimer);
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

SOEXPORT size_t PSC_EADataReceived_size(const PSC_EADataReceived *self)
{
    return self->size;
}

SOEXPORT const char *PSC_EADataReceived_text(const PSC_EADataReceived *self)
{
    return self->text;
}

SOEXPORT void PSC_EADataReceived_markHandling(PSC_EADataReceived *self)
{
    ++self->handling;
}

