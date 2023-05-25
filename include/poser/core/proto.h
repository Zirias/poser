#ifndef POSER_CORE_PROTO_H
#define POSER_CORE_PROTO_H

/** declaration of the PSC_Proto enum
 * @file
 */

/** Protocol to use for TCP connections */
typedef enum PSC_Proto
{
    PSC_P_ANY,	    /**< use both IPv4 and IPv6 as available */
    PSC_P_IPv4,	    /**< use only IPv4 */
    PSC_P_IPv6	    /**< use only IPv6 */
} PSC_Proto;

#endif
