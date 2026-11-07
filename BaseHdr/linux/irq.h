#ifndef __LINUX_IRQ_H__
#define __LINUX_IRQ_H__

/*
 * DCL <linux/irq.h> -- the two helpers the serial code reads the interrupt
 * state through. Mainline's header is the irq-domain/chip layer (thousands of
 * lines of controller plumbing DCL does not have and does not call), so this
 * one carries only the read side plus the registration API by including
 * <linux/interrupt.h>.
 *
 * irq_get_irq_data() returns NULL: no wakeup source is ever armed here, and
 * irqd_is_wakeup_set() is therefore truthfully 0. That is not a stub hiding a
 * feature -- serial_core.c:2385 and 8250_port.c:1828 both test the result
 * before acting, and "nothing is a wake source" is the exact answer.
 *
 * struct irq_data stays incomplete on purpose: those two sites only ever pass
 * the pointer back to irqd_is_wakeup_set(), and an opaque type keeps anyone
 * from reaching into a structure DCL does not maintain.
 */

#include <linux/interrupt.h>

struct irq_data;

static inline struct irq_data* irq_get_irq_data(unsigned int irq) {
	(void)irq;
	return 0;
}

#define irqd_is_wakeup_set(d) (0)


/*
 * irq_canonicalize() -- collapse an irq number to the canonical form a
 * controller uses.  Upstream it exists for x86's split ISA/IO-APIC vectors;
 * arm64's GIC numbers lines directly and the identity function is what
 * mainline itself gives an architecture without that split.  The value walks
 * straight through: serial_core.c:905 stores the result back into
 * new_info->irq, so returning the input unchanged is what keeps a port's
 * reported irq equal to the one it was configured with.
 */
#define irq_canonicalize(irq)	(irq)

/*
 * enable_irq_wake() / disable_irq_wake() -- mark an interrupt line as able to
 * wake the system from sleep.  Both are only called under
 * device_may_wakeup(), which is 0 (see <linux/pm_runtime.h>), so neither
 * reaches an interrupt controller that does not exist.  They still have to
 * *return* something the calling convention expects: 0 is mainline's "the
 * line accepted the request", and a negative here would be read as "this
 * device cannot wake the system" by the resume path at serial_core.c:2386,
 * which is the answer that changes behaviour -- so the success value is the
 * one that keeps every branch where it already is.
 */
static inline int enable_irq_wake(unsigned int irq)
{
	(void)irq;
	return 0;
}

static inline int disable_irq_wake(unsigned int irq)
{
	(void)irq;
	return 0;
}
#endif /* __LINUX_IRQ_H__ */
