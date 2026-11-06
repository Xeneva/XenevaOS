#ifndef _LINUX_TTY_LDISC_H
#define _LINUX_TTY_LDISC_H

/*
 * DCL <linux/tty_ldisc.h> -- the line discipline interface, plus the
 * ld_semaphore it is serialised on.
 *
 * Ported from mainline v7.2. The line discipline is the layer that decides
 * what a byte stream *means*: n_tty (cooked mode) turns 0x7f into a backspace
 * and a line into an input record, while raw mode hands bytes through
 * untouched. tty_ldisc.c registers the set and tty_ldisc_ref()/deref() guard
 * a caller's use of one while it can be swapped underneath (TIOCSETD).
 *
 * The struct definitions are exact -- tty_ldisc_ops is the table n_tty.c
 * fills in, and every member is a member a ported file either assigns or
 * calls. The optional hooks are all present as function pointers; mainline
 * explicitly documents them as allowed to be NULL, and tty_buffer.c:397/428
 * tests receive_buf and lookahead_buf for exactly that reason.
 *
 * ld_semaphore lives here because struct tty_struct embeds it by value
 * (tty.h), so the complete type has to exist before tty.h can. It is
 * mainline's layout with raw_spinlock_t spelled spinlock_t: DCL's spinlock_t
 * *is* the raw lock (typedef int spinlock_t, kernel.h:43), so the distinction
 * mainline draws between raw and non-raw has no referent here.
 *
 * init_ldsem() keeps mainline's lock_class_key argument.  It used to drop it
 * -- the reasoning was that there is no lockdep to give a key to -- but the
 * argument belongs to tty_ldsem.c's *definition*, which is vendored and
 * cannot be edited to match, so the declaration follows it instead.  The key
 * it passes is a per-call-site static, exactly as mainline's macro mints one,
 * and lockdep.h's `struct lock_class_key { }` makes it an object with an
 * address rather than a notion.  That is all the argument is ever asked to
 * be: lockdep_set_class() evaluates it and drops it.
 */

#include <linux/fs.h>
/*
 * spinlock.h, not kernel.h: the wait_lock below is a spinlock_t and
 * tty_ldisc.c locks and unlocks the ldisc table with the raw_ spelling from
 * that header (:45, :112).  This header is included by <linux/tty.h> at :49,
 * and tty_ldisc.c includes tty.h at :7 -- so putting it here is what makes
 * the lock API visible at :45 without editing the vendored source to add an
 * include mainline happens to get from somewhere else.
 */
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/termios.h>
#include <linux/rwsem.h>

struct tty_struct;
struct module;

/* --- the semaphore serialising line discipline changes ----------------- */
struct ld_semaphore {
	atomic_long_t count;
	spinlock_t wait_lock;
	unsigned int wait_readers;
	struct list_head read_wait;
	struct list_head write_wait;
};

/*
 * lockdep.h for struct lock_class_key, which init_ldsem()'s expansion
 * declares a static object of -- a pointer in the prototype would have
 * needed only a forward tag, but `static struct lock_class_key __key;` needs
 * the complete type.  Three arguments because tty_ldsem.c:57 defines it with
 * three, and the vendored definition is what the declaration answers to.
 */
#include <linux/lockdep.h>

void __init_ldsem(struct ld_semaphore* sem, const char* name,
		  struct lock_class_key* key);

#define init_ldsem(sem)						\
	do {								\
		static struct lock_class_key __key;			\
									\
		__init_ldsem((sem), #sem, &__key);			\
	} while (0)

int ldsem_down_read(struct ld_semaphore* sem, long timeout);
int ldsem_down_read_trylock(struct ld_semaphore* sem);
void ldsem_up_read(struct ld_semaphore* sem);
int ldsem_down_write(struct ld_semaphore* sem, long timeout);
int ldsem_down_write_trylock(struct ld_semaphore* sem);
void ldsem_up_write(struct ld_semaphore* sem);
int ldsem_down_read_failed(struct ld_semaphore* sem);
int ldsem_down_write_failed(struct ld_semaphore* sem);

/*
 * The nested pair, exactly as mainline splits them at
 * include/linux/tty_ldisc.h:45.  It matters which arm exists: tty_ldsem.c
 * puts both *definitions* inside `#ifdef CONFIG_DEBUG_LOCK_ALLOC` (:398-413),
 * so with lockdep off there is no function to declare.  Declaring one
 * anyway compiled fine and failed at link, on a symbol only tty_ldisc.c:295
 * names -- the plain declaration never notices that the definition it
 * promises is conditional.
 *
 * The #else arm is mainline's, and it is a macro rather than an inline for
 * the usual reason: it drops the subclass argument entirely.  With lockdep
 * off the subclass is only ever a note to lockdep about which of several
 * identical semaphores this is (LDISC_SEM_OTHER at the call site), so the
 * nested and non-nested take are the same take, and the macro says so by
 * literally rewriting one into the other.
 *
 * CONFIG_DEBUG_LOCK_ALLOC is not in <linux/autoconf.h> at all, which is what
 * sends this to the #else arm -- `#ifdef` reads absence as false, unlike
 * IS_ENABLED() in kernel.h, which reads it as a compile error.
 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
int ldsem_down_read_nested(struct ld_semaphore* sem, int subclass,
			   long timeout);
int ldsem_down_write_nested(struct ld_semaphore* sem, int subclass,
			    long timeout);
#else
# define ldsem_down_read_nested(sem, subclass, timeout)		\
	ldsem_down_read(sem, timeout)
# define ldsem_down_write_nested(sem, subclass, timeout)	\
	ldsem_down_write(sem, timeout)
#endif

/* --- the discipline interface ------------------------------------------ */
struct tty_ldisc_ops {
	char* name;
	int num;

	/*
	 * The following routines are called from above.
	 */
	int (*open)(struct tty_struct* tty);
	void (*close)(struct tty_struct* tty);
	void (*flush_buffer)(struct tty_struct* tty);
	ssize_t (*read)(struct tty_struct* tty, struct file* file, u8* buf,
			size_t nr, void** cookie, unsigned long offset);
	ssize_t (*write)(struct tty_struct* tty, struct file* file,
			 const u8* buf, size_t nr);
	int (*ioctl)(struct tty_struct* tty, unsigned int cmd,
			unsigned long arg);
	int (*compat_ioctl)(struct tty_struct* tty, unsigned int cmd,
			unsigned long arg);
	void (*set_termios)(struct tty_struct* tty, const struct ktermios* old);
	__poll_t (*poll)(struct tty_struct* tty, struct file* file,
			     struct poll_table_struct* wait);
	void (*hangup)(struct tty_struct* tty);

	/*
	 * The following routines are called from below.
	 */
	void (*receive_buf)(struct tty_struct* tty, const u8* cp,
			    const u8* fp, size_t count);
	void (*write_wakeup)(struct tty_struct* tty);
	void (*dcd_change)(struct tty_struct* tty, bool active);
	size_t (*receive_buf2)(struct tty_struct* tty, const u8* cp,
				const u8* fp, size_t count);
	void (*lookahead_buf)(struct tty_struct* tty, const u8* cp,
				 const u8* fp, size_t count);

	struct module* owner;
};

struct tty_ldisc {
	const struct tty_ldisc_ops* ops;
	struct tty_struct* tty;
};

#define MODULE_ALIAS_LDISC(ldisc) \
	MODULE_ALIAS("tty-ldisc-" __stringify(ldisc))

extern const struct seq_operations tty_ldiscs_seq_ops;

struct tty_ldisc* tty_ldisc_ref(struct tty_struct*);
void tty_ldisc_deref(struct tty_ldisc*);
struct tty_ldisc* tty_ldisc_ref_wait(struct tty_struct*);

void tty_ldisc_flush(struct tty_struct* tty);

int tty_register_ldisc(const struct tty_ldisc_ops* new_ldisc);
void tty_unregister_ldisc(const struct tty_ldisc_ops* ldisc);
int tty_set_ldisc(struct tty_struct* tty, int disc);

#endif /* _LINUX_TTY_LDISC_H */
