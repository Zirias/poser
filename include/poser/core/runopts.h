#ifndef POSER_CORE_RUNOPTS_H
#define POSER_CORE_RUNOPTS_H

/** declarations for the PSC_RunOpts class
 * @file
 */

#include <poser/decl.h>

/** Options for running a service.
 * These options are used by PSC_Service and PSC_Daemon.
 * @class PSC_RunOpts runopts.h <poser/core/runopts.h>
 */

/** Initialize to default values.
 * Resets all run options to their default values. If this isn't explicitly
 * called, options are automatically initialized without a pidfile.
 * @memberof PSC_RunOpts
 * @static
 * @param pidfile the pidfile to use, or NULL for none
 */
DECLEXPORT void
PSC_RunOpts_init(const char *pidfile);

/** Run as a different user.
 * When a different user is set here, an attempt is made to switch to it
 * during service startup. This will only work when the service was launched
 * by the superuser (root).
 * @memberof PSC_RunOpts
 * @static
 * @param uid user-id to switch to, or -1 for no change
 * @param gid group-id to switch to, or -1 for no change
 */
DECLEXPORT void
PSC_RunOpts_runas(long uid, long gid);

/** Handle logging using default options.
 * This will automatically configure logging during service startup.
 *
 * When running daemonized (default), it will log to syslog as well as stderr
 * and stop logging to stderr as soon as the service startup completed. It
 * will also enable asynchronous logging after successful startup.
 *
 * When running in foreground, it will just configure logging to stderr.
 * @memberof PSC_RunOpts
 * @static
 * @param logident name to use for syslog logging (default: posercore)
 */
DECLEXPORT void PSC_RunOpts_enableDefaultLogging(const char *logident);

/** Run in foreground.
 * When this is set, PSC_Daemon_run() will directly call the daemon main
 * function without forking or handling a pidfile.
 * @memberof PSC_RunOpts
 * @static
 */
DECLEXPORT void
PSC_RunOpts_foreground(void);

/** Don't wait for successful service startup.
 * When this is set, the parent process will exit immediately instead of
 * waiting for PSC_Daemon_launched() to be called.
 * @memberof PSC_RunOpts
 * @static
 */
DECLEXPORT void
PSC_RunOpts_nowait(void);

/** Enable or disable using worker threads for additional event loops.
 * When this is set to a non-zero value, the service will launch additional
 * threads running their own event loop. These threads will be used for
 * handling connections accepted by a PSC_Server.
 * @memberof PSC_RunOpts
 * @static
 * @param workerThreads the number of worker threads to launch. If this number
 *                      is negative, launch as many worker threads as there
 *                      are CPU cores available, unless that number cannot be
 *                      determined, than use the absolute value as a fallback.
 */
DECLEXPORT void
PSC_RunOpts_workerThreads(int workerThreads);

#endif
