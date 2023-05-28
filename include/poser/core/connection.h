#ifndef POSER_CORE_CONNECTION_H
#define POSER_CORE_CONNECTION_H

/** declarations for the PSC_Connection class
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>
#include <stdint.h>

/** A socket connection.
 * This class offers reading from and writing to a socket (TCP or local UNIX)
 * and retreiving basic peer information.
 *
 * A connection cannot be created directly. It's either created as a client,
 * or obtained from a PSC_Server when a client connected there.
 *
 * There's no destructor either, a connection destroys itself after being
 * closed and having done the necessary cleanup. Note that it automatically
 * closes on any errors, so if you need to know about it, you should listen on
 * the PSC_Connection_closed() event.
 * @class PSC_Connection connection.h <poser/core/connection.h>
 */
C_CLASS_DECL(PSC_Connection);

/** Event arguments for data received on a connection.
 * @class PSC_EADataReceived connection.h <poser/core/connection.h>
 */
C_CLASS_DECL(PSC_EADataReceived);

C_CLASS_DECL(PSC_Event);

/** Connection successfully connected.
 * This event fires as soon as a connection is fully connected and therefore
 * ready to communicate. Note this never fires on connections obtained from a
 * PSC_Server, they are already connected.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the connected event
 */
DECLEXPORT PSC_Event *
PSC_Connection_connected(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Connection closed.
 * This event fires when the connection was closed, either by calling
 * PSC_Connection_close(), or because the peer closed it, or because of any
 * error.
 *
 * If the connection was fully connected before, it passes itself as the event
 * arguments. Otherwise, the event args will be NULL. That way, it's possible
 * to know whether there was an error during establishing the connection.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the closed event
 */
DECLEXPORT PSC_Event *
PSC_Connection_closed(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Data received.
 * This event fires whenever there was new data received on the socket. It
 * passes a PSC_EADataReceived instance as event arguments, from which you can
 * access a buffer and its size.
 *
 * You can also mark the buffer as being handled. This will stop receiving
 * more data (which would overwrite the buffer) until you explicitly call
 * PSC_Connection_confirmDataReceived().
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the data received event
 */
DECLEXPORT PSC_Event *
PSC_Connection_dataReceived(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Data sent.
 * This event fires when data passed to PSC_Connection_sendAsync() was sent.
 * It only fires when an "id" object was passed with the data. This object is
 * passed back via event args, so you can identify which write completed.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the data sent event
 */
DECLEXPORT PSC_Event *
PSC_Connection_dataSent(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Remote name resolved.
 * This event fires as soon as the name of the peer was successfully resolved.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the name resolved event
 */
DECLEXPORT PSC_Event *
PSC_Connection_nameResolved(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** The remote address.
 * The address of the peer. For TCP connections, an IPv4 or IPv6 address. For
 * local UNIX connections, the name of the socket.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the remote address
 */
DECLEXPORT const char *
PSC_Connection_remoteAddr(const PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** The remote hostname.
 * The hostname of the peer. For local UNIX sockets, or if not resolved yet,
 * or if resolving is disabled: NULL.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the remote hostname
 */
DECLEXPORT const char *
PSC_Connection_remoteHost(const PSC_Connection *self)
    CMETHOD ATTR_PURE;

/** The remote port.
 * For local UNIX sockets, this returns 0.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the remote port
 */
DECLEXPORT int
PSC_Connection_remotePort(const PSC_Connection *self)
    CMETHOD ATTR_PURE;

/** Send data to the peer.
 * The data passed is scheduled for sending and sent as soon as the socket is
 * ready for sending. If an id is given, a PSC_Connection_dataSent() event
 * fires as soon as the data was actually sent, passing back the id as the
 * event args.
 *
 * **WARNING:** The data you pass is not (immediately) copied, instead a
 * pointer to it is saved to copy data into the sending buffer as soon as the
 * socket is ready for sending and there's enough room.
 *
 * As a consequence, **do not pass a pointer to an object with automatic
 * storage duration!** (IOW, do not pass a pointer to some local variable).
 * Doing so will not work and, in the worst case, crash your service.
 *
 * Instead, either use a static buffer (static storage duration or a member of
 * your own dynamically allocated object) or dynamically allocate a buffer
 * that you can destroy again from a PSC_Connection_dataSent() handler.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @param buf pointer to the data
 * @param sz size of the data
 * @param id optional identifier object
 * @returns -1 on immediate error, 0 when sending is scheduled
 */
DECLEXPORT int
PSC_Connection_sendAsync(PSC_Connection *self,
	const uint8_t *buf, size_t sz, void *id)
    CMETHOD ATTR_NONNULL((2));

/** Pause receiving data.
 * Stop receiving further data unless PSC_Connection_resume() is called. For
 * each call to PSC_Connection_pause(), a corresponding call to
 * PSC_Connection_resume() is necessary.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 */
DECLEXPORT void
PSC_Connection_pause(PSC_Connection *self)
    CMETHOD;

/** Resume receiving data.
 * Allow receiving of new data again after calling PSC_Connection_pause().
 * Must be called the same number of times to actually resume receiving.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns 1 when receiving was resumed, 0 when still paused, -1 when
 *          receiving wasn't paused
 */
DECLEXPORT int
PSC_Connection_resume(PSC_Connection *self)
    CMETHOD;

/** Confirm receiving data is completed.
 * This reactivates reading from the connection after it was paused by calling
 * PSC_EADataReceived_markHandling() in a data received event handler, to
 * signal the buffer is still being processed. For every event handler setting
 * the mark, a call to this function is needed to actually resume receiving.
 *
 * Note this is independent from the PSC_Connection_pause() mechanism. A
 * PSC_Connection will only receive data when it isn't paused and no
 * PSC_EADataReceived is marked as being handled.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns 1 when receiving was resumed, 0 when still paused, -1 when
 *          receiving wasn't paused
 */
DECLEXPORT int
PSC_Connection_confirmDataReceived(PSC_Connection *self)
    CMETHOD;

/** Close connection.
 * This initiates closing the connection. If TLS is enabled, the TLS shutdown
 * is initiated immediately, and completed asynchronously if necessary.
 *
 * A closed event is fired (either immediately or after completing the TLS
 * shutdown) and then, the connection is scheduled for deletion after all
 * events were handled, which will also close the socket.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @param blacklist (0) or (1): also blacklist the socket address, so it won't
 *                  be reused immediately, use when the peer behaved
 *                  erroneously
 */
DECLEXPORT void
PSC_Connection_close(PSC_Connection *self, int blacklist)
    CMETHOD;

/** Attach a data object.
 * Attach some custom data to the connection.
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @param data the data to attach
 * @param deleter if not NULL, called on the data when the connection is
 *                destroyed or some different data is attached
 */
DECLEXPORT void
PSC_Connection_setData(PSC_Connection *self,
	void *data, void (*deleter)(void *))
    CMETHOD;

/** Retreive attached data object.
 * Get data that was attached with PSC_Connection_setData().
 * @memberof PSC_Connection
 * @param self the PSC_Connection
 * @returns the currently attached data
 */
DECLEXPORT void *
PSC_Connection_data(const PSC_Connection *self)
    CMETHOD ATTR_PURE;

/** The data received.
 * Get a pointer to the data received on a connection.
 * @memberof PSC_EADataReceived
 * @param self the PSC_EADataReceived
 * @returns a pointer to received data
 */
DECLEXPORT const uint8_t *
PSC_EADataReceived_buf(const PSC_EADataReceived *self)
    CMETHOD ATTR_RETNONNULL;

/** The size of the data received.
 * Get the size of the received data (in bytes).
 * @memberof PSC_EADataReceived
 * @param self the PSC_EADataReceived
 * @returns the size of the data
 */
DECLEXPORT size_t
PSC_EADataReceived_size(const PSC_EADataReceived *self)
    CMETHOD;

/** Mark received data as being handled.
 * Calling this makes the PSC_Connection stop receiving further data unless
 * PSC_Connection_confirmDataReceived() is called. Each call to this function
 * requires a corresponding call to PSC_Connection_confirmDataReceived() to
 * actually resume receiving.
 * @memberof PSC_EADataReceived
 * @param self the PSC_EADataReceived
 */
DECLEXPORT void
PSC_EADataReceived_markHandling(PSC_EADataReceived *self)
    CMETHOD;

#endif
