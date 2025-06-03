#ifndef POSER_CORE_SERVICE_H
#define POSER_CORE_SERVICE_H

/** declarations for the PSC_Service class
 * @file
 */

#include <poser/decl.h>
#include <sys/types.h>

/** Maximum number of panic handlers that can be registered */
#define MAXPANICHANDLERS 8

/** Invalid exit code for a child process that was terminated by a signal */
#define PSC_CHILD_SIGNALED 256

/** A main service loop.
 * This class provides a service loop monitoring a set of file descriptors,
 * for example sockets or pipes, for read and/or write readiness. It prefers
 * doing this using platform-specific efficient APIs (kqueue on BSD systems,
 * epoll on Linux systems, event ports on Solaris/Illumos). The default
 * fallback if none of these APIs are available is POSIX select(), but as
 * a build-time option, poll() can be preferred.
 *
 * There's also a PSC_Service_run() function that can be used to automatically
 * provide daemonization, initialization and cleanup of the PSC_ThreadPool,
 * and logging configuration around the main event loop, all configurable
 * in PSC_RunOpts and PSC_ThreadOpts.
 *
 * Status changes on file descriptors are published as events. It also handles
 * standard signals for termination (SIGINT/SIGTERM), timers via PSC_Timer if
 * not using a different backend (SIGALRM) and child process events for
 * PSC_Process (SIGCHLD), and allows registering custom callbacks for any
 * signal.
 *
 * Finally, there's a panic function to quickly exit the loop and still give a
 * chance for some (minimal) cleanup.
 * @class PSC_Service service.h <poser/core/service.h>
 */

/** Event arguments for service (pre)startup events.
 * @class PSC_EAStartup service.h <poser/core/service.h>
 */
C_CLASS_DECL(PSC_EAStartup);

/** Event arguments for child exited events.
 * @class PSC_EAChildExited service.h <poser/core/service.h>
 */
C_CLASS_DECL(PSC_EAChildExited);

C_CLASS_DECL(PSC_Event);

/** A handler for a signal.
 * @param signo the signal number
 */
typedef void (*PSC_SignalHandler)(int signo);

/** A handler for a service panic.
 * @param msg the panic message
 */
typedef void (*PSC_PanicHandler)(const char *msg) ATTR_NONNULL((1));

/** A function to execute on a different worker thread.
 * @param arg optional argument
 */
typedef void (*PSC_OnThreadExec)(void *arg);

/** A file descriptor is ready for reading.
 * The file descriptor is used as the id of the event, so handlers must be
 * registered using the file descriptor of interest as the id. A pointer to
 * the id is also passed as the event arguments.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT PSC_Event *
PSC_Service_readyRead(void)
    ATTR_RETNONNULL ATTR_PURE;

/** A file descriptor is ready for writing.
 * The file descriptor is used as the id of the event, so handlers must be
 * registered using the file descriptor of interest as the id. A pointer to
 * the id is also passed as the event arguments.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT PSC_Event *
PSC_Service_readyWrite(void)
    ATTR_RETNONNULL ATTR_PURE;

/** The service is about to start.
 * This event fires early in the startup process. Especially, it fires before
 * any attempt to switch to a different user. Therefore, it is recommended to
 * use it to create TCP servers. This will allow to use low port numbers for
 * listening when launched as the superuser (root).
 *
 * A PSC_EAStartup object is passed as the event args. Startup can be aborted
 * by setting a non-0 return code on it.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT PSC_Event *
PSC_Service_prestartup(void)
    ATTR_RETNONNULL ATTR_PURE;

/** The service started.
 * This event fires after all other startup completed successfully. It can be
 * useful for any late initialization work. If you don't use
 * PSC_Service_run(), you might want to reconfigure logging here and call
 * Daemon_launched().
 *
 * A PSC_EAStartup object is passed as the event args. Startup can be aborted
 * by setting a non-0 return code on it.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT PSC_Event *
PSC_Service_startup(void)
    ATTR_RETNONNULL ATTR_PURE;

/** The service is shutting down.
 * This event fires when the service starts to shut down. Any cleanup should
 * be done here.
 *
 * Note you can delay the shutdown by calling PSC_Service_shutdownLock() if
 * you need to do some asynchronous cleanup work.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT PSC_Event *
PSC_Service_shutdown(void)
    ATTR_RETNONNULL ATTR_PURE;

/** All events for one loop iteration are processed.
 * This event fires once per loop iteration. It can be used to delay an action
 * until all events for the current iteration were processed.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT PSC_Event *
PSC_Service_eventsDone(void)
    ATTR_RETNONNULL ATTR_PURE;

/** A child process terminated.
 * This event fires whenever a child process exits.
 *
 * The pid is used as the id for the event, so handlers must be registered
 * for specific child processes.
 *
 * A PSC_EAChildExited object is passed as the event args, which allows to
 * retrieve the pid as well as the exit status or the terminating signal.
 *
 * Note that there's no guarantee that an int (used as the event id) can hold
 * all valid values of a pid_t (used for process ids), although this is
 * normally the case in practice. To be really sure, double-check the pid
 * from the event arguments when getting this event.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT PSC_Event *
PSC_Service_childExited(void)
    ATTR_RETNONNULL ATTR_PURE;

/** Lock handling child exits.
 * Use this before registering a handler for child exits to prevent race
 * conditions with quickly exiting processes when the service is running
 * with multiple worker threads.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT void
PSC_Service_lockChildren(void);

/** Unlock handling child exits.
 * Use this directly after registering a handler for child exits.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT void
PSC_Service_unlockChildren(void);

/** Check a file descriptor.
 * This checks whether a given file descriptor is valid for read/write
 * monitoring. Optionally log an appropriate error message if it isn't.
 * @memberof PSC_Service
 * @static
 * @param id the file descriptor to check
 * @param errortopic the topic for the error message (prepended with a colon).
 *                   NULL means don't log anything.
 * @returns 1 if valid for monitoring, 0 otherwise
 */
DECLEXPORT int
PSC_Service_isValidFd(int id, const char *errortopic);

/** Register a file descriptor for read monitoring.
 * For a file descriptor registered for read monitoring, a
 * PSC_Service_readyRead() event will be created when it's ready to read.
 * @memberof PSC_Service
 * @static
 * @param id the file descriptor to monitor
 */
DECLEXPORT void
PSC_Service_registerRead(int id);

/** Unregister a file descriptor for read monitoring.
 * PSC_Service_readyRead() events will no longer be created for the given file
 * descriptor.
 * @memberof PSC_Service
 * @static
 * @param id the file descriptor
 */
DECLEXPORT void
PSC_Service_unregisterRead(int id);

/** Register a file descriptor for write monitoring.
 * For a file descriptor registered for write monitoring, a
 * PSC_Service_readyWrite() event will be created when it's ready to write.
 * @memberof PSC_Service
 * @static
 * @param id the file descriptor to monitor
 */
DECLEXPORT void
PSC_Service_registerWrite(int id);

/** Unregister a file descriptor for write monitoring.
 * PSC_Service_readyWrite() events will no longer be created for the given file
 * descriptor.
 * @memberof PSC_Service
 * @static
 * @param id the file descriptor
 */
DECLEXPORT void
PSC_Service_unregisterWrite(int id);

/** Register a panic handler.
 * When PSC_Service_panic() is called, the registered handlers are called
 * before exiting the service loop.
 * @memberof PSC_Service
 * @static
 * @param handler the panic handler
 */
DECLEXPORT void
PSC_Service_registerPanic(PSC_PanicHandler handler)
    ATTR_NONNULL((1));

/** Unregister a panic handler.
 * The given handler is no longer called on a PSC_Service_panic().
 * @memberof PSC_Service
 * @static
 * @param handler the panic handler
 */
DECLEXPORT void
PSC_Service_unregisterPanic(PSC_PanicHandler handler)
    ATTR_NONNULL((1));

/** Register a signal handler.
 * The given handler is called in the regular event loop on the main thread
 * whenever the given signal was received.
 * Only a single handler per signal is possible, so a previously installed
 * handler is replaced. To stop handling the signal, pass NULL for the
 * handler.
 * There are platform-specific differences which signals exist at all and
 * which can be handled, so be careful if you want to use a signal not
 * specified by POSIX.
 * Also note installing a handler for SIGALRM is, although possible, useless.
 * It is used internally by PSC_Timer. Therefore also avoid calling any
 * functions yourself that might trigger a SIGALRM, this would break the
 * (de-)multiplexing for PSC_Timer.
 * @memberof PSC_Service
 * @static
 * @param signo the signal to handle
 * @param handler the signal handler callback
 */
DECLEXPORT void
PSC_Service_registerSignal(int signo, PSC_SignalHandler handler);

/** Run the service loop.
 * This runs the plain service loop. Callers are responsible to initialize a
 * PSC_ThreadPool if needed, to call PSC_Daemon_run() if needed, to configure
 * logging, etc. Use this if you need some custom setup. Otherwise, see
 * PSC_Service_run().
 * @memberof PSC_Service
 * @static
 * @returns an exit code
 */
DECLEXPORT int
PSC_Service_loop(void);

/** Run the service.
 * This automates everything needed for the service and runs the service loop.
 * For configuration, see PSC_RunOpts. It will typically be called at the end
 * of a main() function, after doing some configuration.
 * @memberof PSC_Service
 * @static
 * @returns an exit code
 */
DECLEXPORT int
PSC_Service_run(void);

/** Request the service to quit.
 * Once all events of the current loop iteration are processed, the service
 * loop will exit cleanly.
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT void
PSC_Service_quit(void);

/** Delay shutdown of the service.
 * To be used from the PSC_Service_shutdown() event, in case some asynchronous
 * cleanup work has to be done that needs the service loop active.
 *
 * Service shutdown will be delayed until PSC_Service_shutdownUnlock() was
 * called the same amount of times as PSC_Service_shutdownLock().
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT void
PSC_Service_shutdownLock(void);

/** Allow shutdown to continue.
 * See PSC_Service_shutdownLock()
 * @memberof PSC_Service
 * @static
 */
DECLEXPORT void
PSC_Service_shutdownUnlock(void);

/** Trigger a service panic.
 * A panic will jump directly back to the service loop and exit it
 * immediately. Registered panic handlers are called and basic cleanup is
 * attempted.
 * @memberof PSC_Service
 * @static
 * @param msg the panic message, will be logged at PSC_L_FATAL level
 */
DECLEXPORT void
PSC_Service_panic(const char *msg)
    ATTR_NONNULL((1)) ATTR_NORETURN;

/** Get the number of worker threads.
 * Returns the number of extra worker threads running.
 * @memberof PSC_Service
 * @static
 * @returns the number of worker threads
 */
DECLEXPORT int
PSC_Service_workers(void);

/** Get current worker thread number.
 * Returns the number of the current worker thread.
 * @memberof PSC_Service
 * @static
 * @returns the number of the current worker thread, or -1 if called from the
 *          main thread, or -2 if called from a thread not used by the service
 */
DECLEXPORT int
PSC_Service_threadNo(void);

/** Schedule a function for execution on a different thread.
 * The given function is scheduled for execution on the worker thread
 * specified by @p threadNo. If called from the target thread, the function
 * is executed immediately.
 * @memberof PSC_Service
 * @static
 * @param threadNo the number of the worker thread to execute on, or a
 *                 negative number for execution on the main thread
 * @param func the function to execute
 * @param arg an optional argument passed to the function
 */
DECLEXPORT void
PSC_Service_runOnThread(int threadNo, PSC_OnThreadExec func, void *arg)
    ATTR_NONNULL((2));

/** Return a status code from a (pre)startup event.
 * Call this to signal an error condition from the PSC_Service_prestartup() or
 * the PSC_Service_startup() event. A non-zero exit code will cause the
 * service loop to exit directly, returning this code.
 * @memberof PSC_EAStartup
 * @param self the PSC_EAStartup
 * @param rc the exit code to return
 */
DECLEXPORT void
PSC_EAStartup_return(PSC_EAStartup *self, int rc)
    CMETHOD;

/** The pid of a child that exited.
 * @memberof PSC_EAChildExited
 * @param self the PSC_EAChildExited
 * @returns the process id of the child
 */
DECLEXPORT pid_t
PSC_EAChildExited_pid(const PSC_EAChildExited *self)
    CMETHOD;

/** The exit status of a child that exited.
 * @memberof PSC_EAChildExited
 * @param self the PSC_EAChildExited
 * @returns the child's exit status, of PSC_CHILD_SIGNALED if the child was
 *	    terminated by a signal
 */
DECLEXPORT int
PSC_EAChildExited_status(const PSC_EAChildExited *self)
    CMETHOD;

/** The signal that terminated the child.
 * @memberof PSC_EAChildExited
 * @param self the PSC_EAChildExited
 * @returns the signal number of the signal terminating the child, or 0 if
 *          the child exited normally
 */
DECLEXPORT int
PSC_EAChildExited_signal(const PSC_EAChildExited *self)
    CMETHOD;

#endif
