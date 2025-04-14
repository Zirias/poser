#ifdef HAVE_XXHX86
#pragma GCC visibility push (hidden)
#include "contrib/xxHash/xxh_x86dispatch.c"
#pragma GCC visibility pop
#else
#include <poser/decl.h>
SOLOCAL char psc__dummy;
#endif
