#ifndef _TTY_INTERNAL_H
#define _TTY_INTERNAL_H

/*
 * DCL/tty.h -- the tty core's private header, the port of mainline's
 * drivers/tty/tty.h (v7.2).
 *
 * It is a *quoted* include: DCL/tty_buffer.c writes `#include "tty.h"`, the
 * compiler looks in the including file's directory first, and lands here
 * rather than at <linux/tty.h> (BaseHdr/linux/tty.h). That is exactly the
 * split mainline relies on -- the public header is what a driver includes,
 * this one is what the tty core's own files include -- and it is the reason
 * this file can be named the same as a completely unrelated BaseHdr header
 * without anyone having to be told which is which.
 *
 * Contents, in two groups:
 *
 *   tty_msg()/tty_*()  -- the logging front end. It prefixes every message
 *                         with "driver_name tty_name:", which is the only way
 *                         to tell which port complained once there is more
 *                         than one. It calls tty_driver_name() and tty_name(),
 *                         both declared public and both defined by tty_io.c;
 *                         nothing in stage 2 logs through it, so no shim
 *                         definition is provided (a shim copy would collide
 *                         with tty_io.c's at stage 5).
 *
 *   the rest           -- functions the tty core's own files call across file
 *                         boundaries: the flip-buffer internals that
 *                         tty_buffer.c exports to tty_io.c/tty_ldisc.c, the
 *                         ldisc locking pair, the tty_mutex helpers, and the
 *                         file-list plumbing.
 *
 * What is *not* here: anything behind CONFIG_AUDIT (tty_audit_* are inline
 * no-ops -- see <linux/tty.h>'s block for why that branch is chosen) and
 * redirected_tty_write(), which needs struct kiocb/struct iov_iter and has no
 * caller in the staged set.
 */

#include <linux/tty.h>
#include <linux/printk.h>
#include <linux/cleanup.h>

#define tty_msg(fn, tty, f, ...) \
	fn("%s %s: " f, tty_driver_name(tty), tty_name(tty), ##__VA_ARGS__)

#define tty_debug(tty, f, ...)	tty_msg(pr_debug, tty, f, ##__VA_ARGS__)
#define tty_notice(tty, f, ...)	tty_msg(pr_notice, tty, f, ##__VA_ARGS__)
#define tty_warn(tty, f, ...)	tty_msg(pr_warn, tty, f, ##__VA_ARGS__)
#define tty_err(tty, f, ...)	tty_msg(pr_err, tty, f, ##__VA_ARGS__)

#define tty_info_ratelimited(tty, f, ...) \
		tty_msg(pr_info_ratelimited, tty, f, ##__VA_ARGS__)

/*
 * Lock subclasses for tty locks
 *
 * TTY_LOCK_NORMAL is for normal ttys and master ptys.
 * TTY_LOCK_SLAVE is for slave ptys only.
 *
 * Lock subclasses are necessary for handling nested locking with pty pairs.
 * tty locks which use nested locking:
 *
 * legacy_mutex - Nested tty locks are necessary for releasing pty pairs.
 *		  The stable lock order is master pty first, then slave pty.
 * termios_rwsem - The stable lock order is tty_buffer lock->termios_rwsem.
 *		   Subclassing this lock enables the slave pty to hold its
 *		   termios_rwsem when claiming the master tty_buffer lock.
 * tty_buffer lock - slave ptys can claim nested buffer lock when handling
 *		     signal chars. The stable lock order is slave pty, then
 *		     master.
 */
enum {
	TTY_LOCK_NORMAL = 0,
	TTY_LOCK_SLAVE,
};

/* Values for tty->flow_change */
enum tty_flow_change {
	TTY_FLOW_NO_CHANGE,
	TTY_THROTTLE_SAFE,
	TTY_UNTHROTTLE_SAFE,
};

static inline void __tty_set_flow_change(struct tty_struct* tty,
					 enum tty_flow_change val)
{
	tty->flow_change = val;
}

static inline void tty_set_flow_change(struct tty_struct* tty,
				       enum tty_flow_change val)
{
	tty->flow_change = val;
	smp_mb();
}

int tty_ldisc_lock(struct tty_struct* tty, unsigned long timeout);
void tty_ldisc_unlock(struct tty_struct* tty);

int __tty_check_change(struct tty_struct* tty, int sig);
int tty_check_change(struct tty_struct* tty);
void __stop_tty(struct tty_struct* tty);
void __start_tty(struct tty_struct* tty);
void tty_write_unlock(struct tty_struct* tty);
int tty_write_lock(struct tty_struct* tty, bool ndelay);
void tty_vhangup_session(struct tty_struct* tty);
void tty_open_proc_set_tty(struct file* filp, struct tty_struct* tty);
int tty_signal_session_leader(struct tty_struct* tty, int exit_session);
void session_clear_tty(struct pid* session);

/* tty_buffer.c */
void tty_buffer_free_all(struct tty_port* port);
void tty_buffer_flush(struct tty_struct* tty, struct tty_ldisc* ld);
void tty_buffer_init(struct tty_port* port);
void tty_buffer_set_lock_subclass(struct tty_port* port);
bool tty_buffer_restart_work(struct tty_port* port);
bool tty_buffer_cancel_work(struct tty_port* port);
void tty_buffer_flush_work(struct tty_port* port);

void tty_ldisc_hangup(struct tty_struct* tty, bool reset);
int tty_ldisc_reinit(struct tty_struct* tty, int disc);
long tty_ioctl(struct file* file, unsigned int cmd, unsigned long arg);
long tty_jobctrl_ioctl(struct tty_struct* tty, struct tty_struct* real_tty,
		       struct file* file, unsigned int cmd, unsigned long arg);
void tty_default_fops(struct file_operations* fops);
struct tty_struct* alloc_tty_struct(struct tty_driver* driver, int idx);
int tty_alloc_file(struct file* file);
void tty_add_file(struct tty_struct* tty, struct file* file);
void tty_free_file(struct file* file);
int tty_release(struct inode* inode, struct file* filp);

#define tty_is_writelocked(tty)  (mutex_is_locked(&tty->atomic_write_lock))

int tty_ldisc_setup(struct tty_struct* tty, struct tty_struct* o_tty);
void tty_ldisc_release(struct tty_struct* tty);
int tty_ldisc_init(struct tty_struct* tty);
void tty_ldisc_deinit(struct tty_struct* tty);

extern int tty_ldisc_autoload;

/* tty_audit.c -- CONFIG_AUDIT is off; inline no-ops, as in mainline. */
static inline void tty_audit_add_data(const struct tty_struct* tty,
				      const void* data, size_t size)
{
	(void)tty; (void)data; (void)size;
}
static inline void tty_audit_tiocsti(const struct tty_struct* tty, u8 ch)
{
	(void)tty; (void)ch;
}

int tty_insert_flip_string_and_push_buffer(struct tty_port* port,
					   const u8* chars, size_t cnt);

#endif /* _TTY_INTERNAL_H */
