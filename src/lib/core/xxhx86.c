#ifdef HAVE_XXHX86
#  ifdef NDEBUG
#    pragma GCC visibility push (hidden)
#    define POSER_XXHX86_HIDDEN
#  endif
#  include "contrib/xxHash/xxh_x86dispatch.c"
#  ifdef POSER_XXHX86_HIDDEN
#    pragma GCC visibility pop
#  endif
#else
#  include <poser/decl.h>
SOLOCAL char psc__dummy;
#endif
