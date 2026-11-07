#ifndef __ASM_IRQ_H__
#define __ASM_IRQ_H__

/*
 * DCL <asm/irq.h> -- probe_irq_on()/probe_irq_off(), the ISA interrupt probe.
 *
 * mainline provides these only to architectures with legacy interrupt
 * controllers that can be asked "which line fired?" by masking and unmasking
 * them; arm64's GIC cannot be probed that way, and neither does anything
 * Xeneva runs.  They are still required here because 8250's autoconfig_irq()
 * (8250_port.c:1234, 1241, 1256) calls them unconditionally while working out
 * whether an ISA-style port has an interrupt line -- the calls sit outside
 * any config arm.
 *
 * The answers are chosen to make that code take its *honest* branch:
 *
 *   probe_irq_on()   -> 0, "no interrupt is being probed for"
 *   probe_irq_off(0) -> -ENODEV, "and none was found"
 *
 * and autoconfig_irq() then does `port->irq = (irq > 0) ? irq : 0`
 * (8250_port.c:1266), i.e. the port comes up interrupt-free.  That is the
 * correct result for a polled/console UART and the wrong result for nothing:
 * a driver that believed it had an IRQ it never received would spin in the
 * RX path waiting for a handler that is never installed, whereas irq 0 keeps
 * every caller on the polling path they are already written for.
 *
 * Returning 0 from probe_irq_off() instead would have looked the same at that
 * one call site and would have been wrong at the next, where -ENODEV is the
 * documented "no IRQ" answer.
 */
#include <linux/errno.h>	/* -ENODEV */

static inline unsigned long probe_irq_on(void)
{
	return 0;
}

static inline int probe_irq_off(unsigned long irqs)
{
	(void)irqs;
	return -ENODEV;
}

#endif /* __ASM_IRQ_H__ */
