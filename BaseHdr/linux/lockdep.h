#ifndef __LINUX_LOCKDEP_H__
#define __LINUX_LOCKDEP_H__

/*
 * DCL <linux/lockdep.h> -- the lock-dependency API, as no-ops.
 *
 * lockdep's whole job is to notice at *runtime* that a program acquires locks
 * in two different orders and might therefore deadlock -- it tracks a graph of
 * held locks while running. DCL has no lock graph, and its locks are no-ops
 * (see <linux/spinlock.h> and <linux/mutex.h>), so there is no acquisition
 * order to record in the first place.
 *
 * The reason this header exists rather than being skipped is that ported code
 * *names* lockdep: tty_buffer.c:618 calls
 *
 *     lockdep_set_subclass(&port->buf.lock, TTY_LOCK_SLAVE);
 *
 * as its final line, and 8250_port.c includes this header outright. A call
 * with no declaration is an implicit-function-declaration error, so the call
 * has to parse. What the call means upstream -- "this lock is being taken
 * nested, tell lockdep it is a different instance so it does not report a
 * recursion" -- has no referent when locks do not recurse, so the arguments
 * are evaluated for side effects and dropped.
 *
 * lockdep_init_map_type() and friends take variadic arguments including
 * format strings, hence the `...` form rather than a fixed parameter list:
 * mainline's call sites pass different numbers of them.
 */

#include <linux/kernel.h>

#define lockdep_set_class(lock, key)		do { (void)(lock); (void)(key); } while (0)
#define lockdep_set_class_and_name(lock, key, name) \
	do { (void)(lock); (void)(key); (void)(name); } while (0)
#define lockdep_set_subclass(lock, sub)		do { (void)(lock); (void)(sub); } while (0)
#define lockdep_set_novalidate_class(lock)	do { (void)(lock); } while (0)

/*
 * The `lock` argument is dropped rather than evaluated, which is what
 * mainline's own !CONFIG_LOCKDEP arm does (lockdep.h:349, `do { (void)(name);
 * (void)(key); } while (0)`).  The difference matters because mainline's
 * seqlock.h is a byte-port here and its __seqcount_init() (:48) and
 * do_write_seqcount_begin_nested() (:478) both address `&s->dep_map` -- a
 * member seqcount_t only has when CONFIG_DEBUG_LOCK_ALLOC is on, which it is
 * not.  Evaluating the first parameter type-checked that member access and
 * turned every dma-resv.c inclusion into "no member named 'dep_map' in
 * 'struct seqcount'".  A macro parameter that never appears in its
 * replacement list is not expanded at all, so the expression never reaches C.
 *
 * mainline evaluates name and key and so does this: both are live parameters
 * of __seqcount_init(), so the (void) casts also keep them from warning.
 *
 * seqcount_acquire/seqcount_release are mainline's own two-step names
 * (lockdep.h:528/:530), spelled the same way as the rwsem quartet below for
 * the same reason -- the lockdep_map they would take does not exist here.
 */
#define lockdep_init_map(lock, name, key, sub)		\
	do { (void)(name); (void)(key); } while (0)
#define lockdep_init_map_type(lock, name, key, sub, ...)		\
	do { (void)(name); (void)(key); } while (0)

#define seqcount_acquire(l, s, t, i)		do {} while (0)
#define seqcount_release(l, i)			do {} while (0)

#define lockdep_match_class(lock, key)		(1)
#define lockdep_match_name(lock, name)		(1)

#define lockdep_rcu_suspicious(file, line, s)				\
	do { (void)(file); (void)(line); (void)(s); } while (0)

#define lockdep_assert_held(l)			do { (void)(l); } while (0)
#define lockdep_assert_held_write(l)		do { (void)(l); } while (0)
#define lockdep_assert_not_held(l)		do { (void)(l); } while (0)
#define lockdep_assert_irqs_disabled()		do {} while (0)
#define lockdep_assert_irqs_enabled()		do {} while (0)
#define lockdep_assert_in_rcu()			do {} while (0)
#define lockdep_assert_preemption_disabled()	do {} while (0)

/*
 * The three names the drm core adds to that family.  drm_auth.c:66 is the
 * interesting one:
 *
 *     lockdep_assert_once(lockdep_is_held(&fpriv->master_lookup_lock) ||
 *                         lockdep_is_held(&dev->master_mutex));
 *
 * and drm_syncobj.c:385/:1020 hand lockdep_is_held() to
 * rcu_dereference_protected()'s condition slot.  mainline answers "does this
 * task hold this lock" out of the held-lock graph it maintains while running
 * (lockdep.h:252); DCL has no graph and its locks are no-ops, so nothing is
 * ever held and no order can be violated -- `1` is the answer that makes
 * every one of these assertions pass, which is what an assertion that can
 * only fail with lockdep is supposed to do.  The RCU condition is a hint
 * DCL's rcu_dereference_protected() drops anyway (rcupdate.h).
 *
 * lockdep_assert_once() *does* evaluate its argument, unlike its neighbours
 * above that take no value and unlike mainline's !CONFIG_LOCKDEP form
 * (`do { } while (0)`, lockdep.h:390): the argument is the expression under
 * assertion, and evaluating it is what keeps the lock it names used -- the
 * same reason lockdep_set_subclass() above evaluates its arguments.  The
 * compiler then folds it away, since lockdep_is_held() is the constant 1.
 * A macro rather than an extern declaration (which is what upstream's
 * !CONFIG_LOCKDEP build uses) follows lockdep_match_class() above, and both
 * files that reference it reach this header, so no definition is needed.
 */
#define lockdep_is_held(lock)			(1)
#define lockdep_assert_once(cond)		do { (void)(cond); } while (0)
#define lockdep_assert_none_held_once()		do {} while (0)

/*
 * The rwsem acquire/release quartet -- tty_ldsem.c:300-310, which brackets
 * every ldsem_up/_down with them:
 *
 *     rwsem_acquire_read(&sem->dep_map, subclass, 0, _RET_IP_);
 *     count = atomic_long_add_return(LDSEM_READ_BIAS, &sem->count);
 *     ...
 *     rwsem_release(&sem->dep_map, _RET_IP_);
 *
 * mainline's versions are real when CONFIG_LOCKDEP and CONFIG_PROVE_LOCKING
 * are on and no-ops otherwise, so this is the arm every non-debug build
 * already takes.  What is *not* borrowed is mainline's `(void)(l)` argument
 * handling: these four take a `struct lockdep_map *` that struct ld_semaphore
 * does not carry (tty_ldisc.h), and evaluating it would mean adding a member
 * whose only purpose is to be addressed and forgotten.  Dropping the
 * parameter instead means the expression never reaches C at all -- a macro
 * parameter that does not appear in the replacement list is not expanded --
 * which also leaves _RET_IP_ (a lockdep return-address macro) as a bare
 * identifier that never has to be defined.
 *
 * Argument order is mainline's: lockdep_map, subclass[, read], ip.
 */
#define rwsem_acquire(l, s, t, i)		do {} while (0)
#define rwsem_acquire_read(l, s, t, i)		do {} while (0)
#define rwsem_release(l, i)			do {} while (0)
#define lock_contended(l, i)			do {} while (0)
#define lock_acquired(l, i)			do {} while (0)

/*
 * lock_acquire_shared_recursive() / lock_release() -- the two lockdep calls
 * drm_connector.c makes around the connector list iterator, at :1054 and
 * :1135. Same spelling as the five above, same argument-dropping reason, but
 * here it is load-bearing rather than tidy.
 *
 * The second argument of the first call and the first of the second is
 * `&connector_list_iter_dep_map`, and mainline only *defines* that object
 * inside `#ifdef CONFIG_LOCKDEP` (drm_connector.c:1032-1036). The two call
 * sites are not under that ifdef. They still compile in mainline because
 * mainline's no-op spelling of these macros discards its arguments before
 * they reach C, so the address of an object that was never declared is
 * parsed and then thrown away.
 *
 * That is exactly what dropping the parameter does here, and it is why the
 * macros must not be written `(void)(l)`: evaluating the argument would
 * require a declaration, the declaration lives behind an ifdef that is off,
 * and the only ways out would be editing the vendored .c or unguarding a
 * lockdep object this kernel does not run.
 */
#define lock_acquire_shared_recursive(l, s, r, n, i)	do {} while (0)
#define lock_release(l, i)				do {} while (0)

struct lock_class_key { };
struct lockdep_map { };


/*
 * lockdep_assert_held_once -- the one assertion the tty/serial headers use
 * that this header did not have (8250.h:200, 8250_port.c:555, and every
 * uart_port_lock* wrapper in serial_core.h reach for it).
 *
 * mainline's is a real test that fires once per site when the lock is not
 * held; DCL runs no lockdep, so it is the same no-op as
 * lockdep_assert_held() directly above.  The distinction that survives is
 * arity: these are macros taking an lvalue, and the `(void)(l)` evaluates the
 * argument so `-Wunused-but-set-variable` does not fire on a lock that is
 * only ever asserted about.
 */
#define lockdep_assert_held_once(l)		do { (void)(l); } while (0)
#endif /* __LINUX_LOCKDEP_H__ */
