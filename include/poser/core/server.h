#ifndef POSER_CORE_SERVER_H
#define POSER_CORE_SERVER_H

/** declarations for the PSC_Server class
 * @file
 */
#include <poser/decl.h>

#include <poser/core/proto.h>

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

/** Set a specific protocol (IPv4 or IPv6).
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 * @param proto protocol the server should use
 */
DECLEXPORT void
PSC_TcpServerOpts_setProto(PSC_TcpServerOpts *self, PSC_Proto proto)
    CMETHOD;

/** Only use numeric hosts, don't attempt to resolve addresses.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 */
DECLEXPORT void
PSC_TcpServerOpts_numericHosts(PSC_TcpServerOpts *self)
    CMETHOD;

/** Wait before reading from newly accepted connections.
 * When set, a PSC_Connection for a new client will not immediately start
 * reading data, but wait for PSC_Connection_activate() to be called.
 * @memberof PSC_TcpServerOpts
 * @param self the PSC_TcpServerOpts
 */
DECLEXPORT void
PSC_TcpServerOpts_connWait(PSC_TcpServerOpts *self)
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

/** Wait before reading from newly accepted connections.
 * When set, a PSC_Connection for a new client will not immediately start
 * reading data, but wait for PSC_Connection_activate() to be called.
 * @memberof PSC_UnixServerOpts
 * @param self the PSC_UnixServerOpts
 */
DECLEXPORT void
PSC_UnixServerOpts_connWait(PSC_UnixServerOpts *self)
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

/** Client disconnected.
 * This event fires after a connection with a client was closed. The
 * PSC_Connection object of the closed connection is passed as the event
 * arguments.
 *
 * Note that when you already monitor the closed event of the PSC_Connection,
 * you will probably not need this event.
 * @memberof PSC_Server
 * @param self the PSC_Server
 * @returns the client disconnected event
 */
DECLEXPORT PSC_Event *
PSC_Server_clientDisconnected(PSC_Server *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** PSC_Server destructor.
 * This will close all active client connections and stop listening.
 * @memberof PSC_Server
 * @param self the PSC_Server
 */
DECLEXPORT void
PSC_Server_destroy(PSC_Server *self);

#endif
