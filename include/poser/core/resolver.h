#ifndef POSER_CORE_RESOLVER_H
#define POSER_CORE_RESOLVER_H

/** declarations for the PSC_Resolver class
 * @file
 */

#include <poser/decl.h>

/** A resolver to do a batch of reverse DNS lookups.
 * The resolver allows adding PSC_IpAddr instances and offers a method to
 * do reverse lookups for all of them in a batch, preferably using a thread
 * job.
 * @class PSC_Resolver resolver.h <poser/core/resolver.h>
 */
C_CLASS_DECL(PSC_Resolver);

/** A single entry for the PSC_Resolver.
 * This just holds a PSC_IpAddr and, if a successful reverse lookup was done,
 * a corresponding name.
 * @class PSC_ResolverEntry resolver.h <poser/core/resolver.h>
 */
C_CLASS_DECL(PSC_ResolverEntry);

C_CLASS_DECL(PSC_Event);
C_CLASS_DECL(PSC_IpAddr);
C_CLASS_DECL(PSC_List);

/** PSC_Resolver default constructor.
 * Creates a new PSC_Resolver
 * @memberof PSC_Resolver
 * @returns a newly created PSC_Resolver
 */
DECLEXPORT PSC_Resolver *
PSC_Resolver_create(void)
    ATTR_RETNONNULL;

/** Add an address.
 * Add an address to the list of addresses to be resolved. Fails if a resolver
 * job is already running.
 * @memberof PSC_Resolver
 * @param self the PSC_Resolver
 * @param addr the address to add
 * @returns 0 on success, -1 on error
 */
DECLEXPORT int
PSC_Resolver_addAddr(PSC_Resolver *self, const PSC_IpAddr *addr)
    CMETHOD ATTR_NONNULL((2));

/** Start resolving.
 * Starts batch resolving, prefers to do this on a thread job, but does it
 * synchronously if the thread pool is not available or the job can't be
 * enqueued. Fails if a resolver job is already running or there are no
 * addresses added to the resolver.
 * @memberof PSC_Resolver
 * @param self the PSC_Resolver
 * @param forceAsync if set to 1, fails when resolving can't be done on a
 *                   thread job
 * @returns 0 on success, -1 on error
 */
DECLEXPORT int
PSC_Resolver_resolve(PSC_Resolver *self, int forceAsync)
    CMETHOD;

/** Resolving finished.
 * This event fires when resolving is completed. Register at least one handler
 * for this event before calling PSC_Resolver_resolve().
 * @memberof PSC_Resolver
 * @param self the PSC_Resolver
 * @returns the done event
 */
DECLEXPORT PSC_Event *
PSC_Resolver_done(PSC_Resolver *self)
    CMETHOD ATTR_PURE ATTR_RETNONNULL;

/** List of addresses and resolved names.
 * This list contains PSC_ResolverEntry instances for every PSC_IpAddr added,
 * with the corresponding names if successfully resolved.
 * @memberof PSC_Resolver
 * @param self the PSC_Resolver
 * @returns a list of PSC_ResolveEntry
 */
DECLEXPORT const PSC_List *
PSC_Resolver_entries(const PSC_Resolver *self)
    CMETHOD ATTR_PURE ATTR_RETNONNULL;

/** PSC_Resolver destructor.
 * Destroys the PSC_Resolver instance. If a resolve job is currently running,
 * this job is canceled and destructions only completes after the canceled
 * job actually stopped. Avoid doing this, it can't be 100% reliable.
 * @memberof PSC_Resolver
 * @param self the PSC_Resolver
 */
DECLEXPORT void
PSC_Resolver_destroy(PSC_Resolver *self);

/** The IP address of the entry.
 * @memberof PSC_ResolverEntry
 * @param self the PSC_ResolverEntry
 * @returns the PSC_IpAddr originally added to the PSC_Resolver
 */
DECLEXPORT const PSC_IpAddr *
PSC_ResolverEntry_addr(const PSC_ResolverEntry *self)
    CMETHOD ATTR_PURE ATTR_RETNONNULL;

/** The resolved name of the entry.
 * @memberof PSC_ResolverEntry
 * @param self the PSC_ResolverEntry
 * @returns the resolved name for an IP address, or NULL if resolving didn't
 *          succeed or no resolving was done yet
 */
DECLEXPORT const char *
PSC_ResolverEntry_name(const PSC_ResolverEntry *self)
    CMETHOD ATTR_PURE;

#endif
