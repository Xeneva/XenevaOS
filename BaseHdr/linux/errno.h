#ifndef __LINUX_ERRNO_H__
#define __LINUX_ERRNO_H__

/*
 * DCL <linux/errno.h> -- the numbers a ported file returns.
 *
 * <linux/kernel.h> owns the bulk of the set (EPERM through the socket
 * errnos), and it is kept as the owner rather than split across two headers
 * that could drift apart. Everything below is here because a file in the
 * staged tty/serial set asks for it and kernel.h did not have it:
 *
 *   EBADF / EINTR / ENOTTY     -- tty_io.c's read/write/ioctl error paths
 *   ENOIOCTLCMD                -- the tty layer's "this ldisc does not do
 *                                 ioctls" answer (mainline: 515)
 *   EINPROGRESS                -- n_tty's poll path
 *   EFAULT / EACCES / EPIPE ...-- the usual open/write failures
 *
 * All guarded, so <linux/mm.h> (which also defines EFAULT and EFBIG) and
 * kernel.h can be included in any order without a redefinition.
 *
 * ERESTARTSYS lives in kernel.h's errno block -- 512, matching mm.h -- because
 * serial_core.c:1144's scoped_cond_guard() needs it from <linux/mutex.h>
 * alone.
 *
 * The IS_ERR/ERR_PTR family is *not* here; it is in <linux/err.h>, because
 * mainline splits them that way and a ported file includes <linux/err.h> by
 * name.
 */

/*
 * The two #includes are at the *bottom* of this file, below the table, and
 * that is load-bearing rather than a style preference.
 *
 * drm_rect.c:24 includes <linux/errno.h> as its first include. This header's
 * guard is set at line 2, and the includes used to follow at line 30 -- which
 * meant <linux/kernel.h> was pulled in from *inside* this file's own pass.
 * kernel.h's subtree runs on through virtio.h to <linux/kstrtox.h> (kernel.h:
 * 234), and kstrtox.h:31 includes <linux/errno.h> in its turn, finds the
 * guard already set, and takes the no-op. So kstrtox.h is parsed here, before
 * this file's line 76 has run, and its `return -ERANGE;` at :69 reports
 * "use of undeclared identifier 'ERANGE'".
 *
 * Nothing is #undef'd and the macro is defined by the time the TU finishes --
 * it is simply not defined yet at the one moment it is read. Moving the
 * includes below the table closes the loop without changing a single number:
 * every entry is a literal, and the #ifndef guards around them are what make
 * the table reconcilable with kernel.h's set in either order, so nothing in
 * it depends on the includes having happened first.
 *
 * One entry does name a symbol from the other header -- EWOULDBLOCK is
 * EAGAIN, which is kernel.h's -- and it is still safe below, because an
 * object-like macro is expanded where it is *used*, not where it is defined:
 * kernel.h has finished by the time any source can read EWOULDBLOCK.
 */
#ifndef EINTR
#define EINTR		4
#endif
#ifndef EBADF
#define EBADF		9
#endif
#ifndef ECHILD
#define ECHILD		10
#endif
#ifndef ESRCH
#define ESRCH		3
#endif
#ifndef EACCES
#define EACCES		13
#endif
#ifndef EFAULT
#define EFAULT		14
#endif
#ifndef ENOTBLK
#define ENOTBLK		15
#endif
#ifndef EEXIST
#define EEXIST		17
#endif
#ifndef EXDEV
#define EXDEV		18
#endif
#ifndef ENOTTY
#define ENOTTY		25
#endif
#ifndef ESPIPE
#define ESPIPE		29
#endif
#ifndef EROFS
#define EROFS		30
#endif
#ifndef EPIPE
#define EPIPE		32
#endif
#ifndef EMLINK
#define EMLINK		31
#endif
#ifndef ERANGE
#define ERANGE		34
#endif
#ifndef EINPROGRESS
#define EINPROGRESS	115
#endif
#ifndef EALREADY
#define EALREADY	114
#endif
#ifndef ENOTCONN
#define ENOTCONN	107
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT	110
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP	95
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK	EAGAIN
#endif

/*
 * ENOIOCTLCMD is a tty-layer convention rather than a POSIX errno: the ldisc
 * answers it to mean "I do not implement this ioctl", and tty_ioctl() then
 * hands the number on to the driver. It has to sit outside the errno table
 * proper (mainline puts it at 515, past MAX_ERRNO's useful range is fine
 * since it never crosses the IS_ERR boundary as a real error).
 */
#ifndef ENOIOCTLCMD
#define ENOIOCTLCMD	515
#endif

/*
 * The table is above and the headers it reconciles with are below: kernel.h
 * owns EPERM through the socket errnos, err.h the IS_ERR/ERR_PTR family.
 * The reason they come after rather than before is at the top of the file.
 */
#include <linux/kernel.h>	/* EPERM, EIO, EAGAIN, EINVAL, ... */
#include <linux/err.h>

#endif /* __LINUX_ERRNO_H__ */
