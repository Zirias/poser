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
				core/util \
				decl

posercore_LDFLAGS:=		-pthread
posercore_HEADERDIR:=		include$(PSEP)poser
posercore_HEADERTGTDIR:=	$(includedir)$(PSEP)poser
posercore_V_MAJ:=		1
posercore_V_MIN:=		0
posercore_V_REV:=		1

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
posercore_LIBS+=		ssl
  else
posercore_PKGDEPS+=		libssl
  endif
posercore_DEFINES+=		-DWITH_TLS
endif

$(call librules, posercore)
