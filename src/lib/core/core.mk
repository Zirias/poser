posercore_PRECHECK:=		ACCEPT4
ACCEPT4_FUNC:=			accept4
ACCEPT4_HEADERS:=		sys/types.h sys/socket.h
ACCEPT4_ARGS:=			int, struct sockaddr *, socklen_t *, int

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

posercore_LDFLAGS:=		-pthread
posercore_HEADERDIR:=		include$(PSEP)poser
posercore_HEADERTGTDIR:=	$(includedir)$(PSEP)poser
posercore_VERSION:=		1.2.2

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

ifneq ($(posercore_HAVE_ACCEPT4),1)
define posercore_warn
**WARNING**

GNU-style accept4() API not detected, falling back to non-atomic
configuration of socket file descriptors.

This introduces a small risk to leak sockets to child processes!

endef
$(warning $(posercore_warn))
endif
