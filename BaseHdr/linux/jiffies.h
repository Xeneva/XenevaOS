#ifndef __LINUX_JIFFIES_H__
#define __LINUX_JIFFIES_H__

/*
 * DCL <linux/jiffies.h> -- mainline puts jiffies and the time_after family
 * here; DCL keeps them in <linux/timer.h>, so this header exists to make the
 * include resolve and re-exports them.
 *
 * Worth knowing when reading timer.h: jiffies there is derived from the ARM
 * generic timer rather than counted by a tick, because Xeneva runs no timer
 * IRQ to count it. An `extern jiffies` backed by a variable nothing
 * increments would link fine and then make every `time_after(jiffies,
 * deadline)` false forever -- which reads as a driver that hangs instead of a
 * driver that timed out.
 */

#include <linux/timer.h>

#endif /* __LINUX_JIFFIES_H__ */
