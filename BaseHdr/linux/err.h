#ifndef __LINUX_ERR_H__
#define __LINUX_ERR_H__

/*
 * DCL <linux/err.h> -- the IS_ERR/ERR_PTR family.
 *
 * These are load-bearing for tty_io.c, which is written in the style of "call,
 * then ask whether the answer was a pointer or an error":
 *
 *     tty = tty_open_by_driver(device, filp);
 *     if (IS_ERR(tty)) {
 *         retval = PTR_ERR(tty);
 *         if (retval != -EAGAIN || signal_pending(current))
 *             return retval;
 *     }
 *
 * The trick is that a 4096-entry errno space sits at the very top of the
 * address space, so any value in -1..-4095 can be passed through a void*
 * without ever being dereferenced -- IS_ERR() is a range check, not a NULL
 * check. <linux/kernel.h> already carries IS_ERR_VALUE() (kernel.h:122); what
 * was missing was everything built on it, which is why a ported file that only
 * ever says `IS_ERR(p)` failed to compile before this header existed.
 *
 * ERR_CAST() and ERR_OR_PTR() are included because tty_io.c and serial_core.c
 * both use them to move an error across a function boundary without
 * converting it to a long and back.
 */

#include <linux/kernel.h>	/* MAX_ERRNO, IS_ERR_VALUE, uintptr_t */

/*
 * <linux/fs.h> is pulled in here, deliberately, and the reason is not visible
 * from either file on its own. fs.h defines ERR_PTR/PTR_ERR/IS_ERR *without*
 * #ifndef guards (fs.h:173-175) because the helper they are built on --
 * dcl_is_err() -- is a static inline that lives there. err.h was written to
 * provide the same three, also guarded, which looked harmless and was not: a
 * translation unit that reached err.h first got one spelling, and then a
 * -Wmacro-redefined the moment fs.h arrived with the other. DCL/tty_buffer.c
 * does exactly that -- <linux/errno.h> at line 7, <linux/tty.h> (-> fs.h) at
 * line 9 -- and the two spellings are close enough to be a trap:
 * `((void*)(long)(err))` vs `((void*)((long)(err)))`, and
 * `IS_ERR_VALUE((unsigned long)(ptr))` vs `dcl_is_err(ptr)`.
 *
 * Including fs.h first makes fs.h's definitions the ones that land and the
 * guarded ones below skip. One definition of IS_ERR in the tree instead of
 * two that agree today.
 */
#include <linux/fs.h>

#ifndef ERR_PTR
#define ERR_PTR(err) ((void*)(long)(err))
#endif

#ifndef PTR_ERR
#define PTR_ERR(ptr) ((long)(ptr))
#endif

#ifndef IS_ERR
#define IS_ERR(ptr) IS_ERR_VALUE((unsigned long)(ptr))
#endif

#ifndef IS_ERR_OR_NULL
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR(ptr))
#endif

#ifndef IS_ERR_OR_ZERO
#define IS_ERR_OR_ZERO(ptr) (IS_ERR(ptr) ? PTR_ERR(ptr) : 0)
#endif

#ifndef ERR_CAST
#define ERR_CAST(ptr) ((void*)(ptr))
#endif

#ifndef PTR_ERR_OR_NULL
#define PTR_ERR_OR_NULL(ptr) (IS_ERR(ptr) ? PTR_ERR(ptr) : 0)
#endif

#endif /* __LINUX_ERR_H__ */
