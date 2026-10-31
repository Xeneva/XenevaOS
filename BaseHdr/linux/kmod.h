#ifndef __LINUX_KMOD_H__
#define __LINUX_KMOD_H__

#include <linux/errno.h>	/* ENOSYS, the only answer available */

/*
 * DCL <linux/kmod.h> -- request_module(), the call that means "the code for
 * this is not in the image; go and fetch it".
 *
 * The body is a static inline returning -ENOSYS because that is literally
 * what happens: DCL/module_loader.c runs the initcalls baked into the kernel
 * image and has no module directory, no symbol resolution against a file on
 * disk, and no way to interpret a format string like "tty-ldisc-%d" as a
 * request for anything.  It has to be a *definition* and not a declaration
 * precisely so that this header, included by tty_ldisc.c:4, is all it takes
 * for the call at tty_ldisc.c:154 to link -- a declaration would have moved
 * the failure from compile time to link time and named it something else.
 *
 * The return value is ignored at the only call site, and the caller retries
 * get_ldops() straight afterwards and takes whatever that says, so -ENOSYS
 * never reaches a user: the observable behaviour is "no such discipline",
 * which is the truth.
 *
 * __request_module()/try_then_request_module() are not provided -- nothing
 * staged spells them, and a macro that swallowed a call would hide exactly
 * the search that does not happen here.
 *
 *   upstream  include/linux/kmod.h, __request_module() in kernel/module/main.c
 */
static inline int request_module(const char* fmt, ...)
{
	(void)fmt;
	return -ENOSYS;
}

#endif /* __LINUX_KMOD_H__ */
