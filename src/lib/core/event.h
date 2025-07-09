#ifndef POSER_CORE_INT_EVENT_H
#define POSER_CORE_INT_EVENT_H

#include <poser/core/event.h>

#include <stddef.h>

C_CLASS_DECL(EvHandlerEntry);

struct PSC_Event
{
    void *pool;
    void *sender;
    EvHandlerEntry *first;
    EvHandlerEntry *last;
    EvHandlerEntry *handling;
};

void PSC_Event_initStatic(PSC_Event *self, void *sender) CMETHOD;
void PSC_Event_destroyStatic(PSC_Event *self);

#endif
