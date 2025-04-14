posercore_PRECHECK=		ACCEPT4 GETRANDOM XXHX86
ACCEPT4_FUNC=			accept4
ACCEPT4_CFLAGS=			-D_GNU_SOURCE
ACCEPT4_HEADERS=		sys/types.h sys/socket.h
ACCEPT4_ARGS=			int, struct sockaddr *, socklen_t *, int
GETRANDOM_FUNC=			getrandom
GETRANDOM_HEADERS=		sys/random.h
GETRANDOM_RETURN=		ssize_t
GETRANDOM_ARGS=			void *, size_t, unsigned
XXHX86_FUNC=			XXH_featureTest
XXHX86_CFLAGS=			-I./$(posercore_SRCDIR)/contrib/xxHash
XXHX86_HEADERS=			xxh_x86dispatch.c
XXHX86_ARGS=			void
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

ifneq ($(WITHOUT_EPOLL),1)
posercore_PRECHECK+=		EPOLL
endif

ifneq ($(WITHOUT_KQUEUE),1)
posercore_PRECHECK+=		KQUEUE KQUEUEX KQUEUE1
endif

posercore_MODULES:=		base64 \
				certinfo \
				client \
				connection \
				daemon \
				dictionary \
				event \
				hash \
				hashtable \
				list \
				log \
				queue \
				random \
				runopts \
				server \
				service \
				stringbuilder \
				threadpool \
				timer \
				util \
				xxhash \
				xxhx86

posercore_HEADERS_INSTALL:=	core \
				core/base64 \
				core/certinfo \
				core/client \
				core/connection \
				core/daemon \
				core/dictionary \
				core/event \
				core/hash \
				core/hashtable \
				core/list \
				core/log \
				core/proto \
				core/queue \
				core/random \
				core/runopts \
				core/server \
				core/service \
				core/stringbuilder \
				core/threadpool \
				core/timer \
				core/util \
				decl

posercore_PRECFLAGS?=		-I.$(PSEP)include
posercore_DEFINES:=		#
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

ifeq ($(WITH_EPOLL),1)
  ifeq ($(WITHOUT_EPOLL),1)
    $(error Cannot set both WITH_EPOLL and WITHOUT_EPOLL)
  endif
  ifeq ($(WITH_POLL),1)
    $(error Cannot set both WITH_EPOLL and WITH_POLL)
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
  ifeq ($(WITH_POLL),1)
    $(error Cannot set both WITH_POLL and WITH_KQUEUE)
  endif
  ifneq ($(posercore_HAVE_KQUEUE),1)
    $(error Requested kqueue (WITH_KQUEUE), but not found)
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
