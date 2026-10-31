#ifndef __LINUX_MUTEX_H__
#define __LINUX_MUTEX_H__

/*
 * DCL <linux/mutex.h> -- mutexes, plus the two guard classes tty/serial code
 * takes them through.
 *
 * mutex_init/mutex_lock/mutex_unlock are already defined in
 * <linux/kernel.h> as no-ops, and `typedef int mutex;` lives there too.
 * This header supplies what the mainline files call but kernel.h does not:
 *
 *   - `struct mutex`, the tag serial_core.c and tty_io.c declare fields with.
 *     (kernel.h's `typedef int mutex` is a different identifier -- the struct
 *     tag and the ordinary name are separate namespaces in C -- so both can
 *     coexist. `struct mutex m;` gets the struct; `mutex_lock(&m)` still hits
 *     kernel.h's macro.)
 *
 *   - mutex_lock_interruptible / mutex_trylock / mutex_is_locked, each
 *     answering in the sense the no-op contract implies: an interruptible
 *     take always succeeds, a trylock always wins, and a lock held by the
 *     running context reads as held. That last one is worth stating plainly
 *     -- there is no contention for it to observe, so "held" is not a
 *     simulation of a queue, it is the truth about a lock nobody else can
 *     be waiting on.
 *
 *   - the `mutex` and `mutex_intr` guard classes. mutex_intr is conditional:
 *     scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &port->mutex) at
 *     serial_core.c:1144 is the only conditional guard in the whole set, and
 *     it is what makes that function's early return safe.
 *
 * Concurrency note, recorded rather than hidden: tty's correctness leans on
 * these for serialising termios changes against readers, and on rwsem for the
 * same. With no-op locks that serialisation does not happen. It does not
 * affect a single reader on a single console -- which is every configuration
 * this milestone targets -- but two processes opening the same port and
 * racing a tcsetattr against a read is not protected. Making these real is a
 * separate, deliberate change: it is the moment Xeneva's process-level
 * locking has to reach into driver context.
 */

#include <linux/kernel.h>	/* mutex_init/lock/unlock, typedef int mutex */
#include <linux/cleanup.h>
#include <linux/atomic.h>	/* atomic_long_t -- <linux/ww_mutex.h> puts one
				 * in struct ww_class and adds to it from
				 * ww_mutex_lock() without naming this header.
				 * mainline's mutex.h takes it the same way
				 * (:18), so this is DCL catching up rather
				 * than a divergence: without it ww_mutex.h
				 * saw neither the type nor
				 * atomic_long_inc_return_relaxed() and clang
				 * reported the definition as conflicting with
				 * its own earlier implicit declaration. */
#include <linux/lockdep.h>	/* lockdep_set_subclass() -- called on
				 * &tty_port.buf.lock by tty_buffer.c:618,
				 * which includes no lockdep header itself,
				 * so this is where it has to be reachable
				 * from. */

struct mutex {
	int state;
};

#define mutex_lock_interruptible(m)	(0)
#define mutex_trylock(m)		(1)
#define mutex_is_locked(m)		(1)
#define mutex_lock_killable(m)		(0)
#define mutex_unlock_nested(m, s)	mutex_unlock(m)
/* mainline kernel/locking/mutex.c; ww_mutex.h's ww_mutex_base_init()
 * and drm_drv.c's teardown spell the constructor/destructor this way. */
#define __mutex_init(m, name, key)	mutex_init(m)
#define mutex_destroy(m)		do { (void)(m); } while (0)

/*
 * atomic_dec_and_mutex_lock(v, lock) -- mainline's "last reference takes the
 * lock" pair (mutex.h:64): decrement, and only if the counter reached zero,
 * lock on the way out.  drm_file.c:505 (drm_release_noglobal) is the caller,
 * and it branches on the return value.  The lock half is a no-op in DCL
 * (mutex_lock, kernel.h:222), so atomic_dec_and_test() is the whole of the
 * semantics that survives: false without locking when the count is not zero,
 * true after locking when it is.  It is called through mutex_lock() rather
 * than skipped so the day the mutex becomes real the pairing is already
 * spelled out here.
 */
static inline bool atomic_dec_and_mutex_lock(atomic_t* v, struct mutex* lock)
{
	if (!atomic_dec_and_test(v))
		return false;
	mutex_lock(lock);
	return true;
}

DCL_LOCK_GUARD(mutex, struct mutex, mutex_lock(_T), mutex_unlock(_T))
DCL_LOCK_GUARD_COND(mutex_intr, struct mutex,
			mutex_lock_interruptible(_T), mutex_unlock(_T))


/*
 * DEFINE_MUTEX(name) -- the static-initialiser form.
 *
 * serial_core.c:40 writes `static DEFINE_MUTEX(port_mutex);`, and without the
 * macro clang read it as an implicit function declaration followed by a
 * parameter list: two errors (implicit-int, "parameter list without types")
 * at one line, which is the tell for a missing object-like macro rather than
 * a missing function.  port_mutex is the lock serial_core holds while walking
 * its driver list (serial_core_ctrl_find() says as much in its doc comment),
 * so it has to be a real object with static storage duration even though
 * taking it is a no-op.
 *
 * mainline's __MUTEX_INITIALIZER fills in wait_lock, osq, wait_list and a
 * lockdep map.  DCL's struct mutex is one int (see above), so { 0 } is the
 * entire initialiser -- and that is the point: there is nothing to lay out.
 */
#define DEFINE_MUTEX(name) struct mutex name = { 0 }
#endif /* __LINUX_MUTEX_H__ */
