/*
 * DCL irq_work.h -- irq_work runs as a DCL work_struct, inline.
 *
 * dma-fence-chain.c raises an irq_work to signal the chain without taking
 * the fence lock in IRQ context; DCL has no IRQ-side queue, and
 * workqueue.h's schedule_work already runs the callback synchronously, so
 * queueing here behaves like mainline's "runs on next irq return" for our
 * single-threaded flow.
 */
#ifndef _LINUX_IRQ_WORK_H
#define _LINUX_IRQ_WORK_H

#include <linux/workqueue.h>

struct irq_work {
	struct work_struct base;
	void (*func)(struct irq_work *work);
};

static inline void dcl_irq_work_trampoline(struct work_struct *base)
{
	struct irq_work *work =
		(struct irq_work *)((char *)base - __builtin_offsetof(struct irq_work, base));
	work->func(work);
}

#define init_irq_work(work, fn)		do {		\
	(work)->func = (fn);				\
	INIT_WORK(&(work)->base, dcl_irq_work_trampoline);	\
} while (0)

static inline bool irq_work_queue(struct irq_work *work)
{
	return schedule_work(&work->base);
}

static inline void irq_work_sync(struct irq_work *work)
{
	flush_work(&work->base);
}

#endif /* _LINUX_IRQ_WORK_H */
