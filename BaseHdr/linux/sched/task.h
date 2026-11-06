#ifndef __DCL_LINUX_SCHED_TASK_H__
#define __DCL_LINUX_SCHED_TASK_H__

#include <linux/sched.h>	/* struct task_struct, for the pair below */

/*
 * DCL <linux/sched/task.h> -- put_task_struct(), the release half of a task
 * reference.
 *
 * Filled from tty_ldsem.c:98 and :217, which drop the reference taken when a
 * waiter was queued (`get_task_struct(waiter->task)` on the way in).  Those
 * are the only two uses in anything staged, and they are why the body is a
 * no-op rather than a decrement: mainline's is
 *
 *     if (refcount_dec_and_test(&t->usage)) __put_task_struct(t);
 *
 * and there is no __put_task_struct() to reach -- DCL's struct task_struct is
 * never freed (sched.h has no free path for one), so its reference count has
 * no last-holder event.  Decrementing a counter whose crossing does nothing
 * would be a read-modify-write on a field that does not exist.
 *
 * The pointer is taken because the call site's argument is one, not because
 * it is used.
 *
 * get_task_struct() is the other half and is here for the same reason
 * tty_ldsem.c:184 needs it -- `get_task_struct(current)` before parking on
 * the semaphore, `put_task_struct(waiter->task)` when the queue drains.  It
 * is a no-op for the matching reason: with no free path for a task_struct,
 * acquiring a reference to one cannot fail or overflow anything either.  The
 * pair is supplied together because a one-sided refcount protocol is exactly
 * the sort of thing that reads as correct until the second side is added.
 *
 *   upstream  include/linux/sched/task.h
 */
static inline void get_task_struct(struct task_struct* t)
{
	(void)t;
}

static inline void put_task_struct(struct task_struct* t)
{
	(void)t;
}

#endif /* __DCL_LINUX_SCHED_TASK_H__ */
