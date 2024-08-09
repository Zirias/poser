#define _DEFAULT_SOURCE

#include "event.h"
#include "log.h"
#include "runopts.h"
#include "service.h"
#include "timer.h"

#include <poser/core/daemon.h>
#include <poser/core/threadpool.h>

#include <grp.h>
#include <setjmp.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <syslog.h>
#include <unistd.h>

#ifndef DEFLOGIDENT
#define DEFLOGIDENT "posercore"
#endif

struct PSC_EAStartup
{
    int rc;
};

static PSC_Event readyRead;
static PSC_Event readyWrite;
static PSC_Event prestartup;
static PSC_Event startup;
static PSC_Event shutdown;
static PSC_Event tick;
static PSC_Event eventsDone;

static fd_set readfds;
static fd_set writefds;
static int nread;
static int nwrite;
static int nfds;
static int running;

static volatile sig_atomic_t shutdownRequest;
static volatile sig_atomic_t timerExpire;

static int shutdownRef;

static PSC_Timer *tickTimer;
static PSC_Timer *shutdownTimer;

static jmp_buf panicjmp;
static PSC_PanicHandler panicHandlers[MAXPANICHANDLERS];
static int numPanicHandlers;

static void handlesig(int signum);
static void tryReduceNfds(int id);

static void handlesig(int signum)
{
    if (signum == SIGALRM) timerExpire = 1;
    else shutdownRequest = 1;
}

static void tryReduceNfds(int id)
{
    if (!nread && !nwrite)
    {
	nfds = 0;
    }
    else if (id+1 >= nfds)
    {
	int fd;
	for (fd = id; fd >= 0; --fd)
	{
	    if (FD_ISSET(fd, &readfds) || FD_ISSET(fd, &writefds))
	    {
		break;
	    }
	}
	nfds = fd+1;
    }
}

SOEXPORT PSC_Event *PSC_Service_readyRead(void)
{
    return &readyRead;
}

SOEXPORT PSC_Event *PSC_Service_readyWrite(void)
{
    return &readyWrite;
}

SOEXPORT PSC_Event *PSC_Service_prestartup(void)
{
    return &prestartup;
}

SOEXPORT PSC_Event *PSC_Service_startup(void)
{
    return &startup;
}

SOEXPORT PSC_Event *PSC_Service_shutdown(void)
{
    return &shutdown;
}

SOEXPORT PSC_Event *PSC_Service_tick(void)
{
    return &tick;
}

SOEXPORT PSC_Event *PSC_Service_eventsDone(void)
{
    return &eventsDone;
}

SOEXPORT void PSC_Service_registerRead(int id)
{
    if (FD_ISSET(id, &readfds)) return;
    FD_SET(id, &readfds);
    ++nread;
    if (id >= nfds) nfds = id+1;
}

SOEXPORT void PSC_Service_unregisterRead(int id)
{
    if (!FD_ISSET(id, &readfds)) return;
    FD_CLR(id, &readfds);
    --nread;
    tryReduceNfds(id);
}

SOEXPORT void PSC_Service_registerWrite(int id)
{
    if (FD_ISSET(id, &writefds)) return;
    FD_SET(id, &writefds);
    ++nwrite;
    if (id >= nfds) nfds = id+1;
}

SOEXPORT void PSC_Service_unregisterWrite(int id)
{
    if (!FD_ISSET(id, &writefds)) return;
    FD_CLR(id, &writefds);
    --nwrite;
    tryReduceNfds(id);
}

SOEXPORT void PSC_Service_registerPanic(PSC_PanicHandler handler)
{
    if (numPanicHandlers >= MAXPANICHANDLERS) return;
    panicHandlers[numPanicHandlers++] = handler;
}

SOEXPORT void PSC_Service_unregisterPanic(PSC_PanicHandler handler)
{
    for (int i = 0; i < numPanicHandlers; ++i)
    {
	if (panicHandlers[i] == handler)
	{
	    if (--numPanicHandlers > i)
	    {
		memmove(panicHandlers + i, panicHandlers + i + 1,
			(numPanicHandlers - i) * sizeof *panicHandlers);
	    }
	    break;
	}
    }
}

static void raiseTick(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    PSC_Event_raise(&tick, 0, 0);
}

SOEXPORT int PSC_Service_setTickInterval(unsigned msec)
{
    if (!tickTimer)
    {
	tickTimer = PSC_Timer_create();
	PSC_Event_register(PSC_Timer_expired(tickTimer), 0, raiseTick, 0);
    }
    PSC_Timer_setMs(tickTimer, msec);
    if (running) PSC_Timer_start(tickTimer, 1);
    return 0;
}

static int panicreturn(void)
{
    return setjmp(panicjmp) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int serviceLoop(int isRun)
{
    int rc = EXIT_FAILURE;

    PSC_RunOpts *opts = runOpts();

    PSC_EAStartup sea = { EXIT_SUCCESS };
    PSC_Event_raise(&prestartup, 0, &sea);
    if (sea.rc != EXIT_SUCCESS)
    {
	rc = sea.rc;
	goto done;
    }

    if (opts->uid != -1 && geteuid() == 0)
    {
	if (opts->daemonize)
	{
	    if (chown(opts->pidfile, opts->uid, opts->gid) < 0)
	    {
		PSC_Log_msg(PSC_L_WARNING,
			"service: cannot change owner of pidfile");
	    }
	}
	if (opts->gid != -1)
	{
	    gid_t gid = opts->gid;
	    if (setgroups(1, &gid) < 0 || setgid(gid) < 0)
	    {
		PSC_Log_msg(PSC_L_ERROR,
			"service: cannot set specified group");
		return rc;
	    }
	}
	if (setuid(opts->uid) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR,
		    "service: cannot set specified user");
	    return rc;
	}
    }

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = SIG_IGN;
    sigemptyset(&handler.sa_mask);
    sigaction(SIGHUP, &handler, 0);
    sigaction(SIGPIPE, &handler, 0);
    sigaction(SIGUSR1, &handler, 0);

    handler.sa_handler = handlesig;
    sigaddset(&handler.sa_mask, SIGTERM);
    sigaddset(&handler.sa_mask, SIGINT);
    sigaddset(&handler.sa_mask, SIGALRM);
    sigset_t mask;

    if (sigprocmask(SIG_BLOCK, &handler.sa_mask, &mask) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set signal mask");
	return rc;
    }

    if (sigaction(SIGTERM, &handler, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set signal handler for SIGTERM");
	goto done;
    }

    if (sigaction(SIGINT, &handler, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set signal handler for SIGINT");
	goto done;
    }

    if (sigaction(SIGALRM, &handler, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set signal handler for SIGALRM");
	goto done;
    }

    PSC_Event_raise(&startup, 0, &sea);
    rc = sea.rc;
    if (rc != EXIT_SUCCESS) goto done;

    if (isRun && opts->daemonize)
    {
	if (opts->logEnabled)
	{
	    const char *logident = opts->logident;
	    if (!logident) logident = DEFLOGIDENT;
	    PSC_Log_setAsync(1);
	    PSC_Log_setSyslogLogger(logident, LOG_DAEMON, 0);
	}
	if (opts->waitLaunched)
	{
	    PSC_Daemon_launched();
	}
    }

    running = 1;
    shutdownRef = -1;
    if (!tickTimer)
    {
	tickTimer = PSC_Timer_create();
	PSC_Timer_setMs(tickTimer, 0);
    }
    else PSC_Timer_start(tickTimer, 1);
    PSC_Log_msg(PSC_L_INFO, "service started");

    if ((rc = panicreturn()) != EXIT_SUCCESS) goto shutdown;

    int src = 0;
    while (shutdownRef != 0)
    {
	PSC_Event_raise(&eventsDone, 0, 0);
	fd_set rfds;
	fd_set wfds;
	fd_set *r = 0;
	fd_set *w = 0;
	if (nread)
	{
	    memcpy(&rfds, &readfds, sizeof rfds);
	    r = &rfds;
	}
	if (nwrite)
	{
	    memcpy(&wfds, &writefds, sizeof wfds);
	    w = &wfds;
	}
	if (!shutdownRequest) src = pselect(nfds, r, w, 0, 0, &mask);
	if (shutdownRequest)
	{
	    shutdownRequest = 0;
	    shutdownRef = 0;
	    PSC_Event_raise(&shutdown, 0, 0);
	    continue;
	}
	if (timerExpire)
	{
	    timerExpire = 0;
	    PSC_Timer_underrun();
	    continue;
	}
	if (src < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "pselect() failed");
	    rc = EXIT_FAILURE;
	    break;
	}
	if (w) for (int i = 0; src > 0 && i < nfds; ++i)
	{
	    if (FD_ISSET(i, w))
	    {
		--src;
		PSC_Event_raise(&readyWrite, i, 0);
	    }
	}
	if (r) for (int i = 0; src > 0 && i < nfds; ++i)
	{
	    if (FD_ISSET(i, r))
	    {
		--src;
		PSC_Event_raise(&readyRead, i, 0);
	    }
	}
    }
    PSC_Event_raise(&eventsDone, 0, 0);

shutdown:
    PSC_Timer_destroy(shutdownTimer);
    shutdownTimer = 0;
    running = 0;
    PSC_Log_msg(PSC_L_INFO, "service shutting down");

done:
    if (sigprocmask(SIG_SETMASK, &mask, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot restore original signal mask");
	rc = EXIT_FAILURE;
    }

    PSC_Timer_destroy(tickTimer);
    tickTimer = 0;

    return rc;
}

static int serviceMain(void *data)
{
    (void)data;

    int rc = EXIT_FAILURE;

    if (PSC_ThreadPool_init() < 0) goto done;

    if (tickTimer) PSC_Service_setTickInterval(1000);

    rc = serviceLoop(1);

done:
    PSC_ThreadPool_done();

    free(readyRead.handlers);
    free(readyWrite.handlers);
    free(prestartup.handlers);
    free(startup.handlers);
    free(shutdown.handlers);
    free(tick.handlers);
    free(eventsDone.handlers);
    memset(&readyRead, 0, sizeof readyRead);
    memset(&readyWrite, 0, sizeof readyWrite);
    memset(&prestartup, 0, sizeof prestartup);
    memset(&startup, 0, sizeof startup);
    memset(&shutdown, 0, sizeof shutdown);
    memset(&tick, 0, sizeof tick);
    memset(&eventsDone, 0, sizeof eventsDone);

    return rc;
}

SOEXPORT int PSC_Service_loop(void)
{
    return serviceLoop(0);
}

SOEXPORT int PSC_Service_run(void)
{
    PSC_RunOpts *opts = runOpts();
    if (opts->logEnabled)
    {
	if (!opts->daemonize) PSC_Log_setFileLogger(stderr);
	else
	{
	    const char *logident = opts->logident;
	    if (!logident) logident = DEFLOGIDENT;
	    PSC_Log_setSyslogLogger(logident, LOG_DAEMON, 1);
	}
    }
    return PSC_Daemon_run(serviceMain, 0);
}

SOEXPORT void PSC_Service_quit(void)
{
    shutdownRequest = 1;
}

static void shutdownTimeout(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    shutdownRef = 0;
}

SOEXPORT void PSC_Service_shutdownLock(void)
{
    if (shutdownRef >= 0) ++shutdownRef;
    if (!shutdownTimer)
    {
	shutdownTimer = PSC_Timer_create();
	PSC_Event_register(PSC_Timer_expired(shutdownTimer), 0,
		shutdownTimeout, 0);
	PSC_Timer_setMs(shutdownTimer, 5000);
	PSC_Timer_start(shutdownTimer, 0);
    }
}

SOEXPORT void PSC_Service_shutdownUnlock(void)
{
    if (shutdownRef > 0) --shutdownRef;
}

SOEXPORT void PSC_Service_panic(const char *msg)
{
    if (running) for (int i = 0; i < numPanicHandlers; ++i)
    {
	panicHandlers[i](msg);
    }
    PSC_Log_setPanic();
    PSC_Log_msg(PSC_L_FATAL, msg);
    if (running) longjmp(panicjmp, -1);
    else abort();
}

SOEXPORT void PSC_EAStartup_return(PSC_EAStartup *self, int rc)
{
    self->rc = rc;
}

SOLOCAL int PSC_Service_shutsdown(void)
{
    return shutdownRef >= 0;
}

