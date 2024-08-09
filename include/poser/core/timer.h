#ifndef POSER_CORE_TIMER_H
#define POSER_CORE_TIMER_H

#include <poser/decl.h>

#include <stddef.h>

/** declarations for the PSC_Timer class
 * @file
 */

/** A timer.
 * @class PSC_Timer timer.h <poser/core/timer.h>
 */
C_CLASS_DECL(PSC_Timer);

C_CLASS_DECL(PSC_Event);

/** PSC_Timer default constructor.
 * Creates a new PSC_Timer
 * @memberof PSC_Timer
 * @returns a newly created PSC_Timer
 */
DECLEXPORT PSC_Timer *
PSC_Timer_create(void)
    ATTR_RETNONNULL;

/** The timer expired.
 * This event is fired on each expiry of the timer.
 * @memberof PSC_Timer
 * @param self the PSC_Timer
 * @returns the expired event
 */
DECLEXPORT PSC_Event *
PSC_Timer_expired(PSC_Timer *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Set expiry in milliseconds.
 * Sets the expiry time in milliseconds. If the timer is currently
 * running, it is first stopped and then restarted with the new value.
 * The initial value is 1000 milliseconds (1 second).
 * @memberof PSC_Timer
 * @param self the PSC_Timer
 * @param ms expiry in milliseconds
 */
DECLEXPORT void
PSC_Timer_setMs(PSC_Timer *self, unsigned ms)
    CMETHOD;

/** Start the timer.
 * Starts the timer. An expired event will be fired after the configured.
 * If the timer is already running, stops and restarts it.
 * expiry time.
 * @memberof PSC_Timer
 * @param self the PSC_Timer
 * @param periodic if nonzero, expire periodically until explicitly stopped.
 */
DECLEXPORT void
PSC_Timer_start(PSC_Timer *self, int periodic)
    CMETHOD;

/** Stop the timer.
 * Stops the timer. No further expired events will occur until
 * PSC_Timer_start() is called again.
 * @memberof PSC_Timer
 * @param self the PSC_Timer
 */
DECLEXPORT void
PSC_Timer_stop(PSC_Timer *self)
    CMETHOD;

/** PSC_Timer destructor.
 * Destroys the timer. If it is currently running, it is stopped first.
 * @memberof PSC_Timer
 * @param self the PSC_Timer
 */
DECLEXPORT void
PSC_Timer_destroy(PSC_Timer *self);

#endif

