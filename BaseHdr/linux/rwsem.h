#ifndef __LINUX_RWSEM_H__
#define __LINUX_RWSEM_H__

/*
 * DCL <linux/rwsem.h> -- the reader/writer semaphore n_tty and tty_io share
 * termios through.
 *
 * The surface is bigger than it first looks: 30-odd calls across the four
 * files, in two forms. Plain down_read/up_read/down_write/up_write guard the
 * termios_rwsem around each read and write, and the scoped form --
 * `scoped_guard(rwsem_read, &tty->termios_rwsem)`, `guard(rwsem_write)(...)`
 * -- appears wherever mainline recently converted to guards (26 calls in
 * n_tty.c alone, at 1037, 1560, 1678, 1872, 2349, 2489, 2611).
 *
 * As with <linux/spinlock.h>, the operations are no-ops and that is DCL's
 * existing locking contract rather than a decision made here: nobody else can
 * be holding the semaphore, so there is never a writer to wait behind and
 * never a reader to exclude. What matters is that the *acquire/release pairs
 * stay balanced* -- which is precisely what the guards buy, and why
 * rwsem_read/rwsem_write exist as classes rather than as bare calls: a
 * down_read() at the top of a function and its up_read() at the bottom are
 * separated by every early return in between.
 *
 * The one behaviour a reader could notice: with no writer exclusion, a
 * tcsetattr() landing mid-read is not excluded from that read. That is the
 * same recorded gap as <linux/mutex.h> notes, and it closes with it.
 *
 * `init_rwsem(s)` is called at uart/port construction (serial_core.c:3115,
 * tty_io.c:586 region) -- harmless either way, kept so the construction code
 * reads as it does upstream.
 */

#include <linux/kernel.h>
#include <linux/cleanup.h>

struct rw_semaphore {
	int count;
};

#define init_rwsem(s)		do { (void)(s); } while (0)
#define down_read(s)		do { (void)(s); } while (0)
#define up_read(s)		do { (void)(s); } while (0)
#define down_write(s)		do { (void)(s); } while (0)
#define up_write(s)		do { (void)(s); } while (0)
#define down_read_trylock(s)	(1)
#define down_write_trylock(s)	(1)
#define down_read_killable(s)	(0)
#define down_write_killable(s)	(0)
#define rwsem_is_locked(s)	(1)

DCL_LOCK_GUARD(rwsem_read, struct rw_semaphore, down_read(_T), up_read(_T))
DCL_LOCK_GUARD(rwsem_write, struct rw_semaphore, down_write(_T), up_write(_T))

#endif /* __LINUX_RWSEM_H__ */
