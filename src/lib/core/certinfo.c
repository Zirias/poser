#include "certinfo.h"

#ifdef WITH_TLS
#include <openssl/evp.h>
#include <poser/core/util.h>
#include <stdlib.h>
#include <string.h>

struct PSC_CertInfo
{
    X509 *cert;
    char *subject;
    char *fpstr;
    int havefingerprint;
    uint8_t fingerprint[64];
};

SOLOCAL PSC_CertInfo *PSC_CertInfo_create(X509 *cert)
{
    PSC_CertInfo *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->cert = cert;
    return self;
}

SOEXPORT const uint8_t *PSC_CertInfo_fingerprint(const PSC_CertInfo *self)
{
    if (!self->havefingerprint)
    {
	PSC_CertInfo *mut = (PSC_CertInfo *)self;
	X509_digest(self->cert, EVP_sha512(), mut->fingerprint, 0);
	mut->havefingerprint = 1;
    }
    return self->fingerprint;
}

SOEXPORT const char *PSC_CertInfo_fingerprintStr(const PSC_CertInfo *self)
{
    if (!self->fpstr)
    {
	PSC_CertInfo *mut = (PSC_CertInfo *)self;
	mut->fpstr = PSC_malloc(129);
	char *fppos = mut->fpstr;
	const uint8_t *fp = PSC_CertInfo_fingerprint(self);
	for (int i = 0; i < 64; ++i)
	{
	    sprintf(fppos, "%02hhx", fp[i]);
	    fppos += 2;
	}
    }
    return self->fpstr;
}

SOEXPORT const char *PSC_CertInfo_subject(const PSC_CertInfo *self)
{
    if (!self->subject)
    {
	PSC_CertInfo *mut = (PSC_CertInfo *)self;
	mut->subject = X509_NAME_oneline(
		X509_get_subject_name(self->cert), 0, 0);
    }
    return self->subject;
}

SOLOCAL void PSC_CertInfo_destroy(PSC_CertInfo *self)
{
    if (!self) return;
    free(self->subject);
    free(self->fpstr);
    free(self);
}

#else
#include <poser/core/service.h>

SOEXPORT const uint8_t *PSC_CertInfo_fingerprint(const PSC_CertInfo *self)
{
    (void)self;
    PSC_Service_panic("This version of libposercore does not support TLS!");
}

SOEXPORT const char *PSC_CertInfo_subject(const PSC_CertInfo *self)
{
    (void)self;
    PSC_Service_panic("This version of libposercore does not support TLS!");
}
#endif
