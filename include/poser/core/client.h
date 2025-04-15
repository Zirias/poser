#ifndef POSER_CORE_CLIENT_H
#define POSER_CORE_CLIENT_H

/** declarations for client construction of the PSC_Connection class
 * @file
 */

#include <poser/decl.h>

#include <poser/core/proto.h>
#include <stddef.h>

C_CLASS_DECL(PSC_Connection);

/** Options for creating a TCP client.
 * @class PSC_TcpClientOpts client.h <poser/core/client.h>
 */
C_CLASS_DECL(PSC_TcpClientOpts);

/** Options for creating a UNIX socket client.
 * @class PSC_UnixClientOpts client.h <poser/core/client.h>
 */
C_CLASS_DECL(PSC_UnixClientOpts);

/** Handler for completed async client creation.
 * This will be called once PSC_Connection_createTcpAsync created a
 * PSC_Connection object.
 * @param receiver the object receiving the callback
 * @param connection the newly created connection, or NULL when creation
 *                   failed
 */
typedef void (*PSC_ClientCreatedHandler)(
	void *receiver, PSC_Connection *connection);

/** PSC_TcpClientOpts constructor.
 * Creates an options object initialized to default values.
 * @memberof PSC_TcpClientOpts
 * @param remotehost host to connect to (name or address)
 * @param port port to connect to
 * @returns a newly created options object
 */
DECLEXPORT PSC_TcpClientOpts *
PSC_TcpClientOpts_create(const char *remotehost, int port)
    ATTR_RETNONNULL ATTR_NONNULL((1));

/** Set read buffer size.
 * Sets the size of the buffer used for reading from the connection, in bytes.
 * The default value is 16 kiB.
 * @memberof PSC_TcpClientOpts
 * @param self the PSC_TcpClientOpts
 * @param sz the size of the read buffer, must be > 0
 */
DECLEXPORT void
PSC_TcpClientOpts_readBufSize(PSC_TcpClientOpts *self, size_t sz)
    CMETHOD;

/** Enable TLS for the connection.
 * Enables TLS for the connection to be created, optionally using a client
 * certificate.
 * @memberof PSC_TcpClientOpts
 * @param self the PSC_TcpClientOpts
 * @param certfile certificate file for client certificate
 * @param keyfile private key file for client certificate
 */
DECLEXPORT void
PSC_TcpClientOpts_enableTls(PSC_TcpClientOpts *self,
	const char *certfile, const char *keyfile)
    CMETHOD;

/** Disable server certificate verification.
 * @memberof PSC_TcpClientOpts
 * @param self the PSC_TcpClientOpts
 */
DECLEXPORT void
PSC_TcpClientOpts_disableCertVerify(PSC_TcpClientOpts *self)
    CMETHOD;

/** Set a specific protocol (IPv4 or IPv6).
 * @memberof PSC_TcpClientOpts
 * @param self the PSC_TcpClientOpts
 * @param proto protocol the client should use
 */
DECLEXPORT void
PSC_TcpClientOpts_setProto(PSC_TcpClientOpts *self, PSC_Proto proto)
    CMETHOD;

/** Only use numeric hosts, don't attempt to resolve addresses.
 * @memberof PSC_TcpClientOpts
 * @param self the PSC_TcpClientOpts
 */
DECLEXPORT void
OBSOLETE(Resolving remote hosts is disabled; use PSC_Resolver instead)
PSC_TcpClientOpts_numericHosts(PSC_TcpClientOpts *self)
    CMETHOD;

/** Enable blacklisting of failed remote addresses.
 * When this is set to a non-zero value, a remote address is put on a
 * blacklist after errors or timeouts during connect or TLS hanshake, or when
 * closed with the blacklist parameter of PSC_Connection_close() set to 1.
 *
 * This can be useful for remote services using some load-balancing or
 * round-robin DNS. In this case, it can be avoided to try the same host over
 * and over again.
 * @memberof PSC_TcpClientOpts
 * @param self the PSC_TcpClientOpts
 * @param blacklistHits number of hits needed to remove the entry from the
 *                      blacklist again
 */
DECLEXPORT void
PSC_TcpClientOpts_setBlacklistHits(PSC_TcpClientOpts *self, int blacklistHits)
    CMETHOD;

/** PSC_TcpClientOpts destructor
 * @memberof PSC_TcpClientOpts
 * @param self the PSC_TcpClientOpts
 */
DECLEXPORT void
PSC_TcpClientOpts_destroy(PSC_TcpClientOpts *self);

/** PSC_UnixClientOpts constructor.
 * Creates an options object initialized to default values.
 * @memberof PSC_UnixClientOpts
 * @param sockname the name/path of the local socket to connect to
 * @returns a newly created options object
 */
DECLEXPORT PSC_UnixClientOpts *
PSC_UnixClientOpts_create(const char *sockname)
    ATTR_RETNONNULL ATTR_NONNULL((1));

/** Set read buffer size.
 * Sets the size of the buffer used for reading from the connection, in bytes.
 * The default value is 16 kiB.
 * @memberof PSC_UnixClientOpts
 * @param self the PSC_UnixClientOpts
 * @param sz the size of the read buffer, must be > 0
 */
DECLEXPORT void
PSC_UnixClientOpts_readBufSize(PSC_UnixClientOpts *self, size_t sz)
    CMETHOD;

/** PSC_UnixClientOpts destructor
 * @memberof PSC_UnixClientOpts
 * @param self the PSC_UnixClientOpts
 */
DECLEXPORT void
PSC_UnixClientOpts_destroy(PSC_UnixClientOpts *self);

/** Create a connection as a TCP client.
 * The created connection will be in a "connecting" state. To know when it is
 * successfully connected, you must listen on the PSC_Connection_connected()
 * event.
 * @memberof PSC_Connection
 * @param opts TCP client options
 * @returns a newly created connection object, or NULL when creation failed
 */
DECLEXPORT PSC_Connection *
PSC_Connection_createTcpClient(const PSC_TcpClientOpts *opts)
    ATTR_NONNULL((1));

/** Create a connection as a TCP client asynchronously.
 * To create a TCP client, it can be necessary to resolve a remote hostname,
 * and/or to read a client certificate to use with TLS. Using this function,
 * these things are done in background, so it is recommended to use this
 * function from within a running service. The connection is created when all
 * required information is present, and passed to the callback provided here.
 *
 * Note that the created connection will still be in a "connecting" state, so
 * you still have to listen on the PSC_Connection_connected() event to know
 * when it is successfully connected.
 * @memberof PSC_Connection
 * @param opts TCP client options
 * @param receiver the object to receive the callback (or NULL)
 * @param callback the callback function
 * @returns -1 on immediate error, 0 when in progress
 */
DECLEXPORT int
PSC_Connection_createTcpClientAsync(const PSC_TcpClientOpts *opts,
	void *receiver, PSC_ClientCreatedHandler callback)
    ATTR_NONNULL((1)) ATTR_NONNULL((3));

/** Create a connection as a UNIX socket client.
 * The created connection will be in a "connecting" state. To know when it is
 * successfully connected, you must listen on the PSC_Connection_connected()
 * event.
 * @memberof PSC_Connection
 * @param opts UNIX client options
 * @returns a newly created connection object, or NULL when creation failed
 */
DECLEXPORT PSC_Connection *
PSC_Connection_createUnixClient(const PSC_UnixClientOpts *opts)
    ATTR_NONNULL((1));

#endif
