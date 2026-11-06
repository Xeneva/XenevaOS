#ifndef __LINUX_SCHED_SIGNAL_H__
#define __LINUX_SCHED_SIGNAL_H__

/*
 * DCL <linux/sched/signal.h> -- the signal half of <linux/sched.h>, which is
 * what mainline splits out here.
 *
 * serial_core.c names two things through it: signal_pending(current) at :1238
 * (inside uart_wait_until_sent, polling for the transmitter to drain) and
 * again at :1802.  Both are the "did the reader go away while we waited"
 * test, and both get DCL's answer -- always 0, because Xeneva has no signal
 * delivery (see the definition in <linux/sched.h> for why that is a statement
 * about the process model rather than a stub).
 *
 * So this header is a re-export, not a re-implementation: including sched.h
 * is exactly what mainline's file does with its own contents, and defining
 * signal_pending here instead would risk the *two* definitions that
 * <linux/sched.h>'s `#ifndef signal_pending` guard exists to prevent.
 */

#include <linux/sched.h>

#endif /* __LINUX_SCHED_SIGNAL_H__ */
