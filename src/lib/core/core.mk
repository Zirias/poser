posercore_PRECHECK=		ACCEPT4 ARC4R GETRANDOM MADVISE MADVFREE \
				MANON MANONYMOUS MSTACK TLS_C11 TLS_GNU \
				UCONTEXT XXHX86
ACCEPT4_FUNC=			accept4
ACCEPT4_CFLAGS=			-D_GNU_SOURCE
ifneq ($(findstring -solaris,$(TARGETARCH)),)
ACCEPT4_CFLAGS+=		-Wno-macro-redefined -D_XOPEN_SOURCE=500
endif
ACCEPT4_HEADERS=		sys/types.h sys/socket.h
ACCEPT4_ARGS=			int, struct sockaddr *, socklen_t *, int
ARC4R_FUNC=			arc4random_buf
ARC4R_CFLAGS=			-D_DEFAULT_SOURCE
ARC4R_HEADERS=			stdlib.h
ARC4R_ARGS=			void *, size_t
ARC4R_RETURN=			void
GETRANDOM_FUNC=			getrandom
GETRANDOM_HEADERS=		sys/random.h
GETRANDOM_RETURN=		ssize_t
GETRANDOM_ARGS=			void *, size_t, unsigned
MADVISE_FUNC=			madvise
MADVISE_CFLAGS=			-D_DEFAULT_SOURCE
MADVISE_HEADERS=		sys/mman.h
MADVISE_ARGS=			void *, size_t, int
MADVFREE_FLAG=			MADV_FREE
MADVFREE_CFLAGS=		-D_DEFAULT_SOURCE
MADVFREE_HEADERS=		sys/mman.h
MANON_FLAG=			MAP_ANON
MANON_CFLAGS=			-D_DEFAULT_SOURCE
MANON_HEADERS=			sys/mman.h
MANONYMOUS_FLAG=		MAP_ANONYMOUS
MANONYMOUS_CFLAGS=		-D_DEFAULT_SOURCE
MANONYMOUS_HEADERS=		sys/mman.h
MSTACK_FLAG=			MAP_STACK
MSTACK_CFLAGS=			-D_DEFAULT_SOURCE
MSTACK_HEADERS=			sys/mman.h
TLS_C11_TYPE=			_Thread_local int
TLS_GNU_TYPE=			__thread int
UCONTEXT_TYPE=			ucontext_t
UCONTEXT_HEADERS=		ucontext.h
XXHX86_FUNC=			XXH_featureTest
XXHX86_CFLAGS=			-I./$(posercore_SRCDIR)/contrib/xxHash
XXHX86_HEADERS=			xxh_x86dispatch.c
XXHX86_ARGS=			void
EVENTFD_FUNC=			eventfd
EVENTFD_HEADERS=		sys/eventfd.h
EVENTFD_ARGS=			unsigned, int
EVPORTS_FUNC=			port_create
EVPORTS_HEADERS=		port.h
EVPORTS_ARGS=			void
EPOLL_FUNC=			epoll_pwait2
EPOLL_HEADERS=			sys/epoll.h
EPOLL_ARGS=			int, struct epoll_event [], int, \
				const struct timespec *, const sigset_t *
KQUEUE_FUNC=			kqueue
KQUEUE_HEADERS=			sys/types.h sys/event.h sys/time.h
KQUEUE_ARGS=			void
KQUEUEX_FUNC=			kqueuex
KQUEUEX_HEADERS=		sys/types.h sys/event.h sys/time.h
KQUEUEX_ARGS=			u_int
KQUEUE1_FUNC=			kqueue1
KQUEUE1_HEADERS=		sys/types.h sys/event.h sys/time.h
SIGNALFD_FUNC=			signalfd
SIGNALFD_HEADERS=		sys/signalfd.h
SIGNALFD_ARGS=			int, const sigset_t *, int
TIMERFD_FUNC=			timerfd_create
TIMERFD_HEADERS=		sys/timerfd.h
TIMERFD_ARGS=			int, int

ifneq ($(WITHOUT_EVENTFD),1)
posercore_PRECHECK+=		EVENTFD
endif

ifneq ($(WITHOUT_EVPORTS),1)
posercore_PRECHECK+=		EVPORTS
endif

ifneq ($(WITHOUT_EPOLL),1)
posercore_PRECHECK+=		EPOLL
endif

ifneq ($(WITHOUT_KQUEUE),1)
posercore_PRECHECK+=		KQUEUE KQUEUEX KQUEUE1
endif

ifneq ($(WITHOUT_SIGNALFD),1)
posercore_PRECHECK+=		SIGNALFD
endif

ifneq ($(WITHOUT_TIMERFD),1)
posercore_PRECHECK+=		TIMERFD
endif

posercore_MODULES=		base64 \
				certinfo \
				client \
				connection \
				daemon \
				dictionary \
				event \
				hash \
				hashtable \
				ipaddr \
				json \
				list \
				log \
				process \
				queue \
				random \
				ratelimit \
				resolver \
				runopts \
				server \
				service \
				$(if $(filter 1,$(posercore_HAVE_UCONTEXT)), \
					stackmgr) \
				stringbuilder \
				threadpool \
				timer \
				util \
				xxhash \
				xxhx86

posercore_HEADERS_INSTALL=	core \
				core/base64 \
				core/certinfo \
				core/client \
				core/connection \
				core/daemon \
				core/dictionary \
				core/event \
				core/hash \
				core/hashtable \
				core/ipaddr \
				core/json \
				core/list \
				core/log \
				core/process \
				core/proto \
				core/queue \
				core/random \
				core/ratelimit \
				core/resolver \
				core/runopts \
				core/server \
				core/service \
				core/stringbuilder \
				core/threadpool \
				core/timer \
				core/util \
				decl

posercore_PRECFLAGS?=		-I.$(PSEP)include
posercore_DEFINES=		#
posercore_LDFLAGS=		-pthread
posercore_HEADERDIR=		include$(PSEP)poser
posercore_HEADERTGTDIR=		$(includedir)$(PSEP)poser
posercore_VERSION=		2.0.0

ifneq ($(findstring -solaris,$(TARGETARCH)),)
posercore_PRECFLAGS+=		-D__EXTENSIONS__
posercore_LIBS+=		socket
endif

ifneq ($(WITH_ATOMICS),1)
posercore_DEFINES+=		-DNO_ATOMICS
endif

ifeq ($(WITH_POLL),1)
posercore_DEFINES+=		-DWITH_POLL
endif

ifeq ($(WITH_TLS),1)
  ifneq ($(OPENSSLINC)$(OPENSSLLIB),)
    ifeq ($(OPENSSLINC),)
$(error OPENSSLLIB specified without OPENSSLINC)
    endif
    ifeq ($(OPENSSLLIB),)
$(error OPENSSLINC specified without OPENSSLLIB)
    endif
posercore_INCLUDES+=		-I$(OPENSSLINC)
posercore_LDFLAGS+=		-L$(OPENSSLLIB)
posercore_LIBS+=		ssl crypto
  else
posercore_PKGDEPS+=		openssl >= 1.1
  endif
posercore_DEFINES+=		-DWITH_TLS
endif

$(call librules, posercore)

ifeq ($(posercore_HAVE_TLS_C11),1)
posercore_DEFINES+=		-DTHREADLOCAL=_Thread_local
else
  ifeq ($(posercore_HAVE_TLS_GNU),1)
posercore_DEFINES+=		-DTHREADLOCAL=__thread
  else
    $(error No thread-local-storage suppport detected, cannot build poser)
  endif
endif

ifneq ($(WITH_POLL),1)
  ifneq ($(posercore_HAVE_EPOLL),1)
    ifneq ($(posercore_HAVE_KQUEUE),1)
posercore_DEFINES+=		-DFD_SETSIZE=$(FD_SETSIZE)
    endif
  endif
endif

ifeq ($(WITH_EVENTFD),1)
  ifeq ($(WITHOUT_EVENTFD),1)
    $(error Cannot set both WITH_EVENTFD and WITHOUT_EVENTFD)
  endif
  ifneq ($(posercore_HAVE_EVENTFD),1)
    $(error Requested eventfd (WITH_EVENTFD), but not found)
  endif
endif

ifeq ($(WITH_EVPORTS),1)
  ifeq ($(WITHOUT_EVPORTS),1)
    $(error Cannot set both WITH_EVPORTS and WITHOUT_EVPORTS)
  endif
  ifeq ($(WITH_POLL),1)
    $(error Cannot set both WITH_EVPORTS and WITH_POLL)
  endif
  ifneq ($(posercore_HAVE_EVPORTS),1)
    $(error Requested event ports (WITH_EVPORTS), but not found)
  endif
endif

ifeq ($(WITH_EPOLL),1)
  ifeq ($(WITHOUT_EPOLL),1)
    $(error Cannot set both WITH_EPOLL and WITHOUT_EPOLL)
  endif
  ifeq ($(WITH_EVPORTS),1)
    $(error Cannot set both WITH_EVPORTS and WITH_EPOLL)
  endif
  ifeq ($(WITH_POLL),1)
    $(error Cannot set both WITH_POLL and WITH_EPOLL)
  endif
  ifneq ($(posercore_HAVE_EPOLL),1)
    $(error Requested epoll (WITH_EPOLL), but not found)
  endif
endif

ifeq ($(WITH_KQUEUE),1)
  ifeq ($(WITHOUT_KQUEUE),1)
    $(error Cannot set both WITH_KQUEUE and WITHOUT_KQUEUE)
  endif
  ifeq ($(WITH_EPOLL),1)
    $(error Cannot set both WITH_EPOLL and WITH_KQUEUE)
  endif
  ifeq ($(WITH_EVPORTS),1)
    $(error Cannot set both WITH_EVPORTS and WITH_KQUEUE)
  endif
  ifeq ($(WITH_POLL),1)
    $(error Cannot set both WITH_POLL and WITH_KQUEUE)
  endif
  ifneq ($(posercore_HAVE_KQUEUE),1)
    $(error Requested kqueue (WITH_KQUEUE), but not found)
  endif
endif

ifeq ($(WITH_SIGNALFD),1)
  ifeq ($(WITHOUT_SIGNALFD),1)
    $(error Cannot set both WITH_SIGNALFD and WITHOUT_SIGNALFD)
  endif
  ifneq ($(posercore_HAVE_SIGNALFD),1)
    $(error Requested signalfd (WITH_SIGNALFD), but not found)
  endif
endif

ifeq ($(WITH_TIMERFD),1)
  ifeq ($(WITHOUT_TIMERFD),1)
    $(error Cannot set both WITH_TIMERFD and WITHOUT_TIMERFD)
  endif
  ifneq ($(posercore_HAVE_TIMERFD),1)
    $(error Requested timerfd (WITH_TIMERFD), but not found)
  endif
endif

ifneq ($(posercore_HAVE_ACCEPT4),1)
define posercore_warn_accept4
**WARNING**

GNU-style accept4() API not detected, falling back to non-atomic
configuration of socket file descriptors.

This introduces a small risk to leak sockets to child processes!

endef
$(warning $(posercore_warn_accept4))
endif

ifneq ($(posercore_HAVE_UCONTEXT),1)
define posercore_warn_ucontext
**WARNING**

User context switching (<ucontext.h>) not detected. Awaiting a PSC_AsyncTask
will therefore block the thread doing this until the task is completed.

This might affect performance of applications using this feature.

endef
$(warning $(posercore_warn_ucontext))
endif
