#ifndef POSER_CORE_RATELIMIT_H
#define POSER_CORE_RATELIMIT_H

/** Declarations for the PSC_RateLimit class
 * @file
 */

#include <poser/decl.h>

#include <stddef.h>

/** A simple rate limiter for actions identified by a key.
 * This class provides configurable rate-limiting with a set of individual
 * limits. Each limit consists of a number of seconds and a maximum number of
 * actions allowed within this time period.
 *
 * For counting, the time period is divided into equally sized slots with a
 * resolution of whole seconds. Up to 512 of these counting slots are created
 * per limit. So, the counting precision depends on the length of the time
 * period; the number of slots will be > 256 for each time period > 256
 * seconds.
 *
 * For an action to be allowed by the rate limiter, it must be allowed by all
 * the limits. Only those limits that allow the action will also count it.
 * @class PSC_RateLimit ratelimit.h <poser/core/ratelimit.h>
 */
C_CLASS_DECL(PSC_RateLimit);

/** Options for creating a new rate limiter.
 * These options are used to create a new PSC_RateLimit instance.
 * @class PSC_RateLimitOpts ratelimit.h <poser/core/ratelimit.h>
 */
C_CLASS_DECL(PSC_RateLimitOpts);

/** PSC_RateLimit default constructor.
 * Creates a new PSC_RateLimit
 * @memberof PSC_RateLimit
 * @param opts rate limit options
 * @returns a newly created PSC_RateLimit
 */
PSC_RateLimit *
PSC_RateLimit_create(const PSC_RateLimitOpts *opts)
    ATTR_RETNONNULL;

/** Check the limits for a given key and count.
 * Checks whether the action identified by the given key is at the current
 * time allowed according to all configured limits. For those limits that
 * allow it, it's also counted.
 * @memberof PSC_RateLimit
 * @param self the PSC_RateLimit
 * @param key identifier of the action
 * @param keysz size of the identifier
 * @returns 1 if the action is currently allowed, 0 otherwise
 */
int
PSC_RateLimit_check(PSC_RateLimit *self, const void *key, size_t keysz)
    CMETHOD ATTR_NONNULL((1));

/** PSC_RateLimit destructor.
 * @memberof PSC_RateLimit
 * @param self the PSC_RateLimit
 */
void
PSC_RateLimit_destroy(PSC_RateLimit *self);

/** PSC_RateLimitOpts default constructor.
 * Creates a new PSC_RateLimitOpts
 * @memberof PSC_RateLimitOpts
 * @returns a newly created PSC_RateLimitOpts
 */
PSC_RateLimitOpts *
PSC_RateLimitOpts_create(void)
    ATTR_RETNONNULL;

/** Add an individual limit.
 * Configures a limit to allow at most @p limit actions in @p seconds seconds.
 * @memberof PSC_RateLimitOpts
 * @param self the PSC_RateLimitOpts
 * @param seconds number of seconds (min: 1, max: 65535)
 * @param limit number of actions (min: 1, max: 65535)
 * @returns 0 on success, -1 on error
 */
int
PSC_RateLimitOpts_addLimit(PSC_RateLimitOpts *self, int seconds, int limit)
    CMETHOD;

/** Compare rate limit options to another instance.
 * @memberof PSC_RateLimitOpts
 * @param self the PSC_RateLimitOpts
 * @param other another PSC_RateLimitOpts instance to compare to
 * @returns 1 if both instances are configured the same, 0 otherwise
 */
int
PSC_RateLimitOpts_equals(const PSC_RateLimitOpts *self,
	const PSC_RateLimitOpts *other)
    CMETHOD ATTR_NONNULL((2));

/** PSC_RateLimitOpts destructor.
 * @memberof PSC_RateLimitOpts
 * @param self the PSC_RateLimitOpts
 */
void PSC_RateLimitOpts_destroy(PSC_RateLimitOpts *self);

#endif
