#ifndef POSER_CORE_CERTINFO_H
#define POSER_CORE_CERTINFO_H

/** declarations for the PSC_CertInfo class
 * @file
 */
#include <poser/decl.h>
#include <stdint.h>

/** Metadata of an X.509 certificate.
 * This class holds some information about a certificate, used for custom
 * validation logic.
 * @class PSC_CertInfo certinfo.h <poser/core/certinfo.h>
 */
C_CLASS_DECL(PSC_CertInfo);

/** Custom certificate validator.
 * This can be used for custom logic whether a specific certificate should
 * be accepted or not.
 * @param receiver the object handling the validation
 * @param info metadata of the certificate to check
 * @returns 1 if acceptable, 0 if not
 */
typedef int (*PSC_CertValidator)(void *receiver, const PSC_CertInfo *info);

/** Fingerprint of the certificate.
 * The fingerprint of the certificate in SHA512 format. The size will be 64
 * bytes.
 * @memberof PSC_CertInfo
 * @param self the PSC_CertInfo
 * @returns pointer to the fingerprint
 */
DECLEXPORT const uint8_t *
PSC_CertInfo_fingerprint(const PSC_CertInfo *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Fingerprint string of the certificate.
 * The fingerprint of the certificate in SHA512 format as a hex str.
 * @memberof PSC_CertInfo
 * @param self the PSC_CertInfo
 * @returns the fingerprint as string
 */
DECLEXPORT const char *
PSC_CertInfo_fingerprintStr(const PSC_CertInfo *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

/** Subject name of the certificate.
 * The certificate's subject name. Warning: Don't use this for your custom
 * validation logic unless you also validate the issuing CA, otherwise it
 * would be inherently insecure!
 * @memberof PSC_CertInfo
 * @param self the PSC_CertInfo
 * @returns the subject name
 */
DECLEXPORT const char *
PSC_CertInfo_subject(const PSC_CertInfo *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

#endif
