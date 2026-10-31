/*
 * DCL/tty_io_shim.c -- drivers/tty/tty_io.c, cut down to what the tty core
 * around it can observe today.
 *
 * mainline's tty_io.c is 3672 lines, and the majority of them are pieces DCL
 * does not have yet.  The open path walks a struct file list (fdtable, fget,
 * alloc_file, tty_add_file); the registration path reserves a chrdev region
 * out of a table that says which majors are taken and hangs a struct class
 * off tty_class so sysfs can publish it; __tty_hangup() swaps f_ops out from
 * under every open file and sends SIGTTOU to the foreground group.  Those
 * need the VFS, the device model and POSIX job control, which is stage 5 --
 * vendoring tty_io.c then is what this file exists to be deleted by, so
 * every symbol defined here is one tty_io.c also defines and the two must
 * never meet in the same link.
 *
 * What is here is the rest, kept in mainline's shape so that the diff at
 * stage 5 is readable:
 *
 *   - the allocator and the driver table -- __tty_alloc_driver,
 *     tty_standard_install, tty_driver_kref_put, tty_register_driver and its
 *     unregister, tty_drivers, tty_std_termios.  These *run* rather than
 *     merely link: uart_register_driver() in serial_core.c drives them, and
 *     that is the path a stage-3 uart registration takes.
 *   - the device nodes -- tty_register_device_attr, tty_unregister_device,
 *     which go through device_create()/device_destroy() instead of
 *     kzalloc + device_register(), because DCL's device model is a table of
 *     devfs slots and not a sysfs tree (linux_cdev_shim.c).
 *   - the small tty-level operations the line discipline and the port layer
 *     call -- tty_write_lock, tty_wakeup, tty_send_xchar, __start_tty,
 *     tty_hangup, tty_kref_put, tty_name.
 *   - tty_check_change, which is not tty_io.c's at all: it belongs to
 *     drivers/tty/tty_jobctrl.c, which is vendored out here for the same
 *     reason tty_io.c is (it needs tasklist_lock, signal_struct.tty_old_pgrp
 *     and kill_pgrp).
 *
 * Six departures from mainline's bodies, each marked where it happens:
 * the chrdev region, the cdev layer, the procfs registration, the hangup's
 * file table, the flow-control wrappers, and hung_up_tty_fops.  None of them
 * changes behaviour any staged caller can see, because none of those callers
 * can reach the thing that would have made it observable.
 *
 *   upstream  drivers/tty/tty_io.c (v7.2), drivers/tty/tty_jobctrl.c
 */

#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_ldisc.h>
#include <linux/termios.h>
#include <linux/printk.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/sprintf.h>
#include <linux/workqueue.h>
/*
 * No <linux/cdev.h>: DCL has no such header, and it would only be here for
 * cdev_del(), which nothing below calls (see destruct_tty_driver).  No
 * <linux/err.h> either -- <linux/tty.h> -> <linux/fs.h> already gives this
 * file ERR_PTR/PTR_ERR/IS_ERR, and the two headers spell IS_ERR differently
 * (err.h:51 tests IS_ERR_VALUE, fs.h:211 dcl_is_err), so including both
 * would be a macro redefinition for the same answer.
 */

/*
 * mainline's private header for this file.  It is the one that declares
 * tty_write_lock/tty_write_unlock, tty_check_change and __start_tty/
 * __stop_tty -- none of which appear in <linux/tty.h>, and all of which are
 * referenced from tty_ioctl.c and tty_port.c too, which is why it is
 * includable alongside <linux/tty.h> rather than instead of it.
 */
#include "../Vendored/drivers/tty/tty.h"

/*
 * The two globals.  LIST_HEAD(tty_drivers) is mainline's own line at
 * tty_io.c:141 and it must be LIST_HEAD and not a zeroed struct list_head:
 * a zeroed pair of pointers is not an empty list, it is a list whose first
 * node is NULL, and the first traversal would dereference it.
 */
LIST_HEAD(tty_drivers);

/*
 * tty_std_termios -- mainline's tty_io.c:124, "for the benefit of tty
 * drivers": the settings a driver sees before anyone has configured it.
 * 9600-ish line at 38400 8N1, cooked, with signals on -- a text console.
 *
 * The control characters are written out as designated initialisers instead
 * of mainline's `.c_cc = INIT_C_CC`.  That macro is not in mainline's
 * <linux/tty.h> (nor anywhere DCL has copied), so writing the octal string
 * back would mean inventing the spelling rather than reading it; these are
 * the same values, named, and the ones that are 0 are left to the zero
 * fill.  VMIN 1 / VTIME 0 is the canonical-mode default every other index
 * here is relative to -- VINTR 3 (^C), VQUIT 28 (^\), VERASE 127 (DEL),
 * VKILL 21 (^U), VEOF 4 (^D), VSTART 17 (^Q), VSTOP 19 (^S), VSUSP 26 (^Z),
 * VREPRINT 18 (^R), VDISCARD 15 (^O), VWERASE 23 (^W), VLNEXT 22 (^V).
 */
struct ktermios tty_std_termios = {
	.c_iflag = ICRNL | IXON,
	.c_oflag = OPOST | ONLCR,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE |
		   IEXTEN,
	.c_cc = {
		[VINTR] = 3,      [VQUIT] = 28,     [VERASE] = 127,
		[VKILL] = 21,     [VEOF] = 4,       [VMIN] = 1,
		[VSTART] = 17,    [VSTOP] = 19,     [VSUSP] = 26,
		[VREPRINT] = 18,  [VDISCARD] = 15,  [VWERASE] = 23,
		[VLNEXT] = 22,
	},
	.c_ispeed = 38400,
	.c_ospeed = 38400,
	/* mainline leaves .c_line commented out too; N_TTY is 0 anyway. */
};

/*
 * tty_class -- mainline has this at tty_io.c:3545 as a non-static global
 * with a devnode() hook that loosens /dev/tty and /dev/ptmx to 0666.  Static
 * here because the only thing that reads it is device_create()/device_destroy()
 * below, both of which discard their class argument outright
 * (linux_cdev_shim.c: `(void)class;`) -- so the devnode hook has nothing to
 * run on and the .name is documentation.  Keeping it static also means this
 * file cannot leave a half-defined twin behind for tty_io.c to collide with.
 */
static const struct class tty_class = {
	.name = "tty",
};

/* ---------------------------------------------------------------- helpers */

/*
 * dcl_tty_line_name() -- the node name for one line.  mainline calls this
 * tty_line_name() and keeps it static in tty_io.c alongside pty_line_name();
 * only the non-pty arm is needed here, because DCL's staged uart drivers are
 * the only thing that registers lines and none of them is a pty.
 */
static void dcl_tty_line_name(struct tty_driver* driver, unsigned index,
			      char* p, unsigned cap)
{
	snprintf(p, cap, "%s%d", driver->name,
		 (int)(index + (unsigned)driver->name_base));
}

/*
 * dcl_release_tty() -- the kref tail for a struct tty_struct, the argument
 * tty_kref_put() below hands to kref_put().
 *
 * mainline's release_tty() calls tty->ops->shutdown, saves the termios back
 * into the driver's slot (tty_save_termios), removes the tty from the
 * driver's table, detaches the port and the link, and finally drops the
 * driver reference that tty_standard_install() took.  Of those, the two that
 * have somewhere to go in this build are the table slot and the driver
 * reference, and they are the two here -- ops->shutdown has no tty->ops
 * table installed yet (nothing allocates a struct tty_struct until tty_io.c
 * does, at stage 5), tty_save_termios writes through driver->termios which
 * tty_register_device_attr() has just freed the entry of, and the port/link
 * detach is the file-table half.  Freeing the struct itself is what the
 * kref reaching zero means.
 */
static void dcl_release_tty(struct kref* kref)
{
	struct tty_struct* tty = container_of(kref, struct tty_struct, kref);
	struct tty_driver* driver = tty->driver;
	unsigned index = (unsigned)tty->index;

	if (driver && driver->ttys && index < driver->num) {
		driver->ttys[index] = NULL;
		tty_driver_kref_put(driver);
	}
	kfree(tty);
}

/* ------------------------------------------------------- tty and driver ops */

const char* tty_name(const struct tty_struct* tty)
{
	if (!tty) /* Hmm.  NULL pointer.  That's fun. */
		return "NULL tty";
	return tty->name;
}

const char* tty_driver_name(const struct tty_struct* tty)
{
	if (!tty || !tty->driver)
		return "";
	return tty->driver->name;
}

void tty_kref_put(struct tty_struct* tty)
{
	if (tty)
		kref_put(&tty->kref, dcl_release_tty);
}

/*
 * tty_init_termios() -- mainline's tty_io.c:1244, verbatim.  The lazy saved
 * termios half matters: a driver that has been closed and reopened gets the
 * settings it was left with rather than the driver's defaults, which is the
 * whole point of the per-index array tty_register_device_attr() frees entries
 * out of when a minor is reused.
 */
void tty_init_termios(struct tty_struct* tty)
{
	struct ktermios* tp;
	int idx = tty->index;

	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS)
		tty->termios = tty->driver->init_termios;
	else {
		/* Check for lazy saved data */
		tp = tty->driver->termios[idx];
		if (tp != NULL) {
			tty->termios = *tp;
			tty->termios.c_line = tty->driver->init_termios.c_line;
		} else
			tty->termios = tty->driver->init_termios;
	}
	/* Compatibility until drivers always set this */
	tty->termios.c_ispeed = tty_termios_input_baud_rate(&tty->termios);
	tty->termios.c_ospeed = tty_termios_baud_rate(&tty->termios);
}

/*
 * tty_standard_install() -- mainline's tty_io.c:1271, verbatim: init the
 * termios, take the driver reference this file's release drops, count the
 * open, and publish the tty in the driver's table.  Called from uart_install()
 * in serial_core.c, so it is on the live path, not just the link path.
 */
int tty_standard_install(struct tty_driver* driver, struct tty_struct* tty)
{
	tty_init_termios(tty);
	tty_driver_kref_get(driver);
	tty->count++;
	driver->ttys[tty->index] = tty;
	return 0;
}

/*
 * tty_wakeup() -- mainline's tty_io.c:507, verbatim.  The flag-gated ldisc
 * callback is what a driver means by "I have drained, more can come"; the
 * unconditional wake after it is what unblocks tty_wait_until_sent().
 */
void tty_wakeup(struct tty_struct* tty)
{
	struct tty_ldisc* ld;

	if (test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) {
		ld = tty_ldisc_ref(tty);
		if (ld) {
			if (ld->ops->write_wakeup)
				ld->ops->write_wakeup(tty);
			tty_ldisc_deref(ld);
		}
	}
	wake_up_interruptible_poll(&tty->write_wait, EPOLLOUT);
}

/*
 * tty_write_unlock() / tty_write_lock() -- mainline's tty_io.c:931, verbatim
 * in both directions.  mutex_trylock() is `(1)` and
 * mutex_lock_interruptible() is `(0)` here (mutex.h), so the blocking arm
 * cannot fail and ndelay can only return -EAGAIN when the trylock lost --
 * which it never does, so the -ERESTARTSYS arm below is unreachable too.
 * That is the mutex contract on this build, not a choice made here; the
 * bodies are kept as they are so that the day the mutex becomes real the
 * code has not already been thinned out.
 */
void tty_write_unlock(struct tty_struct* tty)
{
	mutex_unlock(&tty->atomic_write_lock);
	wake_up_interruptible_poll(&tty->write_wait, EPOLLOUT);
}

int tty_write_lock(struct tty_struct* tty, bool ndelay)
{
	if (!mutex_trylock(&tty->atomic_write_lock)) {
		if (ndelay)
			return -EAGAIN;
		if (mutex_lock_interruptible(&tty->atomic_write_lock))
			return -ERESTARTSYS;
	}
	return 0;
}

/*
 * tty_send_xchar() -- mainline's tty_io.c:1137, with one substitution: the
 * exported start_tty()/stop_tty() become __start_tty()/__stop_tty().  The
 * exported pair exists in tty_io.c and differs only by taking
 * `guard(spinlock_irqsave)(&tty->flow.lock)` around the call -- the lock
 * this build never takes, so calling through them would add a link
 * dependency on two symbols whose entire body is the same call plus a no-op
 * guard.  The control flow of the function is mainline's.
 */
int tty_send_xchar(struct tty_struct* tty, u8 ch)
{
	bool was_stopped = tty->flow.stopped;

	if (tty->ops->send_xchar) {
		down_read(&tty->termios_rwsem);
		tty->ops->send_xchar(tty, ch);
		up_read(&tty->termios_rwsem);
		return 0;
	}

	if (tty_write_lock(tty, false) < 0)
		return -ERESTARTSYS;

	down_read(&tty->termios_rwsem);
	if (was_stopped)
		__start_tty(tty);
	tty->ops->write(tty, &ch, 1);
	if (was_stopped)
		__stop_tty(tty);
	up_read(&tty->termios_rwsem);
	tty_write_unlock(tty);
	return 0;
}

/*
 * __stop_tty() / __start_tty() -- mainline's tty_io.c:740 and :770, verbatim.
 * The `stopped` guard is the load-bearing part: stop_tty() may be called from
 * any context and repeatedly, and the driver's ->stop() is only supposed to
 * run on the transition.  __start_tty() additionally wakes the ldisc, which
 * is why it is the one with tty_wakeup() after it.
 */
void __stop_tty(struct tty_struct* tty)
{
	if (tty->flow.stopped)
		return;
	tty->flow.stopped = true;
	if (tty->ops->stop)
		tty->ops->stop(tty);
}

void __start_tty(struct tty_struct* tty)
{
	if (!tty->flow.stopped || tty->flow.tco_stopped)
		return;
	tty->flow.stopped = false;
	if (tty->ops->start)
		tty->ops->start(tty);
	tty_wakeup(tty);
}

/*
 * tty_hangup() -- mainline's tty_io.c:673, verbatim: queue the work item.
 * Two things about this build make it look different from what mainline
 * does while staying the same line of source.  schedule_work() runs the item
 * inline (workqueue.h:56 calls work->func directly), so mainline's
 * *asynchronous* hangup happens synchronously here; and the item is
 * initialised by tty_init_dev(), which is tty_io.c's, so until stage 5 the
 * func is NULL and this is a no-op.  Both are properties of the pieces
 * around it, and both go away when tty_io.c lands.
 */
void tty_hangup(struct tty_struct* tty)
{
	schedule_work(&tty->hangup_work);
}

/*
 * tty_vhangup() -- mainline does this synchronously by calling __tty_hangup()
 * directly instead of going through the work item.  Same deal as above, one
 * step further: __tty_hangup() is ~80 lines that walk *this process's* file
 * table swapping f_ops for hung_up_tty_fops and then signal the session, and
 * neither the file table nor the signals exist.  Going through the work item
 * keeps the synchrony that does exist -- DCL's schedule_work() is inline --
 * and keeps the one call that will do the real work when tty_io.c provides
 * it, which is the honest shape: nothing has been hung up because there is
 * nothing open to hang up.
 */
void tty_vhangup(struct tty_struct* tty)
{
	schedule_work(&tty->hangup_work);
}

/*
 * tty_hung_up_p() -- mainline's tty_io.c:734 is
 *
 *     return (filp && filp->f_op == &hung_up_tty_fops);
 *
 * Both halves of that comparison are missing and for the same reason:
 * hung_up_tty_fops is the table __tty_hangup() installs *on* an open file,
 * and the list of open files is tty_io.c's.  Nothing in this build can have
 * had its f_ops swapped, so the answer is 0 for every file -- which is what
 * mainline would say too, for a file nobody has hung up.  Returning it
 * directly rather than comparing against a table this file would have to
 * invent keeps the day stage 5 adds the swap to one place: this comment.
 */
int tty_hung_up_p(struct file* filp)
{
	(void)filp;
	return 0;
}

/*
 * tty_check_change() -- mainline's drivers/tty/tty_jobctrl.c.  The whole
 * body is gated by one line, `if (current->signal->tty != tty) return 0;`,
 * and everything after it is "this is our controlling terminal, so refuse to
 * change its settings from the background": SIGTTOU to the foreground group,
 * a wait for the group to stop, a recheck against tasklist_lock.  DCL has no
 * session, no foreground process group and no signals to send, so every
 * process answers the gate the way mainline answers it for one that is not
 * the terminal's session leader.  Returning 0 means the ioctls tty_ioctl.c
 * guards with it -- TIOCSPGRP, TIOCSCTTY, TCSETS from the background -- go
 * ahead, which is the same thing they would do with a single-process
 * session that owns its only terminal.
 *
 *   upstream  drivers/tty/tty_jobctrl.c:205
 */
int tty_check_change(struct tty_struct* tty)
{
	(void)tty;
	return 0;
}

/* ------------------------------------------------------- driver registration */

/*
 * destruct_tty_driver() -- mainline's tty_io.c:3371, the kref tail for a
 * struct tty_driver.
 *
 * Two calls mainline makes are not here.  proc_tty_unregister_driver() has
 * no procfs to remove a file from.  And the tty_cdev_add() that
 * TTY_DRIVER_DYNAMIC_ALLOC would pair with never ran, so the cdev_del() that
 * goes with it has nothing to remove: DCL's devfs slot the node was created
 * with *is* the binding (linux_cdev_shim.c), which is why the attach arm is
 * absent rather than stubbed -- a removal with nothing removed would be a
 * declaration of cdev_del() that only this file could ever satisfy.
 */
static void destruct_tty_driver(struct kref* kref)
{
	struct tty_driver* driver = container_of(kref, struct tty_driver, kref);
	unsigned int i;
	struct ktermios* tp;

	if (driver->flags & TTY_DRIVER_INSTALLED) {
		for (i = 0; i < driver->num; i++) {
			tp = driver->termios ? driver->termios[i] : NULL;
			if (tp) {
				driver->termios[i] = NULL;
				kfree(tp);
			}
			if (!(driver->flags & TTY_DRIVER_DYNAMIC_DEV))
				tty_unregister_device(driver, i);
		}
	}
	kfree(driver->cdevs);
	kfree(driver->ports);
	kfree(driver->termios);
	kfree(driver->ttys);
	kfree(driver);
}

void tty_driver_kref_put(struct tty_driver* driver)
{
	kref_put(&driver->kref, destruct_tty_driver);
}

/*
 * __tty_alloc_driver() -- mainline's tty_io.c:3326, verbatim, including the
 * two flag tests that decide what gets allocated.  A driver with
 * TTY_DRIVER_DEVPTS_MEM keeps no tty or termios array (the pty pair is
 * created on demand and the devpts node owns the minor), and one with
 * TTY_DRIVER_DYNAMIC_ALLOC keeps no per-line port array, because a single
 * cdev covers the whole range.  The failure path frees what exists -- kfree
 * of a NULL pointer is defined, which is why mainline can list all four
 * unconditionally.
 */
struct tty_driver* __tty_alloc_driver(unsigned int lines,
				      struct module* owner,
				      unsigned long flags)
{
	struct tty_driver* driver;
	unsigned int cdevs = 1;
	int err;

	if (!lines || (flags & TTY_DRIVER_UNNUMBERED_NODE && lines > 1))
		return ERR_PTR(-EINVAL);

	driver = kzalloc_obj(*driver);
	if (!driver)
		return ERR_PTR(-ENOMEM);

	kref_init(&driver->kref);
	driver->num = lines;
	driver->owner = owner;
	driver->flags = flags;

	if (!(flags & TTY_DRIVER_DEVPTS_MEM)) {
		driver->ttys = kzalloc_objs(*driver->ttys, lines);
		driver->termios = kzalloc_objs(*driver->termios, lines);
		if (!driver->ttys || !driver->termios) {
			err = -ENOMEM;
			goto err_free_all;
		}
	}

	if (!(flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		driver->ports = kzalloc_objs(*driver->ports, lines);
		if (!driver->ports) {
			err = -ENOMEM;
			goto err_free_all;
		}
		cdevs = lines;
	}

	driver->cdevs = kzalloc_objs(*driver->cdevs, cdevs);
	if (!driver->cdevs) {
		err = -ENOMEM;
		goto err_free_all;
	}

	return driver;

err_free_all:
	kfree(driver->ports);
	kfree(driver->ttys);
	kfree(driver->termios);
	kfree(driver->cdevs);
	kfree(driver);
	return ERR_PTR(err);
}

/*
 * tty_register_driver() -- mainline's tty_io.c:3425 with three things
 * removed, none of which a caller can see.
 *
 * The chrdev region.  mainline allocates a major and a minor range from a
 * table of what is taken (alloc_chrdev_region, or register_chrdev_region for
 * a fixed major) and fails the registration if the range is spoken for.  DCL
 * has no such table: the devfs slot lookup keyed by devt
 * (linux_cdev_shim.c, _dcl_devs[]) is what a node is found by, and the node
 * name -- built by dcl_tty_line_name() from driver->name -- is what two
 * drivers would actually collide on.  So the major is a counter in this file,
 * starting at 4: the first driver that asks for one gets the number mainline's
 * static tty devices sit at, which is the number a caller that left major at 0
 * was in effect asking for, and each later one gets its own.  A driver that
 * set a major keeps it and is handed no check, because there is no table to
 * disagree with.
 *
 * The workqueue.  mainline creates a per-driver flip workqueue unless
 * TTY_DRIVER_NO_WORKQUEUE, and links each port's flip work to it.  DCL's
 * work_struct runs inline (workqueue.h:56), so a queue to run it on is a
 * queue of one and flip_wq would be set only to be destroyed; it is left
 * NULL, which is also what mainline leaves it for a driver that opted out.
 *
 * proc_tty_register_driver().  There is no procfs; see destruct above.
 *
 * The list add and the INSTALLED flag are mainline's, in mainline's order --
 * add first, then instantiate the devices, then mark it installed, so that a
 * failure partway leaves a driver on the list but not installed, and
 * destruct_tty_driver() will not try to unregister devices that were never
 * registered.
 */
int tty_register_driver(struct tty_driver* driver)
{
	int i;
	int error = 0;
	struct device* d;

	if (!driver->major) {
		static unsigned int dcl_next_tty_major = 4;

		driver->major = (int)dcl_next_tty_major++;
	}

	list_add(&driver->tty_drivers, &tty_drivers);

	if (!(driver->flags & TTY_DRIVER_DYNAMIC_DEV)) {
		for (i = 0; i < (int)driver->num; i++) {
			d = tty_register_device_attr(driver, (unsigned)i,
						     NULL, NULL, NULL);
			if (IS_ERR(d)) {
				error = (int)PTR_ERR(d);
				goto err_unreg_devs;
			}
		}
	}

	driver->flags |= TTY_DRIVER_INSTALLED;
	return 0;

err_unreg_devs:
	for (i--; i >= 0; i--)
		tty_unregister_device(driver, (unsigned)i);
	list_del(&driver->tty_drivers);
	return error;
}

/*
 * tty_unregister_driver() -- mainline's tty_io.c:3509 minus the
 * unregister_chrdev_region() the region above did not reserve and the
 * destroy_workqueue() that was never created.  The devices are deliberately
 * *not* torn down here, exactly as mainline does not: the INSTALLED flag
 * stays set and destruct_tty_driver() drops the nodes when the last
 * reference goes, so a caller that unregisters and then still holds the
 * driver can put it once and be done.
 */
void tty_unregister_driver(struct tty_driver* driver)
{
	list_del(&driver->tty_drivers);
}

/*
 * tty_register_device_attr() -- mainline's tty_io.c:3225, with the device
 * model half swapped.
 *
 * mainline kzallocs a struct device, fills in devt/class/parent/release/
 * groups, calls device_register() (which publishes it under sysfs), then
 * attaches the cdev for that minor and rings a KOBJ_ADD uevent.  DCL's
 * device model is device_create() -> _dcl_devs[] + a /dev node
 * (linux_cdev_shim.c), which does the allocation, the name, the devt and the
 * node in one step -- so the whole first half collapses into one call and
 * the cdev attach has no counterpart: there is no file_operations table per
 * minor to attach, only the devfs slot the node was registered with.
 *
 * The pieces kept are the ones that are this function's own job rather than
 * the device model's: the index bounds check with mainline's message, the
 * name, and the freeing of a saved termios entry.  That last one is the
 * reason the array is walked at all -- it clears the lazy state out of a
 * minor that is about to be handed to a *different* tty, so the new one
 * starts from the driver's defaults instead of inheriting the previous
 * occupant's speed (tty_init_termios() above reads it).
 *
 * attr_grp is DCL's, unused: mainline hands it to the device so sysfs can
 * publish the attribute group, and there is no sysfs.
 */
struct device* tty_register_device_attr(struct tty_driver* driver,
					unsigned index,
					struct device* device,
					void* drvdata,
					const struct attribute_group** attr_grp)
{
	char name[64];
	dev_t devt = MKDEV(driver->major, driver->minor_start) + index;
	struct ktermios* tp;
	struct device* dev;

	(void)attr_grp;

	if (index >= driver->num) {
		pr_err("%s: Attempt to register invalid tty line number (%u)\n",
		       driver->name, index);
		return ERR_PTR(-EINVAL);
	}

	dcl_tty_line_name(driver, index, name, sizeof(name));

	if (!(driver->flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		tp = driver->termios ? driver->termios[index] : NULL;
		if (tp) {
			driver->termios[index] = NULL;
			kfree(tp);
		}
	}

	dev = device_create(&tty_class, device, devt, drvdata, "%s", name);
	if (IS_ERR(dev))
		return dev;

	return dev;
}

/*
 * tty_unregister_device() -- mainline's tty_io.c:3312 with the same swap
 * as above: device_destroy() in place of device_del() + put_device(), which
 * is what releases the slot and removes the /dev node.  The cdev arm that
 * follows in mainline is gone for the reason destruct_tty_driver() gives --
 * tty_cdev_add() never ran, so there is no cdev to drop and no declaration
 * of cdev_del() that anything here could put to work.
 */
void tty_unregister_device(struct tty_driver* driver, unsigned index)
{
	dev_t devt = MKDEV(driver->major, driver->minor_start) + index;

	device_destroy(&tty_class, devt);
}
