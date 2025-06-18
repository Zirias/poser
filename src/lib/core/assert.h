#ifndef POSER_CORE_INT_ASSERT_H
#define POSER_CORE_INT_ASSERT_H

#if defined(HAVE_BTINT) || defined(HAVE_BTSZT)
#  ifdef NDEBUG
#    define assert(x)
#  else
#    include <execinfo.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <unistd.h>
#    define assert(x) do { \
  void *posercore_assert_stack[128]; \
  if (!(x)) { \
    fprintf(stderr, "Assertion failed: (" #x "), function %s, file " \
	    __FILE__ ", line %u.\n", __func__, __LINE__); \
    int posercore_assert_stacksz = backtrace(posercore_assert_stack, 128); \
    backtrace_symbols_fd(posercore_assert_stack, posercore_assert_stacksz, \
	    STDERR_FILENO); \
    abort(); \
  } \
} while (0)
#  endif
#else
#  include <assert.h>
#endif

#endif
