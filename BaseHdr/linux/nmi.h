#ifndef __LINUX_NMI_H__
#define __LINUX_NMI_H__

/*
 * DCL <linux/nmi.h> -- one function: touch_nmi_watchdog().
 *
 * mainline's header is the whole NMI watchdog vocabulary: the perf-based
 * hardlockup/softlockup detectors, watchdog_touch(), the cpumask plumbing.
 * Xeneva has no NMI interrupt and no watchdog thread, so none of it can be
 * anything but a lie, and there is exactly one caller in the tree --
 * serial8250_console_write() at 8250_port.c:3336, which pokes it before
 * taking the port lock so a console write cannot itself trip a lockup
 * detector.
 *
 * An empty body is the correct answer rather than a stub pretending to work:
 * with no detector, "kick the watchdog" has no effect *and* nothing to miss.
 * When an iMX8MP bring-up grows a real watchdog, this is the header to fill
 * in -- the call site is already in the right place.
 */
static inline void touch_nmi_watchdog(void)
{
}

#endif /* __LINUX_NMI_H__ */
