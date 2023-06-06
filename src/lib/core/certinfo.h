#ifndef POSER_CORE_INT_CERTINFO_H
#define POSER_CORE_INT_CERTINFO_H

#include <poser/core/certinfo.h>

#ifdef WITH_TLS
#include <openssl/x509.h>

PSC_CertInfo *
PSC_CertInfo_create(X509 *cert)
    ATTR_RETNONNULL ATTR_NONNULL((1));

void
PSC_CertInfo_destroy(PSC_CertInfo *self);

#endif

#endif
