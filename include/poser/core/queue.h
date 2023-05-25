#ifndef POSER_CORE_QUEUE_H
#define POSER_CORE_QUEUE_H

#include <poser/decl.h>

/** declarations for the PSC_Queue class
 * @file
 */

/** A simple queue of objects
 * @class PSC_Queue queue.h <poser/core/queue.h>
 */
C_CLASS_DECL(PSC_Queue);

/** PSC_Queue default constructor.
 * Creates a new PSC_Queue
 * @memberof PSC_Queue
 * @returns a newly created PSC_Queue
 */
DECLEXPORT PSC_Queue *
PSC_Queue_create(void)
    ATTR_RETNONNULL;

/** Enqueue an object.
 * @memberof PSC_Queue
 * @param self the PSC_Queue
 * @param obj the object to enqueue
 * @param deleter optional function to destroy the object
 */
DECLEXPORT void
PSC_Queue_enqueue(PSC_Queue *self, void *obj, void (*deleter)(void *))
    CMETHOD ATTR_NONNULL((2));

/** Dequeue the oldest object.
 * The object will *not* be destroyed, so it can be used by the caller.
 * @memberof PSC_Queue
 * @param self the PSC_Queue
 * @returns the dequeued object, or NULL if the PSC_Queue was empty
 */
DECLEXPORT void *
PSC_Queue_dequeue(PSC_Queue *self)
    CMETHOD;

/** PSC_Queue destructor.
 * All still queued objects that have a deleter attached are also destroyed.
 * @memberof PSC_Queue
 * @param self the PSC_Queue
 */
DECLEXPORT void
PSC_Queue_destroy(PSC_Queue *self);

#endif
