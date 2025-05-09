#ifndef POSER_CORE_INT_TIMER_H
#define POSER_CORE_INT_TIMER_H

#include <poser/core/timer.h>

#if defined(HAVE_EVPORTS) || defined(HAVE_KQUEUE)
void PSC_Timer_doexpire(PSC_Timer *self) CMETHOD;
#elif !defined(HAVE_TIMERFD)
void PSC_Timer_underrun(void);
#endif

#endif
