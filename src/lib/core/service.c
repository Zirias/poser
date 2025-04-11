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
#include <syslog.h>
#include <unistd.h>

#if !defined(HAVE_EPOLL) && !defined(WITH_POLL)
#  define WITH_SELECT
#endif

#ifdef HAVE_EPOLL
#  undef WITH_POLL
#  undef WITH_SELECT
#  define EP_MAX_EVENTS 32
#  include <poser/core/util.h>
#  include <sys/epoll.h>

typedef struct EpollWatch EpollWatch;
struct EpollWatch
{
    EpollWatch *next;
    int fd;
    uint32_t events;
};
static EpollWatch *watches[32];
static int epfd = -1;
#endif

#ifdef WITH_POLL
#  undef WITH_SELECT
#  include <poll.h>
#  include <poser/core/util.h>

static nfds_t nfds;
static size_t fdssz;
static struct pollfd *fds;
#endif

#ifdef WITH_SELECT
#  include <sys/select.h>

static fd_set readfds;
static fd_set writefds;
static int nread;
static int nwrite;
static int nfds;

#endif

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

static int running;

static volatile sig_atomic_t shutdownRequest;
static volatile sig_atomic_t timerExpire;

static int shutdownRef;

static PSC_Timer *tickTimer;
static PSC_Timer *shutdownTimer;

static jmp_buf panicjmp;
static PSC_PanicHandler panicHandlers[MAXPANICHANDLERS];
static int numPanicHandlers;

static void handlesig(int signum)
{
    if (signum == SIGALRM) timerExpire = 1;
    else shutdownRequest = 1;
}

#ifdef WITH_SELECT
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
#endif

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

#ifdef WITH_SELECT
SOEXPORT int PSC_Service_isValidFd(int id, const char *errtopic)
{
    if (id >= 0 && id < FD_SETSIZE) return 1;
    if (errtopic)
    {
	if (id < 0) PSC_Log_fmt(PSC_L_FATAL,
		"%s: negative file descriptor", errtopic);
	else PSC_Log_fmt(PSC_L_ERROR, "%s: ran out of file descriptors "
		"usable with select(), fd=%d is greater or equal to "
		"FD_SETSIZE=%d. Try increasing FD_SETSIZE for building poser.",
		errtopic, id, (int)(FD_SETSIZE));
    }
    return 0;
}
#else
SOEXPORT int PSC_Service_isValidFd(int id, const char *errtopic)
{
    if (id >= 0) return 1;
    if (errtopic) PSC_Log_fmt(PSC_L_FATAL,
	    "%s: negative file descriptor", errtopic);
    return 0;
}
#endif

#ifdef HAVE_EPOLL
static EpollWatch *findWatch(int fd, EpollWatch **parent)
{
    if (fd < 0) return 0;
    EpollWatch *w = watches[(unsigned)fd & 31U];
    while (w)
    {
	if (w->fd == fd) break;
	if (*parent) *parent = w;
	w = w->next;
    }
    return w;
}

static void registerWatch(int id, uint32_t flag)
{
    if (epfd < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: epoll not initialized");
	return;
    }
    if (!PSC_Service_isValidFd(id, "service")) return;
    struct epoll_event ev = {.events = 0, .data = { .fd = id } };
    EpollWatch *w = findWatch(id, 0);
    if (w)
    {
	if (w->events & flag) return;
	w->events |= flag;
	ev.events = w->events;
	epoll_ctl(epfd, EPOLL_CTL_MOD, id, &ev);
    }
    else
    {
	w = PSC_malloc(sizeof *w);
	w->next = 0;
	w->fd = id;
	w->events = flag;
	EpollWatch *p = watches[(unsigned)id & 31U];
	while (p && p->next) p = p->next;
	if (p) p->next = w;
	else watches[(unsigned)id & 31U] = w;
	ev.events = w->events;
	epoll_ctl(epfd, EPOLL_CTL_ADD, id, &ev);
    }
}

static void unregisterWatch(int id, uint32_t flag)
{
    if (epfd < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: epoll not initialized");
	return;
    }
    if (!PSC_Service_isValidFd(id, 0)) return;
    struct epoll_event ev = {.events = 0, .data = { .fd = id } };
    EpollWatch *p = 0;
    EpollWatch *w = findWatch(id, &p);
    if (!w || !(w->events & flag)) return;
    w->events &= (~flag);
    if (w->events)
    {
	ev.events = w->events;
	epoll_ctl(epfd, EPOLL_CTL_MOD, id, &ev);
    }
    else
    {
	if (p) p->next = w->next;
	else watches[(unsigned)id & 31U] = w->next;
	free(w);
	epoll_ctl(epfd, EPOLL_CTL_DEL, id, 0);
    }
}

SOEXPORT void PSC_Service_registerRead(int id)
{
    registerWatch(id, EPOLLIN);
}

SOEXPORT void PSC_Service_unregisterRead(int id)
{
    unregisterWatch(id, EPOLLIN);
}

SOEXPORT void PSC_Service_registerWrite(int id)
{
    registerWatch(id, EPOLLOUT);
}

SOEXPORT void PSC_Service_unregisterWrite(int id)
{
    unregisterWatch(id, EPOLLOUT);
}
#endif

#ifdef WITH_POLL
static struct pollfd *findFd(int fd, size_t *idx)
{
    for (size_t i = 0; i < nfds; ++i)
    {
	if (fds[i].fd == fd)
	{
	    if (idx) *idx = i;
	    return fds + i;
	}
    }
    return 0;
}

static void registerPoll(int id, short flag)
{
    if (!PSC_Service_isValidFd(id, "service")) return;
    struct pollfd *fd = findFd(id, 0);
    if (fd)
    {
	if (fd->events & flag) return;
	fd->events |= flag;
    }
    else
    {
	if (nfds == fdssz)
	{
	    fdssz += 16;
	    fds = PSC_realloc(fds, fdssz * sizeof *fds);
	}
	fd = fds + (nfds++);
	fd->fd = id;
	fd->events = flag;
	fd->revents = 0;
    }
}

static void unregisterPoll(int id, short flag)
{
    if (!PSC_Service_isValidFd(id, 0)) return;
    size_t i = 0;
    struct pollfd *fd = findFd(id, &i);
    if (!fd) return;
    fd->events &= (~flag);
    if (!fd->events)
    {
	--nfds;
	memmove(fds + i, fds + i + 1, (nfds - i) * sizeof *fds);
    }
}

SOEXPORT void PSC_Service_registerRead(int id)
{
    registerPoll(id, POLLIN);
}

SOEXPORT void PSC_Service_unregisterRead(int id)
{
    unregisterPoll(id, POLLIN);
}

SOEXPORT void PSC_Service_registerWrite(int id)
{
    registerPoll(id, POLLOUT);
}

SOEXPORT void PSC_Service_unregisterWrite(int id)
{
    unregisterPoll(id, POLLOUT);
}
#endif

#ifdef WITH_SELECT
SOEXPORT void PSC_Service_registerRead(int id)
{
    if (!PSC_Service_isValidFd(id, "service")) return;
    if (FD_ISSET(id, &readfds)) return;
    FD_SET(id, &readfds);
    ++nread;
    if (id >= nfds) nfds = id+1;
}

SOEXPORT void PSC_Service_unregisterRead(int id)
{
    if (!PSC_Service_isValidFd(id, 0)) return;
    if (!FD_ISSET(id, &readfds)) return;
    FD_CLR(id, &readfds);
    --nread;
    tryReduceNfds(id);
}

SOEXPORT void PSC_Service_registerWrite(int id)
{
    if (!PSC_Service_isValidFd(id, "service")) return;
    if (FD_ISSET(id, &writefds)) return;
    FD_SET(id, &writefds);
    ++nwrite;
    if (id >= nfds) nfds = id+1;
}

SOEXPORT void PSC_Service_unregisterWrite(int id)
{
    if (!PSC_Service_isValidFd(id, 0)) return;
    if (!FD_ISSET(id, &writefds)) return;
    FD_CLR(id, &writefds);
    --nwrite;
    tryReduceNfds(id);
}
#endif

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

static int handleSigFlags(void)
{
    if (shutdownRequest)
    {
	shutdownRequest = 0;
	shutdownRef = 0;
	PSC_Event_raise(&shutdown, 0, 0);
	return 1;
    }
    if (timerExpire)
    {
	timerExpire = 0;
	PSC_Timer_underrun();
	return 1;
    }
    return 0;
}

#ifdef HAVE_EPOLL
static const char *eventBackendInfo(void)
{
    return "epoll";
}

static int processEvents(sigset_t *sigmask)
{
    int prc = 0;
    struct epoll_event ev[EP_MAX_EVENTS];
    if (!shutdownRequest) prc = epoll_pwait2(epfd,
	    ev, EP_MAX_EVENTS, 0, sigmask);
    if (handleSigFlags()) return 0;
    if (prc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "epoll_pwait2() failed");
	return -1;
    }
    for (int i = 0; i < prc; ++i)
    {
	if (ev[i].events & EPOLLOUT)
	{
	    PSC_Event_raise(&readyWrite, ev[i].data.fd, 0);
	}
	if (ev[i].events & EPOLLIN)
	{
	    PSC_Event_raise(&readyRead, ev[i].data.fd, 0);
	}
    }
    return 0;
}
#endif

#ifdef WITH_POLL
static const char *eventBackendInfo(void)
{
    return "poll";
}

static int processEvents(sigset_t *sigmask)
{
    int prc = 0;
    if (!shutdownRequest) prc = ppoll(fds, nfds, 0, sigmask);
    if (handleSigFlags()) return 0;
    if (prc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "ppoll() failed");
	return -1;
    }
    for (size_t i = 0; prc > 0 && i < nfds; ++i)
    {
	if (!fds[i].revents) continue;
	--prc;
	if (fds[i].revents & POLLOUT)
	{
	    PSC_Event_raise(&readyWrite, fds[i].fd, 0);
	}
	if (fds[i].revents & POLLIN)
	{
	    PSC_Event_raise(&readyRead, fds[i].fd, 0);
	}
	fds[i].revents = 0;
    }
    return 0;
}
#endif

#ifdef WITH_SELECT
static const char *eventBackendInfo(void)
{
    static const char infoFmt[] = "select, fd limit: %u";
    static char info[sizeof infoFmt + 12] = { 0 };
    if (!*info) snprintf(info, sizeof info, infoFmt, (unsigned)(FD_SETSIZE));
    return info;
}

static int processEvents(sigset_t *sigmask)
{
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
    int src = 0;
    if (!shutdownRequest) src = pselect(nfds, r, w, 0, 0, sigmask);
    if (handleSigFlags()) return 0;
    if (src < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "pselect() failed");
	return -1;
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
    return 0;
}
#endif

static int serviceLoop(int isRun)
{
    int rc = EXIT_FAILURE;

    PSC_RunOpts *opts = runOpts();

#ifdef HAVE_EPOLL
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: cannot open epoll");
	goto done;
    }
#endif

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
    PSC_Log_fmt(PSC_L_DEBUG, "service started with event backend: %s",
	    eventBackendInfo());

    if ((rc = panicreturn()) != EXIT_SUCCESS) goto shutdown;

    while (shutdownRef != 0)
    {
	PSC_Event_raise(&eventsDone, 0, 0);
	if (processEvents(&mask) < 0)
	{
	    rc = EXIT_FAILURE;
	    break;
	}
    }
    PSC_Event_raise(&eventsDone, 0, 0);

shutdown:
    PSC_Timer_destroy(shutdownTimer);
    shutdownTimer = 0;
    running = 0;
    PSC_Log_msg(PSC_L_DEBUG, "service shutting down");

done:
    if (sigprocmask(SIG_SETMASK, &mask, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot restore original signal mask");
	rc = EXIT_FAILURE;
    }

    PSC_Timer_destroy(tickTimer);
    tickTimer = 0;

#ifdef WITH_POLL
    free(fds);
    fds = 0;
    fdssz = 0;
    nfds = 0;
#endif

#ifdef HAVE_EPOLL
    if (epfd >= 0)
    {
	close(epfd);
	epfd = -1;
    }
    for (int i = 0; i < 32; ++i)
    {
	EpollWatch *n = 0;
	EpollWatch *w = watches[i];
	while (w)
	{
	    n = w->next;
	    free(w);
	    w = n;
	}
	watches[i] = 0;
    }
#endif

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

