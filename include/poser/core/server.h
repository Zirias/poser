#ifndef POSER_CORE_SERVER_H
#define POSER_CORE_SERVER_H

/** declarations for the PSC_Server class
 * @file
 */
#include <poser/decl.h>

#include <poser/core/certinfo.h>
#include <poser/core/proto.h>
#include <stddef.h>

/** A server listening on a socket and accepting connections.
 * This class will open one or multiple listening sockets and handle incoming
 * connections by creating a PSC_Connection for them and firing an event.
 * @class PSC_Server server.h <poser/core/server.h>
 */
C_CLASS_DECL(PSC_Server);

/** Options for creating a TCP server.
 * @class PSC_TcpServerOpts server.h <poser/core/server.h>
 */
C_CLASS_DECL(PSC_TcpServerOpts);

/** Options for creating a local UNIX server.
 * @class PSC_UnixServerOpts server.h <poser/core/server.h>
 */
C_CLASS_DECL(PSC_UnixServerOpts);

C_CLASS_DECL(PSC_Event);

/** PSC_TcpServerOpts constructor.
 * Creates an options object initialized to default values.
 * @memberof PSC_TcpServerOpts
 * @param port the port to listen on
 * @returns a newly created options object
 */
DECLEXPORT PSC_TcpServerOpts *
PSC_TcpServerOpts_create(int port)
    ATTR_RETNONNULL;

/** Bind to a specific hostname or address.
 * This can be called multiple times to bind to multiple names or addresses.
 * If it isn't called at all, the server will listen on any interface/address.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 * @param bindhost hostname or address to bind to
 */
DECLEXPORT void
PSC_TcpServerOpts_bind(PSC_TcpServerOpts *self, const char *bindhost)
    CMETHOD ATTR_NONNULL((2));

/** Set read buffer size.
 * Sets the size of the buffer used for connections accepted from this server,
 * in bytes. The default value is 16 kiB.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 * @param sz the size of the read buffer, must be > 0
 */
DECLEXPORT void
PSC_TcpServerOpts_readBufSize(PSC_TcpServerOpts *self, size_t sz)
    CMETHOD;

/** Enable TLS for the server.
 * Causes TLS to be enabled for any incoming connection, using a server
 * certificate. Note the certificate is required.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 * @param certfile certificate file for the server certificate
 * @param keyfile private key file for the server certificate
 */
DECLEXPORT void
PSC_TcpServerOpts_enableTls(PSC_TcpServerOpts *self,
	const char *certfile, const char *keyfile)
    CMETHOD ATTR_NONNULL((2)) ATTR_NONNULL((3));

/** Enable checking of an optional client certificate.
 * If the client presents a client certificate, enable checking it. When a CA
 * file is given, the certificate must be issued from one of the CAs contained
 * in it. When the client presents a client certificate that doesn't validate,
 * handshake fails.
 *
 * If no CA file is given, any client certificate will fail validation unless
 * a custom validation function is configured with
 * PSC_TcpServerOpts_validateClientCert().
 *
 * To strictly require a client certificate, use
 * PSC_TcpServerOpts_requireClientCert() instead.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 * @param cafile CA file (containing PEM certificates)
 */
DECLEXPORT void
PSC_TcpServerOpts_enableClientCert(PSC_TcpServerOpts *self,
	const char *cafile)
    CMETHOD;

/** Request a certificate from connecting clients.
 * Causes the server to request a client certificate from every connecting
 * client. If the client doesn't present a certificate, or the certificate
 * is not signed by a CA present in the given CA file, handshake fails.
 *
 * If no CA file is given, any client certificate will fail validation unless
 * a custom validation function is configured with
 * PSC_TcpServerOpts_validateClientCert().
 *
 * To optionally enable validation of a client certificate if presented, use
 * PSC_TcpServerOpts_enableClientCert() instead.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 * @param cafile CA file (containing PEM certificates)
 */
DECLEXPORT void
PSC_TcpServerOpts_requireClientCert(PSC_TcpServerOpts *self,
	const char *cafile)
    CMETHOD;

/** Configure a custom validator for client certificates.
 * When this is used, the given validator will be called after default
 * validation of client certificates, so the application can still reject
 * or accept certificates based on custom logic.
 *
 * One of PSC_TcpServerOpts_enableClientCert() or
 * PSC_TcpServerOpts_requireClientCert() must be called for this to have any
 * effect. If a CA file is given there, this callback will only be called
 * after successful validation against the CA file.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 * @param receiver the object handling the validation (or 0 for static)
 * @param validator the custom validator function
 */
DECLEXPORT void
PSC_TcpServerOpts_validateClientCert(PSC_TcpServerOpts *self, void *receiver,
	PSC_CertValidator validator)
    CMETHOD ATTR_NONNULL((3));

/** Set a specific protocol (IPv4 or IPv6).
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 * @param proto protocol the server should use
 */
DECLEXPORT void
PSC_TcpServerOpts_setProto(PSC_TcpServerOpts *self, PSC_Proto proto)
    CMETHOD;

/** PSC_TcpServerOpts destructor.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 */
DECLEXPORT void
PSC_TcpServerOpts_destroy(PSC_TcpServerOpts *self);

/** PSC_UnixServerOpts constructor.
 * Creates an options object initialized to default values.
 * @memberof PSC_UnixServerOpts
 * @param name the file name (path) of the socket to listen on
 * @returns a newly created options object
 */
DECLEXPORT PSC_UnixServerOpts *
PSC_UnixServerOpts_create(const char *name)
    ATTR_RETNONNULL ATTR_NONNULL((1));

/** Set read buffer size.
 * Sets the size of the buffer used for connections accepted from this server,
 * in bytes. The default value is 16 kiB.
 * @memberof PSC_UnixServerOpts
 * @param self the PSC_UnixServerOpts
 * @param sz the size of the read buffer, must be > 0
 */
DECLEXPORT void
PSC_UnixSeverOpts_readBufSize(PSC_UnixServerOpts *self, size_t sz)
    CMETHOD;

/** Set ownership of the UNIX socket.
 * When set, an attempt is made to change ownership of the socket.
 * @memberof PSC_UnixServerOpts
 * @param self the PSC_UnixServerOpts
 * @param uid desired owner for the socket, -1 for no change
 * @param gid desired group for the socket, -1 for no change
 */
DECLEXPORT void
PSC_UnixServerOpts_owner(PSC_UnixServerOpts *self, int uid, int gid)
    CMETHOD;

/** Set access mode for the UNIX socket.
 * When not set, the default mode is 0600, so only the owner can interact with
 * the socket.
 * @memberof PSC_UnixServerOpts
 * @param self the PSC_UnixServerOpts
 * @param mode desired access mode
 */
DECLEXPORT void
PSC_UnixServerOpts_mode(PSC_UnixServerOpts *self, int mode)
    CMETHOD;

/** PSC_UnixServerOpts destructor.
 * @memberof PSC_UnixServerOpts
 * @param self the PSC_UnixServerOpts
 */
DECLEXPORT void
PSC_UnixServerOpts_destroy(PSC_UnixServerOpts *self);

/** Create a TCP server.
 * @memberof PSC_Server
 * @param opts TCP server options
 * @returns a newly created server, or NULL on error
 */
DECLEXPORT PSC_Server *
PSC_Server_createTcp(const PSC_TcpServerOpts *opts)
    ATTR_NONNULL((1));

/** Reconfigure a running TCP server.
 * Try to apply a new configuration to an already running server. The port,
 * protocol preference, read buffer size and list of bind addresses cannot
 * be changed at runtime. If the configuration is the same as before, this
 * silently succeeds.
 * @memberof PSC_Server
 * @param self the PSC_Server
 * @param opts the new TCP server options
 * @returns 0 on success, -1 if the new configuration can't be applied or the
 *            server is not a TCP server.
 */
DECLEXPORT int
PSC_Server_configureTcp(PSC_Server *self, const PSC_TcpServerOpts *opts)
    CMETHOD ATTR_NONNULL((1));

/** Create a local UNIX server.
 * @memberof PSC_Server
 * @param opts UNIX server options
 * @returns a newly created server, or NULL on error
 */
DECLEXPORT PSC_Server *
PSC_Server_createUnix(const PSC_UnixServerOpts *opts)
    ATTR_NONNULL((1));

/** New client connected.
 * This event fires when a new client connected and the connection was
 * accepted. The PSC_Connection object for the new client is passed as the
 * event arguments.
 * @memberof PSC_Server
 * @param self the PSC_Server
 * @returns the client connected event
 */
DECLEXPORT PSC_Event *
PSC_Server_clientConnected(PSC_Server *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Disable the server.
 * This disables accepting new connections while still listening. It's
 * implemented by immediately closing any new connection with a linger timeout
 * of 0, which should be signaled as an error to the client trying to connect.
 * For a TCP server, it means immediately sending an RST packet.
 *
 * Note this does not affect already existing connections.
 * @memberof PSC_Server
 * @param self the PSC_Server
 */
DECLEXPORT void
PSC_Server_disable(PSC_Server *self)
    CMETHOD;

/** Enable the server.
 * This enables accepting new connections again after PSC_Server_disable() was
 * called.
 * @memberof PSC_Server
 * @param self the PSC_Server
 */
DECLEXPORT void
PSC_Server_enable(PSC_Server *self)
    CMETHOD;

/** Graceful server shutdown.
 * This will stop listening, but defer destruction until all client
 * connections are closed or the given timeout is reached. The server is
 * finally destroyed. This could also happen immediately if there aren't any
 * active client connections, so the object should be considered invalid
 * after calling this method.
 * @memberof PSC_Server
 * @param self the PSC_Server
 * @param timeout timeout in milliseconds before forcing destructions. If set
 *                to 0, destruction is not forced until the PSC_Service exits.
 */
DECLEXPORT void
PSC_Server_shutdown(PSC_Server *self, unsigned timeout);

/** PSC_Server destructor.
 * This will close all active client connections and stop listening.
 * @memberof PSC_Server
 * @param self the PSC_Server
 */
DECLEXPORT void
PSC_Server_destroy(PSC_Server *self);

#endif
