#ifdef HAVE_XXHX86
#include "contrib/xxHash/xxh_x86dispatch.c"
#else
#include <poser/decl.h>
SOLOCAL char psc__dummy;
#endif
