#ifndef POSER_CORE_LOG_H
#define POSER_CORE_LOG_H

#include <poser/decl.h>

#include <stdio.h>

#define PSC_MAXLOGLINE 16384

typedef enum PSC_LogLevel
{
    PSC_L_FATAL,
    PSC_L_ERROR,
    PSC_L_WARNING,
    PSC_L_INFO,
    PSC_L_DEBUG
} PSC_LogLevel;

typedef void (*PSC_LogWriter)(PSC_LogLevel level,
	const char *message, void *data)
    ATTR_NONNULL((2));

DECLEXPORT void
PSC_Log_setFileLogger(FILE *file)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_Log_setSyslogLogger(const char *ident, int facility, int withStderr)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_Log_setCustomLogger(PSC_LogWriter writer, void *data)
    ATTR_NONNULL((1));

DECLEXPORT void
PSC_Log_setMaxLogLevel(PSC_LogLevel level);

DECLEXPORT void
PSC_Log_setSilent(int silent);

DECLEXPORT void
PSC_Log_setAsync(int async);

DECLEXPORT void
PSC_Log_msg(PSC_LogLevel level, const char *message)
    ATTR_NONNULL((2));

DECLEXPORT void
PSC_Log_fmt(PSC_LogLevel level, const char *format, ...)
    ATTR_NONNULL((2)) ATTR_FORMAT((printf, 2, 3));

#endif

