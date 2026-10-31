/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SEQLOCK_TYPES_H
#define __LINUX_SEQLOCK_TYPES_H

/*
 * DCL delta: mainline includes <linux/lockdep_types.h> and
 * <linux/mutex_types.h> here.  Neither exists in this tree and neither is
 * reachable for what it would supply:
 *
 *   - struct lockdep_map, only inside seqcount_t's `#ifdef
 *     CONFIG_DEBUG_LOCK_ALLOC` field -- that option is not in autoconf.h;
 *   - struct mutex, only as `__SEQ_LOCK(struct mutex *lock)` inside
 *     SEQCOUNT_LOCKNAME's seqcount_mutex_t, and __SEQ_LOCK expands to
 *     nothing unless CONFIG_LOCKDEP or CONFIG_PREEMPT_RT is set -- neither is.
 *
 * Porting mainline's mutex_types.h would be the wrong move anyway: DCL's
 * <linux/mutex.h> is `typedef int mutex`, and mainline's struct mutex pulls
 * in atomic_long_t, osq_lock and ww_acquire_ctx to describe a lock this tree
 * spells as an int.  The include is dropped rather than retargeted so that a
 * future CONFIG_DEBUG_LOCK_ALLOC turns into a missing type here instead of a
 * silently different one.
 */
#include <linux/spinlock_types.h>

/*
 * Sequence counters (seqcount_t)
 *
 * This is the raw counting mechanism, without any writer protection.
 *
 * Write side critical sections must be serialized and non-preemptible.
 *
 * If readers can be invoked from hardirq or softirq contexts,
 * interrupts or bottom halves must also be respectively disabled before
 * entering the write section.
 *
 * This mechanism can't be used if the protected data contains pointers,
 * as the writer can invalidate a pointer that a reader is following.
 *
 * If the write serialization mechanism is one of the common kernel
 * locking primitives, use a sequence counter with associated lock
 * (seqcount_LOCKNAME_t) instead.
 *
 * If it's desired to automatically handle the sequence counter writer
 * serialization and non-preemptibility requirements, use a sequential
 * lock (seqlock_t) instead.
 *
 * See Documentation/locking/seqlock.rst
 */
typedef struct seqcount {
	unsigned sequence;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} seqcount_t;

/*
 * For PREEMPT_RT, seqcount_LOCKNAME_t write side critical sections cannot
 * disable preemption. It can lead to higher latencies, and the write side
 * sections will not be able to acquire locks which become sleeping locks
 * (e.g. spinlock_t).
 *
 * To remain preemptible while avoiding a possible livelock caused by the
 * reader preempting the writer, use a different technique: let the reader
 * detect if a seqcount_LOCKNAME_t writer is in progress. If that is the
 * case, acquire then release the associated LOCKNAME writer serialization
 * lock. This will allow any possibly-preempted writer to make progress
 * until the end of its writer serialization lock critical section.
 *
 * This lock-unlock technique must be implemented for all of PREEMPT_RT
 * sleeping locks.  See Documentation/locking/locktypes.rst
 */
#if defined(CONFIG_LOCKDEP) || defined(CONFIG_PREEMPT_RT)
#define __SEQ_LOCK(expr)	expr
#else
#define __SEQ_LOCK(expr)
#endif

#define SEQCOUNT_LOCKNAME(lockname, locktype, preemptible, lockbase)	\
typedef struct seqcount_##lockname {					\
	seqcount_t		seqcount;				\
	__SEQ_LOCK(locktype	*lock);					\
} seqcount_##lockname##_t;

SEQCOUNT_LOCKNAME(raw_spinlock, raw_spinlock_t,  false,    raw_spin)
SEQCOUNT_LOCKNAME(spinlock,     spinlock_t,      __SEQ_RT, spin)
SEQCOUNT_LOCKNAME(rwlock,       rwlock_t,        __SEQ_RT, read)
SEQCOUNT_LOCKNAME(mutex,        struct mutex,    true,     mutex)
#undef SEQCOUNT_LOCKNAME

/*
 * Sequential locks (seqlock_t)
 *
 * Sequence counters with an embedded spinlock for writer serialization
 * and non-preemptibility.
 *
 * For more info, see:
 *    - Comments on top of seqcount_t
 *    - Documentation/locking/seqlock.rst
 */
context_lock_struct(seqlock) {
	/*
	 * Make sure that readers don't starve writers on PREEMPT_RT: use
	 * seqcount_spinlock_t instead of seqcount_t. Check __SEQ_LOCK().
	 */
	seqcount_spinlock_t seqcount;
	spinlock_t lock;
};
typedef struct seqlock seqlock_t;

#endif /* __LINUX_SEQLOCK_TYPES_H */
