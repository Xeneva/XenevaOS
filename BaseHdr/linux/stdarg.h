// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * <linux/stdarg.h> -- a forwarder to the compiler's <stdarg.h>, which is
 * what mainline's is: two includes and a guard, no declarations of its own.
 *
 * This file did not start out that way. It carried its own
 * `typedef __builtin_va_list va_list` plus the four va_* macros spelled over
 * the builtins, and that was one typedef too many. The tree's <stdarg.h>
 * resolves to BaseHdr/stdarg.h, which on this target typedefs va_list as
 * `unsigned char*` and hand-walks it; so drm_print.c, which names both this
 * header at line 33 and (through drm_print.h -> printk.h) <stdarg.h>, came
 * out with "typedef redefinition with different types" and, once it picked a
 * side, a va_start() binding a non-const lvalue reference to an unrelated
 * type. One TU cannot have two va_list types, and this was the second copy.
 *
 * Forwarding is also the only arrangement that can work here, because the
 * two users in this tree disagree about which va_list is right and the
 * disagreement is not this file's to settle -- see the note above the
 * #ifdef __GNUC__ in BaseHdr/stdarg.h. A header that re-decides the type
 * forces the choice on both of them; a header that forwards lets the one
 * place make it.
 *
 * <linux/types.h> is left out, though mainline's forwarder names it. mainline
 * needs it for nothing that this header declares -- there is nothing here but
 * the include -- and in DCL it is the aggregator: types.h:21 is
 * `#include <linux/kernel.h>`, whose subtree reaches <linux/sched.h>, which
 * unconditionally redefines the word `current` as a macro. The one source
 * this header has (drm_print.c) does not need that, and the next one may
 * include <Fs/vfs.h>.
 */
#ifndef _LINUX_STDARG_H
#define _LINUX_STDARG_H

#include <stdarg.h>

#endif
