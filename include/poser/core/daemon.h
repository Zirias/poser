#ifndef POSER_CORE_DAEMON_H
#define POSER_CORE_DAEMON_H

/** declarations for the PSC_Daemon class
 * @file
 */

#include <poser/decl.h>

/** Run something as a daemon.
 * @class PSC_Daemon daemon.h <poser/core/daemon.h>
 */

/** Main function for a daemon.
 * This is called by PSC_Daemon_run() in a detached child process.
 * @param data pointer to some custom data
 * @returns an exit code
 */
typedef int (*PSC_Daemon_main)(void *data);

/** Run as a daemon.
 * Runs a given function in a detached child process and optionally handles a
 * pidfile. By default, it waits for successful startup of the daemon, passing
 * through stderr output of the child. The behavior can be configured using
 * PSC_Runopts.
 *
 * Note this is automatically used by PSC_Service_run().
 * @memberof PSC_Daemon
 * @static
 * @param dmain the main function for the daemon
 * @param data data to pass to the main function
 * @returns an exit code
 */
DECLEXPORT int
PSC_Daemon_run(PSC_Daemon_main dmain, void *data)
    ATTR_NONNULL((1));

/** Notify parent that the daemon successfully started.
 * When this is called from the running daemon, the parent process exits.
 *
 * Note this is automatically called by PSC_Service_run() if needed.
 * @memberof PSC_Daemon
 * @static
 */
DECLEXPORT void
PSC_Daemon_launched(void);

#endif
