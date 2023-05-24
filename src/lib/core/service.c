#define _DEFAULT_SOURCE

#include "event.h"
#include "runopts.h"

#include <poser/core/daemon.h>
#include <poser/core/log.h>
#include <poser/core/service.h>
#include <poser/core/threadpool.h>

#include <grp.h>
#include <setjmp.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

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
static volatile sig_atomic_t timerTick;

static int shutdownRef;
static int shutdownTicks;

static struct itimerval timer;
static struct itimerval otimer;

static jmp_buf panicjmp;
static PSC_PanicHandler panicHandlers[MAXPANICHANDLERS];
static int numPanicHandlers;

static void handlesig(int signum);
static void tryReduceNfds(int id);

static void handlesig(int signum)
{
    if (signum == SIGALRM) timerTick = 1;
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

SOEXPORT int PSC_Service_setTickInterval(unsigned msec)
{
    timer.it_interval.tv_sec = msec / 1000U;
    timer.it_interval.tv_usec = 1000U * (msec % 1000U);
    timer.it_value.tv_sec = msec / 1000U;
    timer.it_value.tv_usec = 1000U * (msec % 1000U);
    if (running) return setitimer(ITIMER_REAL, &timer, 0);
    return 0;
}

static int panicreturn(void)
{
    return setjmp(panicjmp) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

SOEXPORT int PSC_Service_loop(void)
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
	    chown(opts->pidfile, opts->uid, opts->gid);
	}
	if (opts->gid != -1)
	{
	    gid_t gid = opts->gid;
	    if (setgroups(1, &gid) < 0 || setgid(gid) < 0)
	    {
		PSC_Log_msg(PSC_L_ERROR, "cannot set specified group");
		return rc;
	    }
	}
	if (setuid(opts->uid) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "cannot set specified user");
	    return rc;
	}
    }

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = handlesig;
    sigemptyset(&handler.sa_mask);
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

    if (setitimer(ITIMER_REAL, &timer, &otimer) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set periodic timer");
	goto done;
    }

    PSC_Event_raise(&startup, 0, &sea);
    rc = sea.rc;
    if (rc != EXIT_SUCCESS) goto done;

    running = 1;
    shutdownRef = -1;
    shutdownTicks = -1;
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
	    shutdownTicks = 5;
	    PSC_Event_raise(&shutdown, 0, 0);
	    continue;
	}
	if (timerTick)
	{
	    timerTick = 0;
	    if (shutdownTicks > 0 && !--shutdownTicks)
	    {
		shutdownRef = 0;
		break;
	    }
	    PSC_Event_raise(&tick, 0, 0);
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

shutdown:
    running = 0;
    PSC_Log_msg(PSC_L_INFO, "service shutting down");

done:
    if (sigprocmask(SIG_SETMASK, &mask, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot restore original signal mask");
	rc = EXIT_FAILURE;
    }

    if (setitimer(ITIMER_REAL, &otimer, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot restore original periodic timer");
	rc = EXIT_FAILURE;
    }

    return rc;
}

static int serviceMain(void *data)
{
    (void)data;

    int rc = EXIT_FAILURE;

    if (PSC_ThreadPool_init() < 0) goto done;

    if (!timer.it_interval.tv_sec &&
	    !timer.it_interval.tv_usec &&
	    !timer.it_value.tv_sec &&
	    !timer.it_value.tv_usec) PSC_Service_setTickInterval(1000);

    rc = PSC_Service_loop();

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

SOEXPORT int PSC_Service_run(void)
{
    return PSC_Daemon_run(serviceMain, 0);
}

SOEXPORT void PSC_Service_quit(void)
{
    shutdownRequest = 1;
}

SOEXPORT void PSC_Service_shutdownLock(void)
{
    if (shutdownRef == 0) PSC_Service_setTickInterval(1000);
    if (shutdownRef >= 0) ++shutdownRef;
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
    PSC_Log_setAsync(0);
    PSC_Log_msg(PSC_L_FATAL, msg);
    if (running) longjmp(panicjmp, -1);
    else abort();
}

SOEXPORT void PSC_EAStartup_return(PSC_EAStartup *self, int rc)
{
    self->rc = rc;
}

