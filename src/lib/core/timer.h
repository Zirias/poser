#ifndef POSER_CORE_INT_TIMER_H
#define POSER_CORE_INT_TIMER_H

#include <poser/core/timer.h>

#ifdef HAVE_KQUEUE
void PSC_Timer_doexpire(PSC_Timer *self) CMETHOD;
#else
void PSC_Timer_underrun(void);
#endif

#endif
