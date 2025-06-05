# WITH_ATOMICS		If possible, use atomic operations to avoid locks
#			(default: on)
# WITH_EVPORTS		Force using event ports over select() even if not
#			detected (default: off)
# WITH_EPOLL		Force using epoll() over select() even if not detected
#			(default: off)
# WITH_KQUEUE		Force using kqueue() over select() even if not detected
#			(default: off)
# WITH_POLL		Prefer using poll() over select() (default: off)
# WITH_TLS		Build with TLS support (default: on)
# WITHOUT_EVPORTS	Disable using event ports over select() even if
# 			available (default: off)
# WITHOUT_EPOLL		Disable using epoll() over select() even if available
#			(default: off)
# WITHOUT_KQUEUE	Disable using kqueue() over select() even if available
#			(default: off)
# WITH_EVENTFD		Force using eventfd() for waking up other worker
#			threads, even if not detected (default: off)
# WITHOUT_EVENTFD	Disable using eventfd() even if available
#			(default: off)
# WITH_SIGNALFD		Force using signalfd() for handling signals even if not
# 			detected (default: off)
# WITHOUT_SIGNALFD	Disable using signalfd() even if available
# 			(default: off)
# WITH_TIMERFD		Force using timerfd for timers even if not detected
# 			(default: off)
# WITHOUT_TIMERFD	Disable using timerfd even if available
# 			(default: off)
# FD_SETSIZE		Number of file descriptors usable with select(), for
#			systems allowing to configure that (default: 4096)
# OPENSSLINC		Path to OpenSSL include files, overriding pkgconfig
# 			(default: empty)
# OPENSSLLIB		Path to OpenSSL libraries, overriding pkgconfig
# 			(default: empty)

BOOLCONFVARS_ON=	WITH_ATOMICS WITH_TLS
BOOLCONFVARS_OFF=	WITH_EVENTFD WITH_EVPORTS WITH_EPOLL WITH_KQUEUE \
			WITH_POLL WITH_SIGNALFD WITH_TIMERFD \
			WITHOUT_EVENTFD WITHOUT_EVPORTS WITHOUT_EPOLL \
			WITHOUT_KQUEUE WITHOUT_SIGNALFD WITHOUT_TIMERFD
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
