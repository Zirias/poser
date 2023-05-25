#ifndef POSER_CORE_EVENT_H
#define POSER_CORE_EVENT_H

/** declarations for the PSC_Event class
 * @file
 */

#include <poser/decl.h>

/** An event handler.
 * @param receiver the object that subscribed to the event
 * @param sender the object that raised the event
 * @param args optional event arguments
 */
typedef void (*PSC_EventHandler)(void *receiver, void *sender, void *args);

/** A simple event class.
 * Events can be raised and subscribed to. The class emitting an event should
 * own it and provide an accessor method for consumers to subscribe to it.
 *
 * When an event is raised, all subscribers are called directly. No mechanism
 * to cross threads is offered, so always use functions/objects wired by
 * events on the same thread.
 * @class PSC_Event event.h <poser/core/event.h>
 */
C_CLASS_DECL(PSC_Event);

/** PSC_Event constructor.
 * @memberof PSC_Event
 * @param sender the object owning the event, or NULL for static classes
 * @returns a newly created event
 */
DECLEXPORT PSC_Event *
PSC_Event_create(void *sender)
    ATTR_RETNONNULL;

/** Register an event handler.
 * @memberof PSC_Event
 * @param self the PSC_Event
 * @param receiver the object that should receive the event
 * @param handler the handler to be called
 * @param id optional identifier to be matched, use 0 if not applicable
 */
DECLEXPORT void
PSC_Event_register(PSC_Event *self, void *receiver,
	PSC_EventHandler handler, int id)
    CMETHOD ATTR_NONNULL((3));

/** Unregister an event handler.
 * When this is called with the exact same parameters as PSC_Event_register(),
 * it removes the event handler from the event again.
 * @memberof PSC_Event
 * @param self the PSC_Event
 * @param receiver the object that should receive the event
 * @param handler the handler to be called
 * @param id optional identifier to be matched, use 0 if not applicable
 */
DECLEXPORT void
PSC_Event_unregister(PSC_Event *self, void *receiver,
	PSC_EventHandler handler, int id)
    CMETHOD ATTR_NONNULL((3));

/** Raise an event.
 * Called by the event owner to notify all subscribers that the event occured.
 * Only handlers with the same identifier will be called.
 * @memberof PSC_Event
 * @param self the PSC_Event
 * @param id optional identifier to be matched, use 0 if not applicable
 * @param args optional event args to pass to the handler(s)
 */
DECLEXPORT void
PSC_Event_raise(PSC_Event *self, int id, void *args)
    CMETHOD;

/** PSC_Event destructor.
 * @memberof PSC_Event
 * @param self the PSC_Event
 */
DECLEXPORT void
PSC_Event_destroy(PSC_Event *self);

#endif
