#ifndef __LINUX_SPRINTF_H
#define __LINUX_SPRINTF_H

/* _vsnprintf lives in Xeneva's own header rather than being restated here:
 * _print.c is what implements it, and <Log/_print.h> also pulls <stdarg.h>
 * for the va_list the declaration needs.  sprintf.h's own note about being
 * va_list-free (see below) describes the *macros* in this file, not this
 * declaration. */
#include <Log/_print.h>

/*
 * DCL <linux/sprintf.h> -- sprintf() and friends.
 *
 * mainline has this as a header of its own and reaches it from
 * <linux/kernel.h> (`#include <linux/sprintf.h>`), which is why a ported
 * source never names it: virtio-rng.c:165 writes
 *
 *     sprintf(vi->name, "virtio_rng.%d", index);
 *
 * while including only its own driver headers. Left undeclared the call is
 * implicitly `int sprintf()`, and because sprintf's real return type *is*
 * int the result links cleanly -- the compiler has assumed int for every
 * argument too, so a wrong pointer type at a future call site would be
 * accepted rather than caught. The declaration exists so the arguments are
 * checked against something.
 *
 * DCL maps sprintf() onto Xeneva's _sprintf() rather than providing a body.
 *
 * KernelAA64/Log/_print.c:827 is
 *
 *     int _sprintf(char* output, const char* format, ...);
 *
 * -- identical in shape to mainline's `int sprintf(char *buf, const char
 * *fmt, ...)` -- and it is already in the image, already exported to loaded
 * modules through KernelAA64/kernel_exports.c (which is how the prebuilt
 * virtio_rng.ko reached it). A wrapper would be a second copy of the same
 * call with a va_list hand-rolled around it, buying only the name. The name
 * is what this header is for.
 *
 * A macro rather than a static inline: the compiler sees one declaration
 * with the real signature and checks the call site against it, and no
 * forwarding function has to redeclare the varargs itself.
 *
 * Worth knowing before a ported file leans on it: _sprintf runs through a
 * fixed 192-byte va_list scratch buffer and a MAX_STRING_LENGTH output cap
 * in _print.c. It is a boot-time debug formatter that happens to be correct
 * for the short strings drivers use to name devices -- it bounds the output
 * by MAX_STRING_LENGTH, not by the size of the caller's buffer.
 *
 * snprintf() is here now, because sysfs_emit() asked for it (the first ported
 * file to need a *bounded* format: rx_trig_bytes_show writes "%d\n" into a
 * page-sized sysfs buffer).  Xeneva already had the body -- _snprintf(output,
 * sz, format, ...) in <Log/_print.h>, same shape as mainline's, bound by both
 * the caller's size and _print.c's MAX_STRING_LENGTH -- so this is a
 * declaration and a spelling, with no new formatter behind it.  vsnprintf and
 * vscnprintf are still absent: no caller yet, and each would need
 * <linux/stdarg.h> plumbing that the one user did not.  vsnprintf is here
 * now, because seq_buf.c:74 needs it: `len = vsnprintf(s->buffer + s->len,
 * s->size - s->len, fmt, args);` is a *bounded* format into a caller-owned
 * buffer, which is exactly what <Log/_print.h>'s _vsnprintf already does --
 * the two signatures are the same three arguments plus a va_list, in the same
 * order, so this is the same decision as snprintf() above: a spelling, with
 * no second formatter behind it.  vscnprintf is still absent, which is
 * consistent -- nothing has asked for it.
 */
extern int _sprintf(char* output, const char* format, ...);
#define sprintf _sprintf


extern int _snprintf(char* output, size_t sz, const char* format, ...);
#define snprintf _snprintf

/*
 * A macro rather than a static inline, for the same reason the comment at the
 * top gives for sprintf(): the compiler checks the call site against
 * _vsnprintf's real signature, and no forwarding function has to restate the
 * va_list parameter.
 */
#define vsnprintf _vsnprintf
#endif /* __LINUX_SPRINTF_H */
