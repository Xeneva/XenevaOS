#ifndef __LINUX_KTIME_H__
#define __LINUX_KTIME_H__

/*
 * DCL <linux/ktime.h> -- nanosecond timestamps.
 *
 * Nearly everything a ported file asks of ktime already lives in
 * <linux/kernel.h>: ktime_t, ktime_get() -- which reads the same generic
 * timer udelay() burns against, so timestamps and delays agree instead of
 * coming from two clocks -- and the ktime_add / ktime_sub / ktime_to_ns /
 * ns_to_ktime / ms_to_ktime family.
 *
 * The first draft of this header re-declared all of them anyway, and the
 * compile rejected it as redefinitions the moment both headers were included.
 * That is the whole point of DCL/linux_irq_shim.c including every header it
 * can reach: a header nothing parses is a header nobody has checked, and
 * duplicating an existing API is exactly the mistake a writer of a new header
 * makes. So this file adds one symbol, and re-exports the rest.
 *
 * ktime_get_real_seconds() is the one call kernel.h cannot answer: seconds
 * since boot, because Xeneva carries no RTC-backed wall clock to read, and
 * tty_io.c:800 only stamps a "first opened" time for a deprecation warning --
 * a monotonic figure is the right answer there (a made-up epoch would be
 * worse than an honest relative one). Body in DCL/linux_irq_shim.c.
 */

#include <linux/kernel.h>

long long ktime_get_real_seconds(void);

#endif /* __LINUX_KTIME_H__ */
