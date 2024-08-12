# WITH_TLS	Build with TLS support (default: on)
# OPENSSLINC	Path to OpenSSL include files, overriding pkgconfig
# 		(default: empty)
# OPENSSLLIB	Path to OpenSSL libraries, overriding pkgconfig
# 		(default: empty)

BOOLCONFVARS_ON=	WITH_TLS
SINGLECONFVARS=		OPENSSLINC OPENSSLLIB
USES=			pkgconfig

include zimk/zimk.mk

INCLUDES += -I.$(PSEP)include

$(call zinc, src/lib/core/core.mk)

DOXYGEN?=	doxygen

docs:
	rm -fr html/*
	doxygen

.PHONY: docs
