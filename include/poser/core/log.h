#ifndef POSER_CORE_LOG_H
#define POSER_CORE_LOG_H

/** declarations for the PSC_Log class
 * @file
 */

#include <poser/decl.h>

#include <stdio.h>

/** A logging mechanism offering pluggable writers.
 * This is a simple logging mechanism that can use any write function to
 * implement different "log sinks". A function writing to an opened FILE and a
 * function writing to syslog are included. It can optionally log
 * asynchronously (making use of PSC_ThreadPool).
 *
 * By default, no log writer is configured, so any log messages are just
 * dropped. You have to set a log writer if you want to see log messages.
 *
 * Note that PSC_Service_Run() automatically configures log writers if
 * configured to do so via PSC_RunOpts.
 * @class PSC_Log log.h <poser/core/log.h>
 */

/** Maximum lenght for a log message.
 * This is the maximum length that can safely be used for a single log
 * message.
 */
#define PSC_MAXLOGLINE 16384

/** Log levels.
 */
typedef enum PSC_LogLevel
{
    PSC_L_FATAL,    /**< fatal condition, service must abort */
    PSC_L_ERROR,    /**< error condition, something can't complete */
    PSC_L_WARNING,  /**< something seems wrong and should get attention */
    PSC_L_INFO,	    /**< info logging to see what the service does */
    PSC_L_DEBUG	    /**< verbose debugging messages */
} PSC_LogLevel;

/** A log writer.
 * Called for any log message to actually write it somewhere.
 * @param level the log level
 * @param message the log message
 * @param data optional context data for the writer
 */
typedef void (*PSC_LogWriter)(PSC_LogLevel level,
	const char *message, void *data)
    ATTR_NONNULL((2));

/** Use a standard file logger.
 * @memberof PSC_Log
 * @static
 * @param file the opened file to write log messages to
 */
DECLEXPORT void
PSC_Log_setFileLogger(FILE *file)
    ATTR_NONNULL((1));

/** Use a standard syslog loggeer.
 * @memberof PSC_Log
 * @static
 * @param ident the name to use for syslog (name of your service)
 * @param facility the syslog facility (e.g. LOG_DAEMON)
 * @param withStderr (0) or (1): also write to stderr
 */
DECLEXPORT void
PSC_Log_setSyslogLogger(const char *ident, int facility, int withStderr)
    ATTR_NONNULL((1));

/** Use a custom log writer.
 * @memberof PSC_Log
 * @static
 * @param writer the log writer
 * @param data optional context data for the writer
 */
DECLEXPORT void
PSC_Log_setCustomLogger(PSC_LogWriter writer, void *data)
    ATTR_NONNULL((1));

/** Set maximum log level.
 * Only log messages up to this level. Highest: PSC_L_DEBUG, lowest:
 * PSC_L_FATAL. Default is PSC_L_INFO.
 * @memberof PSC_Log
 * @static
 * @param level the new maximum log level
 */
DECLEXPORT void
PSC_Log_setMaxLogLevel(PSC_LogLevel level);

/** Enable/disable silent mode.
 * This can be used to temporarily suppress all messages except PSC_L_FATAL
 * and PSC_L_ERROR, regardless of the maximum log level.
 * @memberof PSC_Log
 * @static
 * @param silent 1: enable silent mode, 0: disable silent mode
 */
DECLEXPORT void
PSC_Log_setSilent(int silent);

/** Enable/disable asynchronous logging.
 * In asynchronous mode, calling the actual log writer is done on a worker
 * thread, to avoid having to wait for I/O caused by it on the main thread.
 *
 * Asynchronous mode is disabled by default, but it is recommended to enable
 * it once your service is up and running, as long as you can't guarantee your
 * log writer will never block. Note that both writing to a file (on disk) and
 * writing to syslog *can* block.
 *
 * Also note PSC_Service_run() automatically enables asynchronous logging when
 * configured to handle logging via PSC_RunOpts.
 * @memberof PSC_Log
 * @static
 * @param async 1: enable asynchronous mode, 0: disable asynchronous mode
 */
DECLEXPORT void
PSC_Log_setAsync(int async);

/** Check whether log level is enabled.
 * Checks whether a given log level would currently produce a log message.
 * @memberof PSC_Log
 * @static
 * @param level the log level to check
 * @returns 1 if the level is enabled, 0 otherwise
 */
DECLEXPORT int
PSC_Log_enabled(PSC_LogLevel level);

/** Log a message.
 * @memberof PSC_Log
 * @static
 * @param level the log level
 * @param message the message
 */
DECLEXPORT void
PSC_Log_msg(PSC_LogLevel level, const char *message)
    ATTR_NONNULL((2));

/** Log a message using a prinft-like format string.
 * @memberof PSC_Log
 * @static
 * @param level the log level
 * @param format the printf-like format string
 * @param ... optional arguments for conversions in the format string
 */
DECLEXPORT void
PSC_Log_fmt(PSC_LogLevel level, const char *format, ...)
    ATTR_NONNULL((2)) ATTR_FORMAT((printf, 2, 3));

#endif

