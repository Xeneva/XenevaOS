/*
 * DCL <linux/timekeeping.h> -- the clock header mainline's dma-fence pulls
 * in for ktime_get().  DCL keeps the ktime_t type, its arithmetic and the
 * generic-timer reader in <linux/kernel.h>, and <linux/ktime.h> is the
 * thin include over it, so this header routes rather than redefines: it
 * exists so a mainline source that writes `#include <linux/timekeeping.h>`
 * gets the whole surface in one include.
 */
#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

#include <linux/ktime.h>

#endif /* _LINUX_TIMEKEEPING_H */
