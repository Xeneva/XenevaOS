#ifndef __LINUX_DELAY_H__
#define __LINUX_DELAY_H__

/*
 * DCL <linux/delay.h> -- this header exists so mainline's
 * `#include <linux/delay.h>` resolves; the primitives themselves already live
 * in <linux/kernel.h>:
 *
 *     #define udelay(us) AA64SleepUS(us)
 *     #define mdelay(ms) AA64SleepMS(ms)
 *     #define msleep(ms) mdelay(ms)
 *
 * and both land on the generic timer (KernelAA64/Hal/aa64cpu.c:232). That
 * matters more than it looks: 8250_port.c waits 1-20us on THRE/RI transitions
 * inside polling loops (lines 1243, 1988, 2194, 2198), so a millisecond floor
 * under udelay() would turn wait_for_xmitr() into a ~1000x crawl and put the
 * "send a byte" path far past its timeout. A real microsecond sleep is the
 * difference between an 8250 driver that works and one that appears to hang.
 *
 * The three variants below are the rest of mainline's set. None of the nine
 * serial/tty files uses them today; they are here so the next driver that
 * does does not have to add a header to compile.
 */

#include <linux/kernel.h>	/* udelay, mdelay, msleep */

#define ndelay(ns) udelay(((unsigned long)(ns) + 999UL) / 1000UL)

#define ssleep(sec) mdelay((unsigned long)(sec) * 1000UL)

/*
 * usleep_range() is a latency hint in mainline: sleeping anywhere between min
 * and max is correct, and >= min is the contract that matters. DCL honours it
 * exactly by sleeping min -- the window exists to let a coalescing timer cut
 * power, and there is no timer wheel to coalesce into.
 */
static inline void usleep_range(unsigned long min, unsigned long max) {
	(void)max;
	udelay((unsigned int)min);
}

/*
 * "Interruptible" only means "may return early on a signal". Nothing here
 * sleeps on a signal, so it never does -- returning 0 says so honestly rather
 * than pretending a wait was cut short.
 */
static inline unsigned int msleep_interruptible(unsigned int ms) {
	mdelay(ms);
	return 0;
}

#endif /* __LINUX_DELAY_H__ */
