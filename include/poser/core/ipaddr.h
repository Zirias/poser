#ifndef POSER_CORE_IPADDR_H
#define POSER_CORE_IPADDR_H

/** Declarations for the PSC_IpAddr class
 * @file
 */

#include <poser/decl.h>
#include <poser/core/proto.h>

/** An IPv4 or IPv6 address or network.
 * This class holds data for an IP address or network with a prefix length
 * and offers functions to parse a string representation, create a string in
 * the canonical formats, convert between IPv4 and IPv6 and match networks.
 * @class PSC_IpAddr ipaddr.h <poser/core/ipaddr.h>
 */
C_CLASS_DECL(PSC_IpAddr);

/** PSC_IpAddr default constructor.
 * Creates a new PSC_IpAddr from a given string.
 * @memberof PSC_IpAddr
 * @param str the address as a string
 * @returns a newly created PSC_IpAddr, or NULL if the string did not contain
 *          a valid address.
 */
DECLEXPORT PSC_IpAddr *
PSC_IpAddr_create(const char *str)
    ATTR_NONNULL((1));

/** PSC_IpAddr copy constructor.
 * Creates a clone of a PSC_IpAddr.
 * @memberof PSC_IpAddr
 * @param other the PSC_IpAddr to copy
 * @returns a newly created PSC_IpAddr
 */
DECLEXPORT PSC_IpAddr *
PSC_IpAddr_clone(const PSC_IpAddr *other)
    CMETHOD ATTR_RETNONNULL;

/** Create IPv4 equivalent of a given IPv6 address.
 * The given address must be an IPv6 address with a prefix length of at least
 * 96, which is then matched against a given list of prefixes. These prefixes
 * must be IPv6 addresses with a prefix length of exactly 96.
 *
 * If one of them matches, a new IPv4 address is created from only the last 4
 * bytes and with 96 subtracted from the original prefix length.
 *
 * This can be used to map back IPv6 addresses obtained from NAT64.
 * @memberof PSC_IpAddr
 * @param self the PSC_IpAddr
 * @param prefixes an array of prefixes, must contain at least one entry and
 *                 must be terminated with NULL
 * @returns a newly created PSC_IpAddr holding the Ipv4 equivalent, or NULL
 */
DECLEXPORT PSC_IpAddr *
PSC_IpAddr_tov4(const PSC_IpAddr *self, const PSC_IpAddr **prefixes)
    CMETHOD ATTR_NONNULL((2));

/** Create IPv6 equivalent of a given IPv4 address.
 * The given address must be an IPv4 address, the prefix must be an IPv6
 * address with a prefix length of exactly 96. Then, a new Ipv6 address is
 * returned, constructed from the first 12 bytes of the prefix, followed by
 * the original 4 bytes of the IPv4 address, and 96 added to the original
 * prefix length.
 *
 * This can be used to map IPv4 addresses for NAT64.
 * @memberof PSC_IpAddr
 * @param self the PSC_IpAddr
 * @param prefix the prefix to use for the mapping to IPv6
 * @returns a newly created PSC_IpAddr holding the IPv6 equivalent, or NULL
 */
DECLEXPORT PSC_IpAddr *
PSC_IpAddr_tov6(const PSC_IpAddr *self, const PSC_IpAddr *prefix)
    CMETHOD ATTR_NONNULL((1));

/** The protocol of the address.
 * @memberof PSC_IpAddr
 * @param self the PSC_IpAddr
 * @returns the protocol of the address
 */
DECLEXPORT PSC_Proto
PSC_IpAddr_proto(const PSC_IpAddr *self)
    CMETHOD ATTR_PURE;

/** The prefix length of the address.
 * @memberof PSC_IpAddr
 * @param self the PSC_IpAddr
 * @returns the prefix length of the address
 */
DECLEXPORT unsigned
PSC_IpAddr_prefixlen(const PSC_IpAddr *self)
    CMETHOD ATTR_PURE;

/** The canonical string representation of the address.
 * Returns the canonical string representation of the address. If the prefix
 * length is less than the full length in bits (32 for IPv4, 128 for IPv6),
 * it is appended after a slash ('/').
 * @memberof PSC_IpAddr
 * @param self the PSC_IpAddr
 * @returns the string representation of the address
 */
DECLEXPORT const char *
PSC_IpAddr_string(const PSC_IpAddr *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Compare two addresses for equality.
 * @memberof PSC_IpAddr
 * @param self the PSC_IpAddr
 * @param other another PSC_IpAddr
 * @returns 1 if both addresses are equal (protocol, prefix length and data),
 *          0 otherwise.
 */
DECLEXPORT int
PSC_IpAddr_equals(const PSC_IpAddr *self, const PSC_IpAddr *other)
    CMETHOD ATTR_NONNULL((2)) ATTR_PURE;

/** Check whether an address is part of a network.
 * @memberof PSC_IpAddr
 * @param self the PSC_IpAddr
 * @param prefix a PSC_IpAddr describing a network
 * @returns 1 if the address is part of the given network (the prefix length
 *          is at most the same and for the network's prefix length, the bits
 *          of the address are the same), 0 otherwise
 */
DECLEXPORT int
PSC_IpAddr_matches(const PSC_IpAddr *self, const PSC_IpAddr *prefix)
    CMETHOD ATTR_NONNULL((2)) ATTR_PURE;

/** PSC_IpAddr destructor.
 * @memberof PSC_IpAddr
 * @param self the PSC_IpAddr
 */
DECLEXPORT void
PSC_IpAddr_destroy(PSC_IpAddr *self);

#endif
