#ifndef POSER_CORE_INT_SERVICE_H
#define POSER_CORE_INT_SERVICE_H

#include <poser/core/service.h>

int PSC_Service_running(void);
int PSC_Service_shutsdown(void);

#ifdef HAVE_EVPORTS
#  undef HAVE_KQUEUE
int PSC_Service_epfd(void);
#endif

#ifdef HAVE_KQUEUE
void PSC_Service_armTimer(void *timer, unsigned ms, int periodic);
void PSC_Service_unarmTimer(void *timer, unsigned ms, int periodic);
#endif

#endif
