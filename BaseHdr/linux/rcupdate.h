/*
 * DCL rcupdate.h -- no-op RCU for single-CPU Xeneva.
 *
 * mainline drm/xarray call the read-side + grace-period API; here there is no
 * preemption and no callback queue, so rcu_dereference degrades to a plain
 * load and call_rcu runs the callback immediately.  Immediate execution keeps
 * the xa_node free path honest: mainline never touches an object after
 * call_rcu() on it, which is exactly the contract a synchronous free needs.
 */
#ifndef _LINUX_RCUUPDATE_H
#define _LINUX_RCUUPDATE_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/cleanup.h>
#include <linux/list.h>

struct rcu_head {
	struct rcu_head *next;
	void (*func)(struct rcu_head *head);
};

#define __rcu
#define __rcu_alloc_check

#define list_for_each_entry_rcu(pos, head, member, ...) \
	list_for_each_entry(pos, head, member)
#define list_add_rcu(new, head)		list_add((new), (head))
#define list_add_tail_rcu(new, head)	list_add_tail((new), (head))
#define list_del_rcu(entry)		list_del((entry))

#define rcu_read_lock()			do { } while (0)
#define rcu_read_unlock()		do { } while (0)
#define rcu_read_lock_sched()		do { } while (0)
#define rcu_read_unlock_sched()		do { } while (0)
#define rcu_read_lock_bh()		do { } while (0)
#define rcu_read_unlock_bh()		do { } while (0)
#define rcu_read_lock_held()		1
#define rcu_read_lock_sched_held()	1

#define rcu_dereference(p)		(p)
#define rcu_dereference_raw(p)		(p)
#define rcu_dereference_check(p, c)	(p)
#define rcu_dereference_protected(p, c)	(p)
#define rcu_dereference_bh(p)		(p)
#define rcu_replace_pointer(p, v, c)	({ typeof(p) __tmp = (p); (p) = (v); __tmp; })
#define rcu_access_pointer(p)		(p)
#define rcu_dereference_sparse(p, c)

#define RCU_INIT_POINTER(p, v)		do { (p) = (v); } while (0)
#define rcu_assign_pointer(p, v)	do { (p) = (v); } while (0)
#define RCU_POINTER_INITIALIZER(p, v)	(v)

/*
 * RCU_INITIALIZER(), unrcu_pointer(), rcu_pointer_handoff() -- mainline's
 * three annotation helpers (rcupdate.h:525/:491/:788), with the sparse
 * markers left out because they are empty here: __rcu is above and __force
 * is kernel.h:239, so `(typeof(*(v)) __force __rcu *)(v)` and
 * `(typeof(*(v)) *)(v)` are the same cast.  What remains of the cast is its
 * one real job -- the type check -- so it is kept rather than dropped.
 *
 * RCU_INITIALIZER() adds the annotation and unrcu_pointer() removes it, and
 * both have exactly one caller: dma-fence-chain.c:65, the cmpxchg() in
 * dma_fence_chain_alloc(), which reads
 *
 *	tmp = unrcu_pointer(cmpxchg(&chain->prev, RCU_INITIALIZER(prev),
 *				    RCU_INITIALIZER(replacement)));
 *
 * and reaches this header via dma-fence-chain.h -> dma-fence.h.  The first
 * argument of that cmpxchg is `struct dma_fence __rcu **`, so the casts have
 * to land on that same pointer type or cmpxchg's argument check rejects
 * them; `typeof(*(v))` is the pointee with the annotation already stripped,
 * which is why the pointer spelling is the right one to cast to.
 *
 * rcu_pointer_handoff()'s one caller is dma-fence.h:380, in
 * dma_fence_get_rcu_safe(): it marks the moment a reference stops being
 * RCU-protected and becomes an owned one, which is a statement about the
 * caller, not a change to the pointer.
 */
#define RCU_INITIALIZER(v)	((typeof(*(v)) *)(v))
#define unrcu_pointer(p)	((typeof(*(p)) *)(p))
#define rcu_pointer_handoff(p)	(p)

typedef struct { int held; } dclg_rcu;
static inline void dclg_rcu_release(dclg_rcu* g) { (void)g; rcu_read_unlock(); }
static inline dclg_rcu dclg_rcu_ctor(void)
{
	dclg_rcu g = { .held = 1 };
	rcu_read_lock();
	return g;
}

static inline void synchronize_rcu(void) { }
static inline void synchronize_rcu_expedited(void) { }

static inline void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *))
{
	func(head);
}

#define kfree_rcu(ptr, rhf)		kfree(ptr)

#define rcu_seq_start(s)		do { } while (0)
#define rcu_seq_done(s, c)		1

#endif /* _LINUX_RCUUPDATE_H */
