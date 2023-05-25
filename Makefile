BOOLCONFVARS=	WITH_TLS
SINGLECONFVARS=	OPENSSLINC OPENSSLLIB

# Default configuration
WITH_TLS?=	1		# Build posercore with TLS support

include zimk/zimk.mk

INCLUDES += -I.$(PSEP)include

$(call zinc, src/lib/core/core.mk)

DOXYGEN?=	doxygen

docs:
	rm -fr html/*
	doxygen

.PHONY: docs
