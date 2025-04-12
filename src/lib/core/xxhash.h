#ifndef POSER_CORE_INT_XXHASH_H
#define POSER_CORE_INT_XXHASH_H

#ifdef POSER_XXH_IMPL

#  define XXH_NO_STDLIB
#  define XXH_NO_STREAM

#else

#  include "contrib/xxHash/xxhash.h"
#  ifdef HAVE_XXHX86
#    include "contrib/xxHash/xxh_x86dispatch.h"
#  endif

#endif

#endif
