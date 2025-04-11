# WITH_POLL	Prefer using poll() over select() (default: off)
# WITH_TLS	Build with TLS support (default: on)
# FD_SETSIZE	Number of file descriptors usable with select(), for systems
#		allowing to configure that (default: 4096)
# OPENSSLINC	Path to OpenSSL include files, overriding pkgconfig
# 		(default: empty)
# OPENSSLLIB	Path to OpenSSL libraries, overriding pkgconfig
# 		(default: empty)

BOOLCONFVARS_ON=	WITH_TLS
BOOLCONFVARS_OFF=	WITH_POLL
SINGLECONFVARS=		FD_SETSIZE OPENSSLINC OPENSSLLIB

DEFAULT_FD_SETSIZE=	4096

USES=			pkgconfig

include zimk/zimk.mk

$(call zinc, src/lib/core/core.mk)

DOXYGEN?=	doxygen

docs:
	rm -fr html/*
	doxygen

.PHONY: docs
