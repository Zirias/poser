#ifndef POSER_CORE_PROCESS_H
#define POSER_CORE_PROCESS_H

/** declarations for the PSC_Process class
 * @file
 */

#include <poser/decl.h>
#include <sys/types.h>

/** The default exit status used for exec() errors */
#define PSC_ERR_EXEC 127

/** Standard I/O streams of the child process */
typedef enum PSC_StreamType
{
    PSC_ST_STDIN,	/**< standard input */
    PSC_ST_STDOUT,	/**< standard output */
    PSC_ST_STDERR	/**< standard error */
} PSC_StreamType;

/** What to do with a standard I/O stream in a child process */
typedef enum PSC_StreamAction
{
    PSC_SA_LEAVE,	/**< leave it alone (inherit from parent) */
    PSC_SA_CLOSE,	/**< close it, so the child can't use it */
    PSC_SA_NULL,	/**< redirect it to /dev/null (silence) */
    PSC_SA_PIPE		/**< redirect it to a pipe */
} PSC_StreamAction;

/** A child process.
 * This class offers creating and controlling a child process, optionally
 * opening pipes for the standard I/O streams that are represented by
 * instances of PSC_Connection.
 *
 * There is no explicit destructor. A PSC_Process object destroys itself
 * automatically when the child process exited and, if configured, the
 * pipes for standard output and standard error are closed.
 *
 * A hung process can be stopped with PSC_Process_stop().
 * @class PSC_Process process.h <poser/core/process.h>
 */
C_CLASS_DECL(PSC_Process);

/** Creation options for a child process.
 * @class PSC_ProcessOpts process.h <poser/core/process.h>
 */
C_CLASS_DECL(PSC_ProcessOpts);

/** Event arguments for a finished child process.
 * @class PSC_EAProcessDone process.h <poser/core/process.h>
 */
C_CLASS_DECL(PSC_EAProcessDone);

C_CLASS_DECL(PSC_Connection);
C_CLASS_DECL(PSC_Event);

/** A callback to receive a PSC_Connection for a standard I/O stream.
 * When redirection to a pipe is requested for one of the standard I/O
 * streams, this is called with the stream identifier and a PSC_Connection
 * object representing the parent's end of the pipe.
 * @param obj some object reference
 * @param type the standard I/O stream
 * @param conn the PSC_Connection to use
 */
typedef void (*PSC_StreamCallback)(void *obj,
	PSC_StreamType type, PSC_Connection *conn);

/** A main function for a child process.
 * This is called in a child process by PSC_Process_run().
 * @param argc the number of arguments, including the process name, which
 *             will be NULL if not specified explicitly
 * @param argv the array of arguments
 * @returns an exit status [-128..127]
 */
typedef int (*PSC_ProcessMain)(int argc, char **argv);

/** PSC_ProcessOpts default constructor.
 * Creates a new PSC_ProcessOpts
 * @memberof PSC_ProcessOpts
 * @returns a newly created PSC_ProcessOpts
 */
DECLEXPORT PSC_ProcessOpts *
PSC_ProcessOpts_create(void)
    ATTR_RETNONNULL;

/** Set the name of the child process.
 * If set, this is passed as argv[0], and also becomes the display name for
 * tools like ps(1) when PSC_Process_exec() is used. If not set, argv[0]
 * will be NULL for PSC_Process_run(), while PSC_Process_exec() will default
 * to the path executed.
 * @memberof PSC_ProcessOpts
 * @param self the PSC_ProcessOpts
 * @param name the process name
 */
DECLEXPORT void
PSC_ProcessOpts_setName(PSC_ProcessOpts *self, const char *name)
    CMETHOD ATTR_NONNULL((2));

/** Add a command-line argument.
 * All arguments added here are passed in argv[] to the child process.
 * @memberof PSC_ProcessOpts
 * @param self the PSC_ProcessOpts
 * @param arg the argment to add
 */
DECLEXPORT void
PSC_ProcessOpts_addArg(PSC_ProcessOpts *self, const char *arg)
    CMETHOD ATTR_NONNULL((2));

/** Configure an action for a standard I/O stream of the child process.
 * @memberof PSC_ProcessOpts
 * @param self the PSC_ProcessOpts
 * @param stream which stream to configure
 * @param action what to do with the stream
 * @returns 0 on success, -1 if invalid values were passed
 */
DECLEXPORT int
PSC_ProcessOpts_streamAction(PSC_ProcessOpts *self, PSC_StreamType stream,
	PSC_StreamAction action)
    CMETHOD;

/** Configure which exit status to use to indicate exec() errors.
 * If not set, the default value PSC_ERR_EXEC (127) is used, which is also
 * what most shells use. This should be a status code (in the range
 * [-128..127]) the program you intend to execute never uses. 127 is typically
 * a safe choice.
 * @memberof PSC_ProcessOpts
 * @param self the PSC_ProcessOpts
 * @param execError the status code to use for exec() errors
 * @returns 0 on success, -1 if an invalid status code was passed
 */
DECLEXPORT int
PSC_ProcessOpts_setExecError(PSC_ProcessOpts *self, int execError)
    CMETHOD;

/** PSC_ProcessOpts destructor.
 * @memberof PSC_ProcessOpts
 * @param self the PSC_ProcessOpts
 */
DECLEXPORT void
PSC_ProcessOpts_destroy(PSC_ProcessOpts *self);

/** PSC_Process default constructor.
 * Creates a new PSC_Process configured as given by the PSC_ProcessOpts
 * passed.
 * @memberof PSC_Process
 * @param opts the options for the process
 */
DECLEXPORT PSC_Process *
PSC_Process_create(const PSC_ProcessOpts *opts)
    ATTR_RETNONNULL;

/** Execute an external program in the child process.
 * @memberof PSC_Process
 * @param self the PSC_Process
 * @param obj some object reference for the callback @p cb
 * @param cb a callback to receive PSC_Connection objects for configured
 *           pipes. This must be given if any pipes are configured.
 * @param path the path to the external program
 * @returns 0 on success, -1 on error
 */
DECLEXPORT void
PSC_Process_exec(PSC_Process *self, void *obj, PSC_StreamCallback cb,
	const char *path)
    CMETHOD ATTR_NONNULL((3));

/** Execute a given function in the child process.
 * @memberof PSC_Process
 * @param self the PSC_Process
 * @param obj some object reference for the callback @p cb
 * @param cb a callback to receive PSC_Connection objects for configured
 *           pipes. This must be given if any pipes are configured.
 * @param main the function to execute in the child
 * @returns 0 on success, -1 on error
 */
DECLEXPORT void
PSC_Process_run(PSC_Process *self, void *obj, PSC_StreamCallback cb,
	PSC_ProcessMain main)
    CMETHOD ATTR_NONNULL((3));

/** Send signals to stop the child process.
 * This sends the child process a TERM signal, and optionally later a KILL
 * signal. Fails if the child process is not running or sending a KILL signal
 * is already scheduled.
 * @memberof PSC_Process
 * @param self the PSC_Process
 * @param forceMs if not 0, schedule sending a KILL signal in @p forceMs
 *                milliseconds if the child didn't terminate until then
 * @returns 0 on success, -1 on error
 */
DECLEXPORT int
PSC_Process_stop(PSC_Process *self, unsigned forceMs)
    CMETHOD;

/** The child process finished.
 * This event fires when the child process terminated, and, if there are pipes
 * configured for standard output and/or standard error, these pipes were
 * closed, for example by reaching EOF. It passes a PSC_EAProcessDone instance
 * as the event argument.
 * @memberof PSC_Process
 * @param self the PSC_Process
 * @returns the done event
 */
DECLEXPORT PSC_Event *
PSC_Process_done(PSC_Process *self)
    CMETHOD ATTR_RETNONNULL;

/** The process id of the child process.
 * This fails if the child is not currently running.
 * @memberof PSC_Process
 * @param self the PSC_Process
 * @returns the process id, or -1 on error
 */
DECLEXPORT pid_t
PSC_Process_pid(const PSC_Process *self)
    CMETHOD;

/** The exit status of the child process.
 * @memberof PSC_EAProcessDone
 * @param self the PSC_EAProcessDone
 * @returns the child's exit status, or PSC_CHILD_SIGNALED if the child was
 *          terminated by a signal
 */
DECLEXPORT int
PSC_EAProcessDone_status(const PSC_EAProcessDone *self)
    CMETHOD;

/** The signal that terminated the child process.
 * @memberof PSC_EAProcessDone
 * @param self the PSC_EAProcessDone
 * @returns the signal that terminated the child process, or 0 if it exited
 *          normally
 */
DECLEXPORT int
PSC_EAProcessDone_signal(const PSC_EAProcessDone *self)
    CMETHOD;

#endif
