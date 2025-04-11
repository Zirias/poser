posercore_PRECHECK:=		ACCEPT4
ACCEPT4_FUNC:=			accept4
ACCEPT4_CFLAGS:=		-D_GNU_SOURCE
ACCEPT4_HEADERS:=		sys/types.h sys/socket.h
ACCEPT4_ARGS:=			int, struct sockaddr *, socklen_t *, int
EPOLL_FUNC:=			epoll_pwait2
EPOLL_HEADERS:=			sys/epoll.h
EPOLL_ARGS:=			int, struct epoll_event [], int, \
				const struct timespec *, const sigset_t *
KQUEUE_FUNC:=			kqueue
KQUEUE_HEADERS:=		sys/types.h sys/event.h sys/time.h
KQUEUE_ARGS:=			void
KQUEUEX_FUNC:=			kqueuex
KQUEUEX_HEADERS:=		sys/types.h sys/event.h sys/time.h
KQUEUEX_ARGS:=			u_int
KQUEUE1_FUNC:=			kqueue1
KQUEUE1_HEADERS:=		sys/types.h sys/event.h sys/time.h

posercore_DEFINES:=		#

ifeq ($(WITH_EPOLL),1)
  ifeq ($(WITH_POLL),1)
    $(error Cannot set both WITH_EPOLL and WITH_POLL)
  endif
posercore_DEFINES+=		-DHAVE_EPOLL
posercore_HAVE_EPOLL:=		1
else
  ifneq ($(WITHOUT_EPOLL),1)
posercore_PRECHECK+=		EPOLL
  endif
endif

ifeq ($(WITH_KQUEUE),1)
  ifeq ($(WITH_EPOLL),1)
    $(error Cannot set both WITH_EPOLL and WITH_KQUEUE)
  endif
  ifeq ($(WITH_POLL),1)
    $(error Cannot set both WITH_POLL and WITH_KQUEUE)
  endif
posercore_DEFINES+=		-DHAVE_KQUEUE
posercore_HAVE_KQUEUE:=		1
posercore_PRECHECK+=		KQUEUEX KQUEUE1
else
  ifneq ($(WITHOUT_KQUEUE),1)
posercore_PRECHECK+=		KQUEUE KQUEUEX KQUEUE1
  endif
endif

posercore_MODULES:=		certinfo \
				client \
				connection \
				daemon \
				event \
				hashtable \
				list \
				log \
				queue \
				runopts \
				server \
				service \
				stringbuilder \
				threadpool \
				timer \
				util

posercore_HEADERS_INSTALL:=	core \
				core/certinfo \
				core/client \
				core/connection \
				core/daemon \
				core/event \
				core/hashtable \
				core/list \
				core/log \
				core/proto \
				core/queue \
				core/runopts \
				core/server \
				core/service \
				core/stringbuilder \
				core/threadpool \
				core/timer \
				core/util \
				decl

posercore_PRECFLAGS?=		-I.$(PSEP)include
posercore_LDFLAGS:=		-pthread
posercore_HEADERDIR:=		include$(PSEP)poser
posercore_HEADERTGTDIR:=	$(includedir)$(PSEP)poser
posercore_VERSION:=		1.2.2

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
posercore_PKGDEPS+=		openssl
  endif
posercore_DEFINES+=		-DWITH_TLS
endif

$(call librules, posercore)

ifneq ($(WITH_POLL),1)
  ifneq ($(posercore_HAVE_EPOLL),1)
    ifneq ($(posercore_HAVE_KQUEUE),1)
posercore_DEFINES+=		-DFD_SETSIZE=$(FD_SETSIZE)
    endif
  endif
endif

ifneq ($(posercore_HAVE_ACCEPT4),1)
define posercore_warn
**WARNING**

GNU-style accept4() API not detected, falling back to non-atomic
configuration of socket file descriptors.

This introduces a small risk to leak sockets to child processes!

endef
$(warning $(posercore_warn))
endif
