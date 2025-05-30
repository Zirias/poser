#define _DEFAULT_SOURCE

#include "event.h"
#include "log.h"
#include "runopts.h"
#include "service.h"
#include "timer.h"

#include <poser/core/daemon.h>
#include <poser/core/threadpool.h>
#include <poser/core/util.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#undef SVC_NO_ATOMICS
#ifdef __STDC_NO_ATOMICS__
#  define SVC_NO_ATOMICS
#else
#  include <stdatomic.h>
#  if ATOMIC_INT_LOCK_FREE != 2
#    define SVC_NO_ATOMICS
#  endif
#endif

#ifndef NSIG
#  define NSIG 64
#endif

#if !defined(HAVE_KQUEUE) && !defined(HAVE_EVPORTS) \
	&& !defined(HAVE_EPOLL) && !defined(WITH_POLL)
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
#  define SIGFD_RDFD sfd
#  define SIGFD_TYPE struct signalfd_siginfo
#  define SIGFD_VALUE(x) (x).ssi_signo

int sfd = -1;
#endif

#ifdef HAVE_EVPORTS
#  undef HAVE_KQUEUE
#  undef HAVE_EPOLL
#  undef WITH_POLL
#  undef WITH_SELECT
#  define EVP_MAX_EVENTS 32
#  include <errno.h>
#  include <poll.h>
#  include <port.h>
#  include <poser/core/util.h>

typedef struct EvportWatch EvportWatch;
struct EvportWatch
{
    EvportWatch *next;
    int fd;
    int events;
};
#endif

#ifdef HAVE_KQUEUE
#  undef HAVE_EPOLL
#  undef WITH_POLL
#  undef WITH_SELECT
#  define KQ_MAX_EVENTS 32
#  define KQ_MAX_CHANGES 128
#  include <errno.h>
#  include <sys/types.h>
#  include <sys/event.h>
#  include <sys/time.h>
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
#endif

#ifdef WITH_POLL
#  undef WITH_SELECT
#  include <poll.h>
#  include <poser/core/util.h>
#endif

#ifdef WITH_SELECT
#  include <sys/select.h>
#endif

#ifndef DEFLOGIDENT
#define DEFLOGIDENT "posercore"
#endif

typedef enum ServiceLoopFlags
{
    SLF_NONE	    = 0,
    SLF_SVCRUN	    = (1 << 0),
    SLF_SVCMAIN	    = (1 << 1)
} ServiceLoopFlags;

typedef struct SvcCommand
{
    PSC_OnThreadExec func;
    void *arg;
} SvcCommand;

typedef struct SecondaryService
{
    pthread_t handle;
    sem_t cmdsem;
    int commandpipe[2];
    int threadno;
} SecondaryService;

typedef struct Service
{
    SecondaryService *svcid;
    PSC_Timer *shutdownTimer;
#ifdef HAVE_EVPORTS
    EvportWatch *watches[32];
#endif
#ifdef HAVE_EPOLL
    EpollWatch *watches[32];
#endif
#ifdef WITH_POLL
    nfds_t nfds;
    size_t fdssz;
    struct pollfd *fds;
#endif
    PSC_Event readyRead;
    PSC_Event readyWrite;
    PSC_Event eventsDone;
#ifdef WITH_SELECT
    fd_set readfds;
    fd_set writefds;
#endif
#ifdef HAVE_KQUEUE
    struct kevent changes[KQ_MAX_CHANGES];
    int kqfd;
    int nchanges;
#endif
    int running;
#if defined(HAVE_EVPORTS) || defined(HAVE_EPOLL)
    int epfd;
#endif
#ifdef WITH_SELECT
    int nread;
    int nwrite;
    int nfds;
#endif
#ifdef SVC_NO_ATOMICS
    int shutdownRef;
#else
    atomic_int shutdownRef;
#endif
} Service;

static THREADLOCAL Service *svc;
static Service *mainsvc;
static SecondaryService *ssvc;
static int nssvc;
static int commandpipe[2];
static sem_t cmdsem;
#ifdef SVC_NO_ATOMICS
sem_t shutdownrq;
#endif

static PSC_Event prestartup;
static PSC_Event startup;
static PSC_Event shutdown;
static PSC_Event childExited;

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

static jmp_buf panicjmp;
static PSC_PanicHandler panicHandlers[MAXPANICHANDLERS];
static int numPanicHandlers;

static sigset_t sigorigmask;
static sigset_t sigblockmask;
#if defined(WITH_SIGHDL) || defined(HAVE_SIGNALFD)
typedef struct SigHdlRec SigHdlRec;
struct SigHdlRec
{
    SigHdlRec *next;
    PSC_SignalHandler hdl;
    int signo;
};
static SigHdlRec *sigcallbacks[32];
static SigHdlRec *sighdlrec(int signo)
{
    unsigned h = ((unsigned)signo) & 0x1fU;
    for (SigHdlRec *r = sigcallbacks[h]; r; r = r->next)
    {
	if (r->signo == signo) return r;
    }
    return 0;
}
#endif
#ifdef WITH_SIGHDL
#  define SIGFD_RDFD sfd[0]
#  define SIGFD_TYPE int
#  define SIGFD_VALUE(x) (x)

int sfd[2];
static void handlesig(int signum)
{
    if (write(sfd[1], &signum, sizeof signum) <= 0) abort();
}
#endif

static void svcinit(void)
{
    if (svc) return;
    svc = PSC_malloc(sizeof *svc);
    memset(svc, 0, sizeof *svc);
#ifdef HAVE_KQUEUE
    svc->kqfd = -1;
#endif
#if defined(HAVE_EVPORTS) || defined(HAVE_EPOLL)
    svc->epfd = -1;
#endif
}

#ifdef WITH_SELECT
static void tryReduceNfds(int id)
{
    if (!svc->nread && !svc->nwrite)
    {
	svc->nfds = 0;
    }
    else if (id+1 >= svc->nfds)
    {
	int fd;
	for (fd = id; fd >= 0; --fd)
	{
	    if (FD_ISSET(fd, &svc->readfds) || FD_ISSET(fd, &svc->writefds))
	    {
		break;
	    }
	}
	svc->nfds = fd+1;
    }
}
#endif

SOEXPORT PSC_Event *PSC_Service_readyRead(void)
{
    svcinit();
    return &svc->readyRead;
}

SOEXPORT PSC_Event *PSC_Service_readyWrite(void)
{
    svcinit();
    return &svc->readyWrite;
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

SOEXPORT PSC_Event *PSC_Service_eventsDone(void)
{
    svcinit();
    return &svc->eventsDone;
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

#ifdef HAVE_EVPORTS
static EvportWatch *findWatch(int fd, EvportWatch **parent)
{
    if (fd < 0) return 0;
    EvportWatch *w = svc->watches[(unsigned)fd & 31U];
    while (w)
    {
	if (w->fd == fd) break;
	if (parent) *parent = w;
	w = w->next;
    }
    return w;
}

static void reregister(int id)
{
    EvportWatch *w = findWatch(id, 0);
    if (!w) return;
    port_associate(svc->epfd, PORT_SOURCE_FD, id, w->events, 0);
}

static void registerWatch(int id, int flag)
{
    svcinit();
    if (svc->epfd < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: event port not initialized");
	return;
    }
    if (!PSC_Service_isValidFd(id, "service")) return;
    EvportWatch *w = findWatch(id, 0);
    if (w)
    {
	if (w->events & flag) return;
	w->events |= flag;
    }
    else
    {
	w = PSC_malloc(sizeof *w);
	w->next = 0;
	w->fd = id;
	w->events = flag;
	EvportWatch *p = svc->watches[(unsigned)id & 31U];
	while (p && p->next) p = p->next;
	if (p) p->next = w;
	else svc->watches[(unsigned)id & 31U] = w;
    }
    port_associate(svc->epfd, PORT_SOURCE_FD, id, w->events, 0);
}

static void unregisterWatch(int id, int flag)
{
    if (svc->epfd < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: event port not initialized");
	return;
    }
    if (!PSC_Service_isValidFd(id, 0)) return;
    EvportWatch *p = 0;
    EvportWatch *w = findWatch(id, &p);
    if (!w || !(w->events & flag)) return;
    w->events &= (~flag);
    if (w->events)
    {
	port_associate(svc->epfd, PORT_SOURCE_FD, id, w->events, 0);
    }
    else
    {
	if (p) p->next = w->next;
	else svc->watches[(unsigned)id & 31U] = w->next;
	free(w);
	port_dissociate(svc->epfd, PORT_SOURCE_FD, id);
    }
}

SOEXPORT void PSC_Service_registerRead(int id)
{
    registerWatch(id, POLLIN);
}

SOEXPORT void PSC_Service_unregisterRead(int id)
{
    unregisterWatch(id, POLLIN);
}

SOEXPORT void PSC_Service_registerWrite(int id)
{
    registerWatch(id, POLLOUT);
}

SOEXPORT void PSC_Service_unregisterWrite(int id)
{
    unregisterWatch(id, POLLOUT);
}

SOLOCAL int PSC_Service_epfd(void)
{
    return svc->epfd;
}
#endif

#ifdef HAVE_KQUEUE
static void flushChanges(void)
{
    struct kevent receipts[KQ_MAX_CHANGES];
    for (int i = 0; i < svc->nchanges; ++i)
    {
	svc->changes[i].flags |= EV_RECEIPT;
    }
    kevent(svc->kqfd, svc->changes, svc->nchanges, receipts, svc->nchanges, 0);
    svc->nchanges = 0;
}

static struct kevent *addChange(void)
{
    if (svc->nchanges == KQ_MAX_CHANGES) flushChanges();
    return svc->changes + svc->nchanges++;
}

static int initKqueue(void)
{
    svcinit();
    if (svc->kqfd >= 0) return 0;
#  if defined(HAVE_KQUEUEX)
    svc->kqfd = kqueuex(KQUEUE_CLOEXEC);
#  elif defined(HAVE_KQUEUE1)
    svc->kqfd = kqueue1(O_CLOEXEC);
#  else
    svc->kqfd = kqueue();
    if (svc->kqfd >= 0) fcntl(svc->kqfd, F_SETFD, FD_CLOEXEC);
#  endif
    if (svc->kqfd < 0) return -1;
    if (!svc->svcid)
    {
	struct sigaction sa = { .sa_handler = SIG_IGN };
	if (sigaction(SIGTERM, &sa, 0) < 0) goto fail;
	if (sigaction(SIGINT, &sa, 0) < 0) goto fail;
	if (sigaction(SIGALRM, &sa, 0) < 0) goto fail;

	EV_SET(addChange(), SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(addChange(), SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	EV_SET(addChange(), SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
    }
    return 0;

fail:
    close(svc->kqfd);
    svc->kqfd = -1;
    return -1;
}

static int verifyKqueue(int fd, int log)
{
    svcinit();
    if (svc->kqfd < 0)
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
    EpollWatch *w = svc->watches[(unsigned)fd & 31U];
    while (w)
    {
	if (w->fd == fd) break;
	if (parent) *parent = w;
	w = w->next;
    }
    return w;
}

static void registerWatch(int id, uint32_t flag)
{
    svcinit();
    if (svc->epfd < 0)
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
	epoll_ctl(svc->epfd, EPOLL_CTL_MOD, id, &ev);
    }
    else
    {
	w = PSC_malloc(sizeof *w);
	w->next = 0;
	w->fd = id;
	w->events = flag;
	EpollWatch *p = svc->watches[(unsigned)id & 31U];
	while (p && p->next) p = p->next;
	if (p) p->next = w;
	else svc->watches[(unsigned)id & 31U] = w;
	ev.events = w->events;
	epoll_ctl(svc->epfd, EPOLL_CTL_ADD, id, &ev);
    }
}

static void unregisterWatch(int id, uint32_t flag)
{
    if (svc->epfd < 0)
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
	epoll_ctl(svc->epfd, EPOLL_CTL_MOD, id, &ev);
    }
    else
    {
	if (p) p->next = w->next;
	else svc->watches[(unsigned)id & 31U] = w->next;
	free(w);
	epoll_ctl(svc->epfd, EPOLL_CTL_DEL, id, 0);
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
    for (size_t i = 0; i < svc->nfds; ++i)
    {
	if (svc->fds[i].fd == fd)
	{
	    if (idx) *idx = i;
	    return svc->fds + i;
	}
    }
    return 0;
}

static void registerPoll(int id, short flag)
{
    svcinit();
    if (!PSC_Service_isValidFd(id, "service")) return;
    struct pollfd *fd = findFd(id, 0);
    if (fd)
    {
	if (fd->events & flag) return;
	fd->events |= flag;
    }
    else
    {
	if (svc->nfds == svc->fdssz)
	{
	    svc->fdssz += 16;
	    svc->fds = PSC_realloc(svc->fds, svc->fdssz * sizeof *svc->fds);
	}
	fd = svc->fds + (svc->nfds++);
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
	--svc->nfds;
	memmove(svc->fds + i, svc->fds + i + 1,
		(svc->nfds - i) * sizeof *svc->fds);
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
    svcinit();
    if (!PSC_Service_isValidFd(id, "service")) return;
    if (FD_ISSET(id, &svc->readfds)) return;
    FD_SET(id, &svc->readfds);
    ++svc->nread;
    if (id >= svc->nfds) svc->nfds = id+1;
}

SOEXPORT void PSC_Service_unregisterRead(int id)
{
    svcinit();
    if (!PSC_Service_isValidFd(id, 0)) return;
    if (!FD_ISSET(id, &svc->readfds)) return;
    FD_CLR(id, &svc->readfds);
    --svc->nread;
    tryReduceNfds(id);
}

SOEXPORT void PSC_Service_registerWrite(int id)
{
    svcinit();
    if (!PSC_Service_isValidFd(id, "service")) return;
    if (FD_ISSET(id, &svc->writefds)) return;
    FD_SET(id, &svc->writefds);
    ++svc->nwrite;
    if (id >= svc->nfds) svc->nfds = id+1;
}

SOEXPORT void PSC_Service_unregisterWrite(int id)
{
    svcinit();
    if (!PSC_Service_isValidFd(id, 0)) return;
    if (!FD_ISSET(id, &svc->writefds)) return;
    FD_CLR(id, &svc->writefds);
    --svc->nwrite;
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
    flushChanges();
}

SOLOCAL void PSC_Service_armTimer(void *timer, unsigned ms, int periodic)
{
    if (initKqueue() < 0) return;
    EV_SET(addChange(), (uintptr_t)timer, EVFILT_TIMER,
	    EV_ADD|(!periodic * EV_ONESHOT), NOTE_MSECONDS, ms, 0);
    flushChanges();
}

SOLOCAL void PSC_Service_unarmTimer(void *timer, unsigned ms, int periodic)
{
    if (initKqueue() < 0) return;
    EV_SET(addChange(), (uintptr_t)timer, EVFILT_TIMER,
	    EV_DELETE|(!periodic * EV_ONESHOT), NOTE_MSECONDS, ms, 0);
    flushChanges();
}
#endif

#ifdef HAVE_SIGNALFD
static int initSigfd(void)
{
#if defined(SFD_NONBLOCK) && defined(SFD_CLOEXEC)
    sfd = signalfd(sfd, &sigblockmask, SFD_NONBLOCK|SFD_CLOEXEC);
#else
    sfd = signalfd(sfd, &sigblockmask, 0);
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
    SigHdlRec *r = sighdlrec(signo);

    if (svc && svc->running && signo != SIGTERM && signo != SIGINT
	    && signo != SIGALRM && signo != SIGCHLD)
    {
#ifdef WITH_SIGHDL
	struct sigaction sa;
#endif
	if (r && !handler)
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
	if (!r && handler)
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

    if (!r)
    {
	unsigned h = ((unsigned)signo) & 0x1fU;
	r = PSC_malloc(sizeof *r);
	r->next = 0;
	r->signo = signo;
	SigHdlRec *p = sigcallbacks[h];
	while (p && p->next) p = p->next;
	if (p) p->next = r;
	else sigcallbacks[h] = r;
    }
    r->hdl = handler;
}
#endif

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

#ifdef SIGFD_RDFD
static int handleSigfd(void)
{
    if (SIGFD_RDFD < 0) return -1;

    SIGFD_TYPE fdsi;
    ssize_t rrc;
    while ((rrc = read(SIGFD_RDFD, &fdsi, sizeof fdsi)) == sizeof fdsi)
    {
	switch (SIGFD_VALUE(fdsi))
	{
	    case SIGINT:
	    case SIGTERM:
		svc->shutdownRef = 0;
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
	SigHdlRec *r = sighdlrec(SIGFD_VALUE(fdsi));
	if (r && r->hdl)
	{
	    r->hdl(SIGFD_VALUE(fdsi));
	}
    }
    if (rrc < 0 && errno != EWOULDBLOCK) return -1;
    return 0;
}
#endif

#ifdef HAVE_EVPORTS
static const char *eventBackendInfo(void)
{
    return "event ports";
}

static int processEvents(void)
{
    port_event_t ev[EVP_MAX_EVENTS];
    unsigned nev = 1;
    int pgrc;
#ifdef WITH_SIGHDL
    if (!svc->svcid)
    {
	sigset_t origmask;
	pthread_sigmask(SIG_SETMASK, &sigorigmask, &origmask);
	errno = 0;
	do
	{
	    nev = 1;
	    pgrc = port_getn(svc->epfd, ev, EVP_MAX_EVENTS, &nev, 0);
	} while (pgrc < 0 && errno == EINTR && nev == 0);
	if (errno == EINTR) pgrc = 0;
	pthread_sigmask(SIG_SETMASK, &origmask, 0);
    }
    else
#endif
    {
	pgrc = port_getn(svc->epfd, ev, EVP_MAX_EVENTS, &nev, 0);
    }
    if (pgrc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "port_getn() failed");
	return -1;
    }
    for (unsigned i = 0; i < nev; ++i)
    {
	if (ev[i].portev_source == PORT_SOURCE_TIMER)
	{
	    PSC_Timer_doexpire(ev[i].portev_user);
	    continue;
	}
#ifdef SIGFD_RDFD
	if (!svc->svcid && ev[i].portev_events & POLLIN
		&& (int)ev[i].portev_object == SIGFD_RDFD)
	{
	    if (handleSigfd() >= 0)
	    {
		reregister(SIGFD_RDFD);
		continue;
	    }
	    PSC_Log_msg(PSC_L_ERROR, "reading signalfd failed");
	    return -1;
	}
#endif
	if (ev[i].portev_events & POLLOUT)
	{
	    PSC_Event_raise(&svc->readyWrite, (int)ev[i].portev_object, 0);
	    reregister((int)ev[i].portev_object);
	}
	if (ev[i].portev_events & POLLIN)
	{
	    PSC_Event_raise(&svc->readyRead, (int)ev[i].portev_object, 0);
	    reregister((int)ev[i].portev_object);
	}
    }
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
    int qrc;
    do
    {
	qrc = kevent(svc->kqfd, svc->changes, svc->nchanges,
		ev, KQ_MAX_EVENTS, 0);
    } while (qrc < 0 && errno == EINTR);
    if (qrc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "kevent() failed");
	return -1;
    }
    svc->nchanges = 0;
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
			svc->shutdownRef = 0;
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
		PSC_Event_raise(&svc->readyWrite, ev[i].ident, 0);
		break;

	    case EVFILT_READ:
		PSC_Event_raise(&svc->readyRead, ev[i].ident, 0);
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
    int prc;
#ifdef WITH_SIGHDL
    if (!svc->svcid)
    {
	do
	{
	    prc = epoll_pwait2(svc->epfd, ev, EP_MAX_EVENTS, 0, &sigorigmask);
	} while (prc < 0 && errno == EINTR);
    }
    else
#endif
    {
	prc = epoll_wait(svc->epfd, ev, EP_MAX_EVENTS, -1);
    }
    if (prc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "epoll_pwait2() failed");
	return -1;
    }
    for (int i = 0; i < prc; ++i)
    {
#ifdef SIGFD_RDFD
	if (!svc->svcid && ev[i].events & EPOLLIN
		&& ev[i].data.fd == SIGFD_RDFD)
	{
	    if (handleSigfd() >= 0) continue;
	    PSC_Log_msg(PSC_L_ERROR, "reading signalfd failed");
	    return -1;
	}
#endif
	if (ev[i].events & EPOLLOUT)
	{
	    PSC_Event_raise(&svc->readyWrite, ev[i].data.fd, 0);
	}
	if (ev[i].events & EPOLLIN)
	{
	    PSC_Event_raise(&svc->readyRead, ev[i].data.fd, 0);
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
    int prc;
#ifdef WITH_SIGHDL
    if (!svc->svcid)
    {
	sigset_t origmask;
	pthread_sigmask(SIG_SETMASK, &sigorigmask, &origmask);
	errno = 0;
	do
	{
	    prc = poll(svc->fds, svc->nfds, -1);
	} while (prc < 0 && errno == EINTR);
	pthread_sigmask(SIG_SETMASK, &origmask, 0);
    }
    else
#endif
    {
	prc = poll(svc->fds, svc->nfds, -1);
    }
    if (prc < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "poll() failed");
	return -1;
    }
    for (size_t i = 0; prc > 0 && i < svc->nfds; ++i)
    {
	if (!svc->fds[i].revents) continue;
	--prc;
#ifdef SIGFD_RDFD
	if (!svc->svcid && svc->fds[i].revents & POLLIN
		&& svc->fds[i].fd == SIGFD_RDFD)
	{
	    if (handleSigfd() >= 0) goto next;
	    PSC_Log_msg(PSC_L_ERROR, "reading signalfd failed");
	    return -1;
	}
#endif
	if (svc->fds[i].revents & POLLOUT)
	{
	    PSC_Event_raise(&svc->readyWrite, svc->fds[i].fd, 0);
	}
	if (svc->fds[i].revents & POLLIN)
	{
	    PSC_Event_raise(&svc->readyRead, svc->fds[i].fd, 0);
	}
next:
	svc->fds[i].revents = 0;
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
    if (svc->nread)
    {
	memcpy(&rfds, &svc->readfds, sizeof rfds);
	r = &rfds;
    }
    if (svc->nwrite)
    {
	memcpy(&wfds, &svc->writefds, sizeof wfds);
	w = &wfds;
    }
    int src;
#ifdef WITH_SIGHDL
    if (!svc->svcid)
    {
	do
	{
	    src = pselect(svc->nfds, r, w, 0, 0, &sigorigmask);
	} while (src < 0 && errno == EINTR);
    }
    else
#endif
    {
	src = select(svc->nfds, r, w, 0, 0);
    }
    if (src < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "pselect() failed");
	return -1;
    }
    if (w) for (int i = 0; src > 0 && i < svc->nfds; ++i)
    {
	if (FD_ISSET(i, w))
	{
	    --src;
	    PSC_Event_raise(&svc->readyWrite, i, 0);
	}
    }
    if (r) for (int i = 0; src > 0 && i < svc->nfds; ++i)
    {
	if (FD_ISSET(i, r))
	{
	    --src;
#ifdef SIGFD_RDFD
	    if (!svc->svcid && i == SIGFD_RDFD)
	    {
		if (handleSigfd() >= 0) continue;
		PSC_Log_msg(PSC_L_ERROR, "reading signalfd failed");
		return -1;
	    }
#endif
	    PSC_Event_raise(&svc->readyRead, i, 0);
	}
    }
    return 0;
}
#endif

static void shutdownWorker(void *arg)
{
    (void)arg;
    svc->shutdownRef = 0;
}

static void handleCommand(void *receiver, void *sender, void *args)
{
    (void)sender;

    Service *s = receiver;
    int fd = *((int *)args);
    sem_t *sem = s ? &s->svcid->cmdsem : &cmdsem;
    SvcCommand command;
    while (read(fd, &command, sizeof command) == sizeof command)
    {
	sem_wait(sem);
	command.func(command.arg);
    }
}

static int serviceLoop(ServiceLoopFlags flags);

static void *runsecondary(void *arg)
{
    svcinit();
    svc->svcid = arg;
    intptr_t rc = serviceLoop(0);
    return (void *)rc;
}

static int serviceLoop(ServiceLoopFlags flags)
{
    int rc = EXIT_FAILURE;

    PSC_RunOpts *opts = 0;

#if defined(WITH_SIGHDL) || defined(HAVE_KQUEUE)
    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
#endif

    if (flags & SLF_SVCMAIN)
    {
	opts = runOpts();

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

#ifdef WITH_SIGHDL
	if (pipe(sfd) < 0)
	{
	    PSC_Log_msg(PSC_L_FATAL, "service: cannot create signal pipe");
	    goto done;
	}
	fcntl(sfd[0], F_SETFD, FD_CLOEXEC);
	fcntl(sfd[1], F_SETFD, FD_CLOEXEC);
	fcntl(sfd[0], F_SETFL, fcntl(sfd[0], F_GETFL) | O_NONBLOCK);
	fcntl(sfd[1], F_SETFL, fcntl(sfd[1], F_GETFL) | O_NONBLOCK);

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
    }

    svcinit();

#ifdef HAVE_EVPORTS
    svc->epfd = port_create();
    if (svc->epfd < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: cannot create event port");
	goto done;
    }
    fcntl(svc->epfd, F_SETFD, O_CLOEXEC);
#endif

#ifdef HAVE_KQUEUE
    if (initKqueue() < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: cannot open kqueue");
	goto done;
    }
    if (svc->nchanges) flushChanges();
#endif

#ifdef HAVE_EPOLL
    svc->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (svc->epfd < 0)
    {
	PSC_Log_msg(PSC_L_FATAL, "service: cannot open epoll");
	goto done;
    }
#endif

    if (flags & SLF_SVCMAIN)
    {
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
	for (int h = 0; h < 32; ++h)
	{
	    for (SigHdlRec *r = sigcallbacks[h]; r; r = r->next)
	    {
		sigaddset(&sigblockmask, r->signo);
	    }
	}
	if (sigprocmask(SIG_SETMASK, &sigblockmask, 0) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "cannot set signal mask");
	    return rc;
	}
#endif

#ifdef HAVE_SIGNALFD
	initSigfd();
#endif

#ifdef WITH_SIGHDL
	for (int h = 0; h < 32; ++h)
	{
	    for (SigHdlRec *r = sigcallbacks[h]; r; r = r->next)
	    {
		switch (r->signo)
		{
		    case SIGTERM:
		    case SIGINT:
#ifndef HAVE_TIMERFD
		    case SIGALRM:
#endif
		    case SIGCHLD:
			break;

		    default:
			if (sigaction(r->signo, &handler, 0) < 0)
			{
			    PSC_Log_fmt(PSC_L_ERROR,
				    "cannot set signal handler for signal %d",
				    r->signo);
			}
		}
	    }
	}
#endif

#if defined(WITH_SIGHDL) || defined(HAVE_SIGNALFD)
	PSC_Service_registerRead(SIGFD_RDFD);
#endif

	if (pipe(commandpipe) < 0)
	{
	    PSC_Log_msg(PSC_L_ERROR, "service: error creating command pipe");
	    commandpipe[0] = -1;
	    commandpipe[1] = -1;
	    goto shutdown;
	}
	fcntl(commandpipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(commandpipe[1], F_SETFD, FD_CLOEXEC);
	fcntl(commandpipe[0], F_SETFL,
		fcntl(commandpipe[0], F_GETFL) | O_NONBLOCK);
	fcntl(commandpipe[1], F_SETFL,
		fcntl(commandpipe[1], F_GETFL) | O_NONBLOCK);
	PSC_Service_registerRead(commandpipe[0]);
	PSC_Event_register(&svc->readyRead, 0, handleCommand, commandpipe[0]);

	if ((flags & SLF_SVCRUN) && opts->daemonize)
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

	svc->running = 1;
	svc->shutdownRef = -1;
	mainsvc = svc;
	sem_init(&cmdsem, 0, 0);
#ifdef SVC_NO_ATOMICS
	sem_init(&shutdownrq, 0, 0);
#endif
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
#ifdef HAVE_EVPORTS
		"event ports"
#elif defined(HAVE_KQUEUE)
		"kqueue"
#elif defined(HAVE_TIMERFD)
		"timerfd"
#else
		"setitimer"
#endif
		")", eventBackendInfo());

	if ((rc = panicreturn()) != EXIT_SUCCESS) goto shutdown;

	if (opts->workerThreads)
	{
	    if (opts->workerThreads < 0)
	    {
#ifdef _SC_NPROCESSORS_CONF
		long ncpu = sysconf(_SC_NPROCESSORS_CONF);
		if (ncpu >= 1)
		{
		    nssvc = ncpu > INT_MAX ? INT_MAX : ncpu;
		}
		else
#endif
		{
		    nssvc = -opts->workerThreads;
		}
	    }
	    else nssvc = opts->workerThreads;
	}
	else nssvc = 0;

	if (nssvc)
	{
	    ssvc = PSC_malloc(nssvc * sizeof *ssvc);
	    memset(ssvc, 0, nssvc * sizeof *ssvc);
	    for (int i = 0; i < nssvc; ++i)
	    {
		if (pipe(ssvc[i].commandpipe) < 0)
		{
		    PSC_Log_msg(PSC_L_ERROR,
			    "service: error creating command pipe");
		    ssvc[i].commandpipe[1] = -1;
		    goto shutdown;
		}
		fcntl(ssvc[i].commandpipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(ssvc[i].commandpipe[1], F_SETFD, FD_CLOEXEC);
		fcntl(ssvc[i].commandpipe[0], F_SETFL,
			fcntl(ssvc[i].commandpipe[0], F_GETFL) | O_NONBLOCK);
		fcntl(ssvc[i].commandpipe[1], F_SETFL,
			fcntl(ssvc[i].commandpipe[1], F_GETFL) | O_NONBLOCK);
		ssvc[i].threadno = i;
		sem_init(&ssvc[i].cmdsem, 0, 0);
		if (pthread_create(&ssvc[i].handle, 0,
			    runsecondary, ssvc+i) != 0)
		{
		    PSC_Log_msg(PSC_L_ERROR,
			    "service: error creating worker thread");
		    close(ssvc[i].commandpipe[0]);
		    close(ssvc[i].commandpipe[1]);
		    ssvc[i].commandpipe[1] = -1;
		    goto shutdown;
		}
	    }
	}
    }
    else
    {
	PSC_Service_registerRead(svc->svcid->commandpipe[0]);
	PSC_Event_register(&svc->readyRead, svc, handleCommand,
		svc->svcid->commandpipe[0]);
	svc->running = 1;
	svc->shutdownRef = -1;
    }

    while (svc->shutdownRef != 0)
    {
	if (processEvents() < 0)
	{
	    rc = EXIT_FAILURE;
	    break;
	}
	PSC_Event_raise(&svc->eventsDone, 0, 0);
#ifdef SVC_NO_ATOMICS
	if (flags & SLF_SVCMAIN)
	{
	    int sr;
	    sem_getvalue(&shutdownrq, &sr);
	    if (sr)
	    {
		for (int i = 0; i < sr; ++i) sem_trywait(&shutdownrq);
		if (svc->shutdownRef == -1) svc->shutdownRef = 0;
	    }
	}
#endif
    }

shutdown:
    PSC_Timer_destroy(svc->shutdownTimer);
    svc->shutdownTimer = 0;
    svc->running = 0;
    if (flags & SLF_SVCMAIN) PSC_Log_msg(PSC_L_DEBUG, "service shutting down");

done:
    if (flags & SLF_SVCMAIN)
    {
	if (nssvc)
	{
	    for (int i = 0; i < nssvc; ++i)
	    {
		if (ssvc[i].commandpipe[1] < 0) break;
		PSC_Service_runOnThread(i, shutdownWorker, 0);
		pthread_join(ssvc[i].handle, 0);
		sem_destroy(&ssvc[i].cmdsem);
		close(ssvc[i].commandpipe[0]);
		close(ssvc[i].commandpipe[1]);
	    }
	    free(ssvc);
	    nssvc = 0;
	}

#ifdef SVC_NO_ATOMICS
	sem_destroy(&shutdownrq);
#endif
	sem_destroy(&cmdsem);

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

	if (commandpipe[0] >= 0) close(commandpipe[0]);
	if (commandpipe[1] >= 0) close(commandpipe[1]);

#ifdef HAVE_SIGNALFD
	if (sfd >= 0)
	{
	    close(sfd);
	    sfd = -1;
	}
#endif

#ifdef WITH_SIGHDL
	if (sfd[0] >= 0)
	{
	    close(sfd[0]);
	    sfd[0] = -1;
	}

	if (sfd[1] >= 0)
	{
	    close(sfd[1]);
	    sfd[1] = -1;
	}
#endif

#if defined(WITH_SIGHDL) || defined(HAVE_SIGNALFD)
	for (int i = 0; i < 32; ++i)
	{
	    SigHdlRec *n = 0;
	    SigHdlRec *r = sigcallbacks[i];
	    while (r)
	    {
		n = r->next;
		free(r);
		r = n;
	    }
	    sigcallbacks[i] = 0;
	}
#endif

	mainsvc = 0;
    }

#ifdef WITH_POLL
    free(svc->fds);
#endif

#ifdef HAVE_EVPORTS
    if (svc->epfd >= 0)
    {
	close(svc->epfd);
    }
    for (int i = 0; i < 32; ++i)
    {
	EvportWatch *n = 0;
	EvportWatch *w = svc->watches[i];
	while (w)
	{
	    n = w->next;
	    free(w);
	    w = n;
	}
    }
#endif

#ifdef HAVE_KQUEUE
    if (svc->kqfd >= 0)
    {
	close(svc->kqfd);
    }
#endif

#ifdef HAVE_EPOLL
    if (svc->epfd >= 0)
    {
	close(svc->epfd);
    }
    for (int i = 0; i < 32; ++i)
    {
	EpollWatch *n = 0;
	EpollWatch *w = svc->watches[i];
	while (w)
	{
	    n = w->next;
	    free(w);
	    w = n;
	}
    }
#endif

    PSC_Event_destroyStatic(&svc->readyRead);
    PSC_Event_destroyStatic(&svc->readyWrite);
    PSC_Event_destroyStatic(&svc->eventsDone);
    free(svc);
    svc = 0;

    return rc;
}

static int serviceMain(void *data)
{
    (void)data;

    int rc = EXIT_FAILURE;

    if (PSC_ThreadPool_init() < 0) goto done;

    rc = serviceLoop(SLF_SVCRUN | SLF_SVCMAIN);

done:
    PSC_ThreadPool_done();

    PSC_Event_destroyStatic(&prestartup);
    PSC_Event_destroyStatic(&startup);
    PSC_Event_destroyStatic(&shutdown);
    PSC_Event_destroyStatic(&childExited);

    return rc;
}

SOEXPORT int PSC_Service_loop(void)
{
    return serviceLoop(SLF_SVCMAIN);
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
    if (!mainsvc) return;
#ifdef SVC_NO_ATOMICS
    if (mainsvc == svc)
    {
	if (mainsvc->shutdownRef == -1) mainsvc->shutdownRef = 0;
    }
    else sem_post(&shutdownrq);
#else
    int expected = -1;
    atomic_compare_exchange_strong(&mainsvc->shutdownRef, &expected, 0);
#endif
}

static void shutdownTimeout(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    if (svc) svc->shutdownRef = 0;
}

SOEXPORT void PSC_Service_shutdownLock(void)
{
    if (svc->shutdownRef >= 0) ++svc->shutdownRef;
    if (!svc->shutdownTimer)
    {
	svc->shutdownTimer = PSC_Timer_create();
	PSC_Event_register(PSC_Timer_expired(svc->shutdownTimer), 0,
		shutdownTimeout, 0);
	PSC_Timer_setMs(svc->shutdownTimer, 5000);
	PSC_Timer_start(svc->shutdownTimer, 0);
    }
}

SOEXPORT void PSC_Service_shutdownUnlock(void)
{
    if (svc->shutdownRef > 0) --svc->shutdownRef;
}

SOEXPORT void PSC_Service_panic(const char *msg)
{
    int mainpanic = 0;
    if (svc && !svc->svcid && svc->running)
    {
	mainpanic = 1;
	for (int i = 0; i < numPanicHandlers; ++i)
	{
	    panicHandlers[i](msg);
	}
    }
    PSC_Log_setPanic();
    PSC_Log_msg(PSC_L_FATAL, msg);
    if (mainpanic) longjmp(panicjmp, -1);
    else abort();
}

SOEXPORT int PSC_Service_workers(void)
{
    return nssvc;
}

SOEXPORT int PSC_Service_threadNo(void)
{
    if (!svc) return -2;
    if (!svc->svcid) return -1;
    return svc->svcid->threadno;
}

SOEXPORT void PSC_Service_runOnThread(int threadNo,
	PSC_OnThreadExec func, void *arg)
{
    SvcCommand cmd = { .func = func, .arg = arg };
    if (threadNo < 0)
    {
	if (svc && !svc->svcid)
	{
	    func(arg);
	    return;
	}
	if (write(commandpipe[1], &cmd, sizeof cmd) != sizeof cmd)
	{
	    PSC_Log_msg(PSC_L_WARNING,
		    "service: cannot execute on main thread");
	}
	sem_post(&cmdsem);
	return;
    }
    if (threadNo >= nssvc)
    {
	PSC_Log_msg(PSC_L_ERROR,
		"service: attempt to run on non-existing thread");
	return;
    }
    if (svc && svc->svcid && threadNo == svc->svcid->threadno)
    {
	func(arg);
	return;
    }
    if (write(ssvc[threadNo].commandpipe[1], &cmd, sizeof cmd) != sizeof cmd)
    {
	PSC_Log_msg(PSC_L_WARNING, "service: cannot execute on worker thread");
    }
    sem_post(&ssvc[threadNo].cmdsem);
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
    return svc->shutdownRef >= 0;
}

