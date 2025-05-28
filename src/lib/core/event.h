#ifndef POSER_CORE_INT_EVENT_H
#define POSER_CORE_INT_EVENT_H

#include <poser/core/event.h>

#include <stddef.h>

C_CLASS_DECL(EvHandlerEntry);
C_CLASS_DECL(PSC_Dictionary);

struct PSC_Event
{
    void *sender;
    EvHandlerEntry *first;
    EvHandlerEntry *last;
    PSC_Dictionary *index;
};

void PSC_Event_destroyStatic(PSC_Event *self);

#endif
