/*
 * DCL sys/ioccom.h -- BSD-side ioctl encoding for uapi <drm/drm.h>.
 *
 * drm.h's __KERNEL__/__linux__ branches are both off for this tree's
 * targets, so it takes the BSD branch and includes this name.  Route it to
 * <asm/ioctl.h> (uapi asm-generic, byte-identical): FreeBSD's _IOC layout
 * for _IOWR is bit-compatible with Linux's anyway, and one definition keeps
 * kernel, dcltest, and the DRM ioctl table computing the same numbers.
 */
#ifndef _DCL_SYS_IOCCOM_H
#define _DCL_SYS_IOCCOM_H

#include <asm/ioctl.h>

#endif /* _DCL_SYS_IOCCOM_H */
