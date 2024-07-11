#ifndef POSER_CORE_INT_SERVICE_H
#define POSER_CORE_INT_SERVICE_H

#include <poser/core/service.h>

#define _POSIX_C_SOURCE 200112L
#include <time.h>

C_CLASS_DECL(PSC_Timer);

int PSC_Service_shutsdown(void);
int PSC_Service_attachTimer(PSC_Timer *t, timer_t *tid);
void PSC_Service_detachTimer(PSC_Timer *t);

#endif
