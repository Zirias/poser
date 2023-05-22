#ifndef POSER_CORE_EVENT_H
#define POSER_CORE_EVENT_H

#include <poser/decl.h>

typedef void (*PSC_EventHandler)(void *receiver, void *sender, void *args);

C_CLASS_DECL(PSC_Event);

DECLEXPORT PSC_Event *
PSC_Event_create(void *sender)
    ATTR_RETNONNULL;

DECLEXPORT void
PSC_Event_register(PSC_Event *self, void *receiver,
	PSC_EventHandler handler, int id)
    CMETHOD ATTR_NONNULL((3));

DECLEXPORT void
PSC_Event_unregister(PSC_Event *self, void *receiver,
	PSC_EventHandler handler, int id)
    CMETHOD ATTR_NONNULL((3));

DECLEXPORT void
PSC_Event_raise(PSC_Event *self, int id, void *args)
    CMETHOD;

DECLEXPORT void
PSC_Event_destroy(PSC_Event *self);

#endif
