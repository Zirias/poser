#define _DEFAULT_SOURCE

#include "event.h"
#include "log.h"
#include "runopts.h"
#include "service.h"
#include "timer.h"

#include <poser/core/daemon.h>
#include <poser/core/threadpool.h>

#include <errno.h>
#include <grp.h>
#include <setjmp.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#ifndef NSIG
#  define NSIG 64
#endif

#if !defined(HAVE_KQUEUE) && !defined(HAVE_EPOLL) && !defined(WITH_POLL)
#  define WITH_SELECT
#endif

#ifdef HAVE_KQUEUE
#  undef HAVE_SIGNALFD
#endif

#if !defined(HAVE_KQUEUE) && !defined(HAVE_SIGNALFD)
#  define WITH_SIGHDL
#endif

#ifdef HAVE_SIGNALFD
#  include <sys/signalfd.h>

static int sfd = -1;
#endif

#ifdef HAVE_KQUEUE
#  undef HAVE_EPOLL
#  undef WITH_POLL
#  undef WITH_SELECT
#  define KQ_MAX_EVENTS 32
#  define KQ_MAX_CHANGES 128
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/types.h>
#  include <sys/event.h>
#  include <sys/time.h>

static int kqfd = -1;
static int nchanges;
static struct kevent changes[KQ_MAX_CHANGES];
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

struct PSC_EAChildExited
{
    pid_t pid;
    int val;
    int signaled;
};

static PSC_Event readyRead;
static PSC_Event readyWrite;
static PSC_Event prestartup;
static PSC_Event startup;
static PSC_Event shutdown;
static PSC_Event tick;
static PSC_Event eventsDone;
static PSC_Event childExited;

static int running;

static int shutdownRef;

static PSC_Timer *tickTimer;
static PSC_Timer *shutdownTimer;

static jmp_buf panicjmp;
static PSC_PanicHandler panicHandlers[MAXPANICHANDLERS];
static int numPanicHandlers;

static sigset_t sigorigmask;
static sigset_t sigblockmask;
#if defined(WITH_SIGHDL) || defined(HAVE_SIGNALFD)
static PSC_SignalHandler sigcallback[NSIG];
#endif
#ifdef WITH_SIGHDL
static volatile sig_atomic_t sigflags[NSIG];
static void handlesig(int signum)
{
    if (signum < 0 || signum >= NSIG) return;
    sigflags[signum] = 1;
}
#endif

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

SOEXPORT PSC_Event *PSC_Service_childExited(void)
{
    return &childExited;
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

#ifdef HAVE_KQUEUE
static struct kevent *addChange(void)
{
    if (nchanges == KQ_MAX_CHANGES)
    {
	kevent(kqfd, changes, nchanges, 0, 0, 0);
	nchanges = 0;
    }
    return changes + nchanges++;
}

static int initKqueue(void)
{
    if (kqfd >= 0) return 0;
#  if defined(HAVE_KQUEUEX)
    kqfd = kqueuex(KQUEUE_CLOEXEC);
#  elif defined(HAVE_KQUEUE1)
    kqfd = kqueue1(O_CLOEXEC);
#  else
    kqfd = kqueue();
    if (kqfd >= 0) fcntl(kqfd, F_SETFD, FD_CLOEXEC);
#  endif
    if (kqfd < 0) return -1;
    struct sigaction sa = { .sa_handler = SIG_IGN };
    if (sigaction(SIGTERM, &sa, 0) < 0) goto fail;
    if (sigaction(SIGINT, &sa, 0) < 0) goto fail;
    if (sigaction(SIGALRM, &sa, 0) < 0) goto fail;

    EV_SET(addChange(), SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
    EV_SET(addChange(), SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
    EV_SET(addChange(), SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
    return 0;

fail:
    close(kqfd);
    kqfd = -1;
    return -1;
}

static int verifyKqueue(int fd, int log)
{
    if (kqfd < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: kqueue not initialized");
	return -1;
    }
    if (!PSC_Service_isValidFd(fd, log ? "service" : 0)) return -1;
    return 0;
}

SOEXPORT void PSC_Service_registerRead(int id)
{
    if (verifyKqueue(id, 1) < 0) return;
    EV_SET(addChange(), id, EVFILT_READ, EV_ADD, 0, 0, 0);
}

SOEXPORT void PSC_Service_unregisterRead(int id)
{
    if (verifyKqueue(id, 0) < 0) return;
    EV_SET(addChange(), id, EVFILT_READ, EV_DELETE, 0, 0, 0);
}

SOEXPORT void PSC_Service_registerWrite(int id)
{
    if (verifyKqueue(id, 1) < 0) return;
    EV_SET(addChange(), id, EVFILT_WRITE, EV_ADD, 0, 0, 0);
}

SOEXPORT void PSC_Service_unregisterWrite(int id)
{
    if (verifyKqueue(id, 0) < 0) return;
    EV_SET(addChange(), id, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
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

#ifdef HAVE_KQUEUE
SOEXPORT void PSC_Service_registerSignal(int signo, PSC_SignalHandler handler)
{
    if (initKqueue() < 0) return;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    if (handler)
    {
	if (signo != SIGTERM && signo != SIGINT && signo != SIGCHLD)
	{
	    sa.sa_handler = SIG_IGN;
	    if (sigaction(signo, &sa, 0) < 0) return;
	}
	EV_SET(addChange(), signo, EVFILT_SIGNAL, EV_ADD, 0, 0,
		(void *)handler);
    }
    else if (signo == SIGTERM || signo == SIGINT || signo == SIGCHLD)
    {
	EV_SET(addChange(), signo, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
    }
    else
    {
	sa.sa_handler = SIG_DFL;
	sigaction(signo, &sa, 0);
	EV_SET(addChange(), signo, EVFILT_SIGNAL, EV_DELETE, 0, 0, 0);
    }
    kevent(kqfd, changes, nchanges, 0, 0, 0);
    nchanges = 0;
}

SOLOCAL void PSC_Service_armTimer(void *timer, unsigned ms, int periodic)
{
    if (initKqueue() < 0) return;
    EV_SET(addChange(), (uintptr_t)timer, EVFILT_TIMER,
	    EV_ADD|(!periodic * EV_ONESHOT), NOTE_MSECONDS, ms, 0);
    kevent(kqfd, changes, nchanges, 0, 0, 0);
    nchanges = 0;
}

SOLOCAL void PSC_Service_unarmTimer(void *timer, unsigned ms, int periodic)
{
    if (initKqueue() < 0) return;
    EV_SET(addChange(), (uintptr_t)timer, EVFILT_TIMER,
	    EV_DELETE|(!periodic * EV_ONESHOT), NOTE_MSECONDS, ms, 0);
    kevent(kqfd, changes, nchanges, 0, 0, 0);
    nchanges = 0;
}
#endif

#ifdef HAVE_SIGNALFD
static int initSigfd(void)
{
#if defined(SFD_NONBLOCK) && defined(SFD_CLOEXEC)
    sfd = signalfd(sfd, &sigblockmask, SFD_NONBLOCK|SFD_CLOEXEC);
#else
    if (sfd >= 0)
    {
	fcntl(sfd, F_SETFD, FD_CLOEXEC);
	fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL) | O_NONBLOCK);
    }
#endif
    return sfd;
}
#endif

#if defined(WITH_SIGHDL) || defined(HAVE_SIGNALFD)
SOEXPORT void PSC_Service_registerSignal(int signo, PSC_SignalHandler handler)
{
    if (signo < 0 || signo >= NSIG) return;

    if (running && signo != SIGTERM && signo != SIGINT
	    && signo != SIGALRM && signo != SIGCHLD)
    {
#ifdef WITH_SIGHDL
	struct sigaction sa;
#endif
	if (sigcallback[signo] && !handler)
	{
#ifdef WITH_SIGHDL
	    memset(&sa, 0, sizeof sa);
	    sa.sa_handler = SIG_DFL;
	    sigemptyset(&sa.sa_mask);
	    if (sigaction(signo, &sa, 0) == 0)
#endif
	    {
		sigdelset(&sigblockmask, signo);
		sigprocmask(SIG_SETMASK, &sigblockmask, 0);
#ifdef HAVE_SIGNALFD
		initSigfd();
#endif
	    }
	}
	if (!sigcallback[signo] && handler)
	{
	    if (sigaddset(&sigblockmask, signo) == 0 &&
		    sigprocmask(SIG_SETMASK, &sigblockmask, 0) == 0)
	    {
#ifdef HAVE_SIGNALFD
		initSigfd();
#else
		memset(&sa, 0, sizeof sa);
		sa.sa_handler = handlesig;
		sigemptyset(&sa.sa_mask);
		sigaction(signo, &sa, 0);
#endif
	    }
	}
    }

    sigcallback[signo] = handler;
}
#endif

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
    if (running && msec) PSC_Timer_start(tickTimer, 1);
    else PSC_Timer_stop(tickTimer);
    return 0;
}

static int panicreturn(void)
{
    return setjmp(panicjmp) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static void reapChildren(void)
{
    PSC_EAChildExited ea;
    int status;
    pid_t pid;
    while ((pid = waitpid((pid_t)-1, &status, WNOHANG)) > 0)
    {
	ea.pid = pid;
	if (WIFEXITED(status))
	{
	    ea.val = WEXITSTATUS(status);
	    ea.signaled = 0;
	}
	else if (WIFSIGNALED(status))
	{
	    ea.val = WTERMSIG(status);
	    ea.signaled = 1;
	}
	else continue;
	PSC_Event_raise(&childExited, (int)pid, &ea);
    }
}

#ifdef WITH_SIGHDL
static int handleSigFlags(void)
{
    if (sigflags[SIGINT] || sigflags[SIGTERM])
    {
	shutdownRef = 0;
	PSC_Event_raise(&shutdown, 0, 0);
    }
#ifndef HAVE_TIMERFD
    if (sigflags[SIGALRM])
    {
	PSC_Timer_underrun();
    }
#endif
    if (sigflags[SIGCHLD])
    {
	reapChildren();
    }
    int handled = 0;
    for (int s = 0; s < NSIG; ++s) if (sigflags[s])
    {
	sigflags[s] = 0;
	handled = 1;
	if (sigcallback[s]) sigcallback[s](s);
    }
    return handled;
}
#endif

#ifdef HAVE_SIGNALFD
static int handleSigfd(void)
{
    if (sfd < 0) return -1;

    struct signalfd_siginfo fdsi;
    ssize_t rrc;
    while ((rrc = read(sfd, &fdsi, sizeof fdsi)) == sizeof fdsi)
    {
	switch (fdsi.ssi_signo)
	{
	    case SIGINT:
	    case SIGTERM:
		shutdownRef = 0;
		PSC_Event_raise(&shutdown, 0, 0);
		break;

#ifndef HAVE_TIMERFD
	    case SIGALRM:
		PSC_Timer_underrun();
		break;
#endif

	    case SIGCHLD:
		reapChildren();
		break;

	    default:
		break;
	}
	if (sigcallback[fdsi.ssi_signo])
	{
	    sigcallback[fdsi.ssi_signo](fdsi.ssi_signo);
	}
    }
    if (rrc < 0 && errno != EWOULDBLOCK) return -1;
    return 0;
}
#endif

#ifdef HAVE_KQUEUE
static const char *eventBackendInfo(void)
{
    return "kqueue";
}

static int processEvents(void)
{
    struct kevent ev[KQ_MAX_EVENTS];
    int qrc = kevent(kqfd, changes, nchanges, ev, KQ_MAX_EVENTS, 0);
    if (qrc < 0)
    {
	if (errno == EINTR) return 0;
	PSC_Log_msg(PSC_L_ERROR, "kevent() failed");
	return -1;
    }
    nchanges = 0;
    PSC_Timer *timer;
    for (int i = 0; i <	qrc; ++i)
    {
	if (ev[i].flags & EV_ERROR) continue;
	switch (ev[i].filter)
	{
	    case EVFILT_SIGNAL:
		switch (ev[i].ident)
		{
		    case SIGINT:
		    case SIGTERM:
			shutdownRef = 0;
			PSC_Event_raise(&shutdown, 0, 0);
			break;

		    case SIGCHLD:
			reapChildren();
			break;

		    default:
			break;
		}
		PSC_SignalHandler handler = (PSC_SignalHandler)ev[i].udata;
		if (handler) handler(ev[i].ident);
		break;

	    case EVFILT_TIMER:
		timer = (PSC_Timer *)ev[i].ident;
		PSC_Timer_doexpire(timer);
		break;

	    case EVFILT_WRITE:
		PSC_Event_raise(&readyWrite, ev[i].ident, 0);
		break;

	    case EVFILT_READ:
		PSC_Event_raise(&readyRead, ev[i].ident, 0);
		break;

	    default:
		break;
	}
    }
    return 0;
}
#endif

#ifdef HAVE_EPOLL
static const char *eventBackendInfo(void)
{
    return "epoll";
}

static int processEvents(void)
{
    struct epoll_event ev[EP_MAX_EVENTS];
#ifdef WITH_SIGHDL
    int prc = epoll_pwait2(epfd, ev, EP_MAX_EVENTS, 0, &sigorigmask);
    if (!handleSigFlags() && prc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "epoll_pwait2() failed");
	return -1;
    }
#else
    int prc = epoll_wait(epfd, ev, EP_MAX_EVENTS, -1);
    if (prc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "epoll_pwait2() failed");
	return -1;
    }
#endif
    for (int i = 0; i < prc; ++i)
    {
#ifdef HAVE_SIGNALFD
	if (ev[i].events & EPOLLIN && ev[i].data.fd == sfd)
	{
	    if (handleSigfd() >= 0) continue;
	    PSC_Log_msg(PSC_L_ERROR, "reading signalfd failed");
	    return -1;
	}
#endif
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

static int processEvents(void)
{
#ifdef WITH_SIGHDL
    sigset_t origmask;
    pthread_sigmask(SIG_SETMASK, &sigorigmask, &origmask);
    errno = 0;
    int prc = poll(fds, nfds, -1);
    int pollerr = errno;
    pthread_sigmask(SIG_SETMASK, &origmask, 0);
    if (!handleSigFlags() && prc < 0)
    {
	if (pollerr == EINTR) return 0;
	PSC_Log_msg(PSC_L_ERROR, "poll() failed");
	return -1;
    }
#else
    int prc = poll(fds, nfds, -1);
    if (prc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "poll() failed");
	return -1;
    }
#endif
    for (size_t i = 0; prc > 0 && i < nfds; ++i)
    {
	if (!fds[i].revents) continue;
	--prc;
#ifdef HAVE_SIGNALFD
	if (fds[i].revents & POLLIN && fds[i].fd == sfd)
	{
	    if (handleSigfd() >= 0) continue;
	    PSC_Log_msg(PSC_L_ERROR, "reading signalfd failed");
	    return -1;
	}
#endif
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

static int processEvents(void)
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
#ifdef WITH_SIGHTL
    int src = pselect(nfds, r, w, 0, 0, &sigorigmask);
    if (!handleSigFlags() && src < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "pselect() failed");
	return -1;
    }
#else
    int src = select(nfds, r, w, 0, 0);
    if (src < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "pselect() failed");
	return -1;
    }
#endif
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
#ifdef HAVE_SIGNALFD
	    if (i == sfd)
	    {
		if (handleSigfd() >= 0) continue;
		PSC_Log_msg(PSC_L_ERROR, "reading signalfd failed");
		return -1;
	    }
#endif
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

    sigemptyset(&sigblockmask);
#if defined(WITH_SIGHDL) || defined(HAVE_SIGNALFD)
    sigaddset(&sigblockmask, SIGTERM);
    sigaddset(&sigblockmask, SIGINT);
#endif
    sigaddset(&sigblockmask, SIGALRM);
    sigaddset(&sigblockmask, SIGCHLD);
    if (sigprocmask(SIG_SETMASK, &sigblockmask, &sigorigmask) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set signal mask");
	return rc;
    }

#if defined(WITH_SIGHDL) || defined(HAVE_KQUEUE)
    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
#endif
#ifdef WITH_SIGHDL
    handler.sa_handler = handlesig;
    sigemptyset(&handler.sa_mask);

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

#ifndef HAVE_TIMERFD
    if (sigaction(SIGALRM, &handler, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set signal handler for SIGALRM");
	goto done;
    }
#endif

    if (sigaction(SIGCHLD, &handler, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set signal handler for SIGCHLD");
	goto done;
    }
#endif

#ifdef HAVE_SIGNALFD
    if (initSigfd() < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: cannot open signalfd");
	goto done;
    }
#endif

#ifdef HAVE_KQUEUE
    if (initKqueue() < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: cannot open kqueue");
	goto done;
    }
    if (nchanges)
    {
	kevent(kqfd, changes, nchanges, 0, 0, 0);
	nchanges = 0;
    }
#endif

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

    PSC_Event_raise(&startup, 0, &sea);
    rc = sea.rc;
    if (rc != EXIT_SUCCESS) goto done;

#if defined(WITH_SIGHDL) || defined(HAVE_SIGNALFD)
    for (int s = 0; s < NSIG; ++s)
    {
	if (sigcallback[s]) sigaddset(&sigblockmask, s);
    }
    if (sigprocmask(SIG_SETMASK, &sigblockmask, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot set signal mask");
	return rc;
    }
#endif

#ifdef HAVE_SIGNALFD
    initSigfd();
    PSC_Service_registerRead(sfd);
#endif

#ifdef WITH_SIGHDL
    for (int s = 0; s < NSIG; ++s)
    {
	switch (s)
	{
	    case SIGTERM:
	    case SIGINT:
#ifndef HAVE_TIMERFD
	    case SIGALRM:
#endif
	    case SIGCHLD:
		break;

	    default:
		if (sigcallback[s] && sigaction(s, &handler, 0) < 0)
		{
		    PSC_Log_fmt(PSC_L_ERROR, "cannot set signal handler "
			    "for signal %d", s);
		}
	}
    }
#endif

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
	PSC_Service_setTickInterval(0);
    }
    else PSC_Timer_start(tickTimer, 1);
    PSC_Log_fmt(PSC_L_DEBUG, "service started with event backend: "
	    "%s (signals: "
#ifdef HAVE_KQUEUE
	    "kqueue"
#elif defined(HAVE_SIGNALFD)
	    "signalfd"
#else
	    "async handlers"
#endif
	    ", timers: "
#ifdef HAVE_KQUEUE
	    "kqueue"
#elif defined(HAVE_TIMERFD)
	    "timerfd"
#else
	    "setitimer"
#endif
	    ")", eventBackendInfo());

    if ((rc = panicreturn()) != EXIT_SUCCESS) goto shutdown;

    while (shutdownRef != 0)
    {
	if (processEvents() < 0)
	{
	    rc = EXIT_FAILURE;
	    break;
	}
	PSC_Event_raise(&eventsDone, 0, 0);
    }

shutdown:
    PSC_Timer_destroy(shutdownTimer);
    shutdownTimer = 0;
    running = 0;
    PSC_Log_msg(PSC_L_DEBUG, "service shutting down");

done:
#if defined(WITH_SIGHDL) || defined(HAVE_KQUEUE)
    handler.sa_handler = SIG_DFL;
    for (int s = 0; s < NSIG; ++s)
    {
	sigaction(s, &handler, 0);
    }
#endif
    if (sigprocmask(SIG_SETMASK, &sigorigmask, 0) < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "cannot restore original signal mask");
	rc = EXIT_FAILURE;
    }

    PSC_Timer_destroy(tickTimer);
    tickTimer = 0;

#ifdef HAVE_SIGNALFD
    if (sfd >= 0)
    {
	close(sfd);
	sfd = -1;
    }
#endif

#ifdef WITH_POLL
    free(fds);
    fds = 0;
    fdssz = 0;
    nfds = 0;
#endif

#ifdef HAVE_KQUEUE
    if (kqfd >= 0)
    {
	close(kqfd);
	kqfd = -1;
    }
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
    free(childExited.handlers);
    memset(&readyRead, 0, sizeof readyRead);
    memset(&readyWrite, 0, sizeof readyWrite);
    memset(&prestartup, 0, sizeof prestartup);
    memset(&startup, 0, sizeof startup);
    memset(&shutdown, 0, sizeof shutdown);
    memset(&tick, 0, sizeof tick);
    memset(&eventsDone, 0, sizeof eventsDone);
    memset(&childExited, 0, sizeof childExited);

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
    shutdownRef = 0;
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

SOEXPORT pid_t PSC_EAChildExited_pid(const PSC_EAChildExited *self)
{
    return self->pid;
}

SOEXPORT int PSC_EAChildExited_status(const PSC_EAChildExited *self)
{
    if (self->signaled) return PSC_CHILD_SIGNALED;
    return self->val;
}

SOEXPORT int PSC_EAChildExited_signal(const PSC_EAChildExited *self)
{
    if (!self->signaled) return 0;
    return self->val;
}

SOLOCAL int PSC_Service_shutsdown(void)
{
    return shutdownRef >= 0;
}

