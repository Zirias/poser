#include <poser/core/log.h>
#include <poser/core/threadpool.h>
#include <poser/core/util.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>

static PSC_LogWriter currentwriter = 0;
static void *writerdata;
static PSC_LogLevel maxlevel = PSC_L_INFO;
static int logsilent = 0;
static int logasync = 0;

static const char *levels[] =
{
    "[FATAL]",
    "[ERROR]",
    "[WARN ]",
    "[INFO ]",
    "[DEBUG]"
};

static int syslogLevels[] =
{
    LOG_CRIT,
    LOG_ERR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
};

typedef struct LogJobArgs
{
    PSC_LogLevel level;
    PSC_LogWriter writer;
    void *writerdata;
    char message[];
} LogJobArgs;

static void logmsgJobProc(void *arg);
static void writeFile(PSC_LogLevel level, const char *message, void *data)
    ATTR_NONNULL((2));
static void writeSyslog(PSC_LogLevel level, const char *message, void *data)
    ATTR_NONNULL((2));

static void logmsgJobProc(void *arg)
{
    LogJobArgs *lja = arg;
    lja->writer(lja->level, lja->message, lja->writerdata);
    free(lja);
}

static void writeFile(PSC_LogLevel level, const char *message, void *data)
{
    FILE *target = data;
    fprintf(target, "%s  %s\n", levels[level], message);
    fflush(target);
}

static void writeSyslog(PSC_LogLevel level, const char *message, void *data)
{
    (void)data;
    syslog(syslogLevels[level], "%s", message);
}

SOEXPORT void PSC_Log_setFileLogger(FILE *file)
{
    currentwriter = writeFile;
    writerdata = file;
}

SOEXPORT void PSC_Log_setSyslogLogger(const char *ident,
	int facility, int withStderr)
{
    int logopts = LOG_PID;
    if (withStderr) logopts |= LOG_PERROR;
    openlog(ident, logopts, facility);
    currentwriter = writeSyslog;
    writerdata = 0;
}

SOEXPORT void PSC_Log_setCustomLogger(PSC_LogWriter writer, void *data)
{
    currentwriter = writer;
    writerdata = data;
}

SOEXPORT void PSC_Log_setMaxLogLevel(PSC_LogLevel level)
{
    maxlevel = level;
}

SOEXPORT void PSC_Log_setSilent(int silent)
{
    logsilent = silent;
}

SOEXPORT void PSC_Log_setAsync(int async)
{
    logasync = async;
}

SOEXPORT void PSC_Log_msg(PSC_LogLevel level, const char *message)
{
    if (!currentwriter) return;
    if (logsilent && level > PSC_L_ERROR) return;
    if (level > maxlevel) return;
    if (logasync && PSC_ThreadPool_active())
    {
	size_t msgsize = strlen(message)+1;
	LogJobArgs *lja = PSC_malloc(sizeof *lja + msgsize);
	lja->level = level;
	lja->writer = currentwriter;
	lja->writerdata = writerdata;
	strcpy(lja->message, message);
	PSC_ThreadJob *job = PSC_ThreadJob_create(logmsgJobProc, lja, 8);
	PSC_ThreadPool_enqueue(job);
    }
    else currentwriter(level, message, writerdata);
}

SOEXPORT void PSC_Log_fmt(PSC_LogLevel level, const char *format, ...)
{
    if (!currentwriter) return;
    if (logsilent && level > PSC_L_ERROR) return;
    if (level > maxlevel) return;
    char buf[PSC_MAXLOGLINE];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, PSC_MAXLOGLINE, format, ap);
    va_end(ap);
    PSC_Log_msg(level, buf);
}

