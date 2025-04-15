#ifdef NDEBUG
#  pragma GCC visibility push (hidden)
#  define POSER_XXHASH_HIDDEN
#endif
#define XXH_NO_STDLIB
#define XXH_NO_STREAM
#include "contrib/xxHash/xxhash.c"
#ifdef POSER_XXHASH_HIDDEN
#  pragma GCC visibility pop
#endif
