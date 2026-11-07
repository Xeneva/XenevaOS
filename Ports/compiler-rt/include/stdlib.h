/* Minimal <stdlib.h> for the freestanding compiler-rt build.
 *
 * int_util.c does `#include <stdlib.h>` on _WIN32 for abort().  This build is
 * -ffreestanding, so __STDC_HOSTED__ is zero and int_util.c takes the
 * __builtin_trap() branch instead: nothing here is linked from libc.  clang's
 * resource directory provides stddef.h/stdint.h but has no stdlib.h, and
 * aarch64-unknown-windows has no MSVC SDK on this host, so this header only
 * satisfies the unconditional include.
 */
#ifndef __COMPILER_RT_STDLIB_H__
#define __COMPILER_RT_STDLIB_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void abort(void);

#ifdef __cplusplus
}
#endif

#endif
