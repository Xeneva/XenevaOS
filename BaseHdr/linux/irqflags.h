/*
 * DCL <linux/irqflags.h> -- the one thing the kernel side asks about
 * interrupt state: irqs_disabled().
 *
 * mainline gets it here, and every consumer reaches it through
 * <linux/interrupt.h>'s own include of this header (mainline: 12), which is
 * why <drm/drm_util.h>:37 -- `in_atomic() || in_dbg_master() || irqs_disabled()`
 * inside drm_can_sleep() -- resolves without this header being named at the
 * call site.  <linux/interrupt.h> in this tree carries the same include.
 *
 * The answer is read from the real register rather than from a flag DCL
 * keeps, because there is no such flag: daif's bit 7 (DAIF.I) is the mask
 * ARM64 itself honours, and Hal/AA64/aa64lowlevel.s:309 already exposes it
 * as read_daif().  A fabricated constant would be right almost always and
 * wrong precisely when drm_can_sleep() matters -- inside an IRQ handler that
 * took a lock it should not have.
 *
 * Deliberately not here: local_irq_save/restore/disable/enable.  The only
 * mentions in this tree are seqlock.h:72/:75, inside a CONFIG_LOCKDEP arm
 * that does not compile, and DCL has no way to mask interrupts from C (only
 * restore_daif() to put an old value back).  Writing those macros as no-ops
 * would make the next caller believe it had a critical section; letting the
 * compiler say "implicit declaration" instead means it has to be built
 * honestly, the day something here actually needs it.
 */
#ifndef __LINUX_IRQFLAGS_H__
#define __LINUX_IRQFLAGS_H__

#include <Hal/AA64/aa64lowlevel.h>	/* read_daif() */

static inline int irqs_disabled(void)
{
	return (read_daif() & (1UL << 7)) != 0;	/* DAIF_I */
}

#endif /* __LINUX_IRQFLAGS_H__ */
