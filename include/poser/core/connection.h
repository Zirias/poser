#ifndef POSER_CORE_CONNECTION_H
#define POSER_CORE_CONNECTION_H

#include <poser/decl.h>

#include <stddef.h>
#include <stdint.h>

C_CLASS_DECL(PSC_Connection);
C_CLASS_DECL(PSC_EADataReceived);
C_CLASS_DECL(PSC_Event);

DECLEXPORT PSC_Event *
PSC_Connection_connected(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Connection_closed(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Connection_dataReceived(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Connection_dataSent(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT PSC_Event *
PSC_Connection_nameResolved(PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT const char *
PSC_Connection_remoteAddr(const PSC_Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;

DECLEXPORT const char *
PSC_Connection_remoteHost(const PSC_Connection *self)
    CMETHOD ATTR_PURE;

DECLEXPORT int
PSC_Connection_remotePort(const PSC_Connection *self)
    CMETHOD ATTR_PURE;

DECLEXPORT int
PSC_Connection_write(PSC_Connection *self,
	const uint8_t *buf, size_t sz, void *id)
    CMETHOD ATTR_NONNULL((2));

DECLEXPORT void
PSC_Connection_activate(PSC_Connection *self)
    CMETHOD;

DECLEXPORT int
PSC_Connection_confirmDataReceived(PSC_Connection *self)
    CMETHOD;

DECLEXPORT void
PSC_Connection_close(PSC_Connection *self, int blacklist)
    CMETHOD;

DECLEXPORT void
PSC_Connection_setData(PSC_Connection *self,
	void *data, void (*deleter)(void *))
    CMETHOD;

DECLEXPORT void *
PSC_Connection_data(const PSC_Connection *self)
    CMETHOD ATTR_PURE;

DECLEXPORT const uint8_t *
PSC_EADataReceived_buf(const PSC_EADataReceived *self)
    CMETHOD ATTR_RETNONNULL;

DECLEXPORT uint16_t
PSC_EADataReceived_size(const PSC_EADataReceived *self)
    CMETHOD;

DECLEXPORT void
PSC_EADataReceived_markHandling(PSC_EADataReceived *self)
    CMETHOD;

#endif
