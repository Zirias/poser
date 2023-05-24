#ifndef POSER_CORE_INT_EVENT_H
#define POSER_CORE_INT_EVENT_H

#include <poser/core/event.h>

#include <stddef.h>

C_CLASS_DECL(EvHandler);

struct PSC_Event
{
    void *sender;
    EvHandler *handlers;
    size_t size;
    size_t capa;
    int dirty;
};

#endif
