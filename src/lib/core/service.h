#ifndef POSER_CORE_INT_SERVICE_H
#define POSER_CORE_INT_SERVICE_H

#include <poser/core/service.h>

#ifdef _POSIX_C_SOURCE
#  if _POSIX_C_SOURCE < 200112L
#    undef _POSIX_C_SOURCE
#  endif
#endif
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200112L
#endif
#include <time.h>

C_CLASS_DECL(PSC_Timer);

int PSC_Service_shutsdown(void);
int PSC_Service_attachTimer(PSC_Timer *t, timer_t *tid);
void PSC_Service_detachTimer(PSC_Timer *t);

#endif
