#ifndef POSER_CORE_INT_EVENT_H
#define POSER_CORE_INT_EVENT_H

#include <poser/core/event.h>

#include <stddef.h>

C_CLASS_DECL(EvHandlerEntry);
C_CLASS_DECL(EvHandlerPool);

struct PSC_Event
{
    void *sender;
    EvHandlerPool *pool;
    EvHandlerEntry *first;
    EvHandlerEntry *last;
    EvHandlerEntry *handling;
};

void PSC_Event_initStatic(PSC_Event *self, void *sender) CMETHOD;
void PSC_Event_destroyStatic(PSC_Event *self);

#endif
