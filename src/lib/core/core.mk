posercore_MODULES:=		client \
				connection \
				daemon \
				event \
				log \
				runopts \
				server \
				service \
				threadpool \
				util

posercore_HEADERS_INSTALL:=	core \
				core/client \
				core/connection \
				core/daemon \
				core/event \
				core/log \
				core/proto \
				core/runopts \
				core/server \
				core/service \
				core/threadpool \
				core/util \
				decl

posercore_LDFLAGS:=		-pthread
posercore_HEADERDIR:=		include$(PSEP)poser
posercore_HEADERTGTDIR:=	$(includedir)$(PSEP)poser
posercore_V_MAJ:=		1
posercore_V_MIN:=		0
posercore_V_REV:=		0

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
