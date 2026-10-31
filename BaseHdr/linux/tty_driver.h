#ifndef _LINUX_TTY_DRIVER_H
#define _LINUX_TTY_DRIVER_H

/*
 * DCL <linux/tty_driver.h> -- struct tty_driver and struct tty_operations.
 *
 * Ported from mainline v7.2, with two classes of change:
 *
 *  - __randomize_layout dropped from both structs. It is a GCC plugin that
 *    shuffles field order per build to defeat struct-prediction attacks; DCL
 *    has no plugin and no adversary, and a struct whose layout differs between
 *    translation units would be a correctness bug, not a hardening feature.
 *
 *  - The CONFIG_PROC_FS and CONFIG_CONSOLE_POLL blocks are written out in the
 *    "present" direction (proc registration is declared, the poll ops are
 *    not). proc_tty_register_driver() is one of the calls tty_register_driver()
 *    makes, so it needs a symbol; poll_* is a console-polling hook nothing in
 *    the staged set installs.
 *
 * `struct cdev **cdevs` is the field that makes /dev/ttyS0 possible at all:
 * tty_io.c's cdev bridge looks the driver up through it, and serial_core.c's
 * uart_add_one_port() eventually lands here. It is an array -- one cdev per
 * line -- because a serial driver can have as many ports as minors.
 */

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/bitops.h>	/* BIT() for the tty_driver_flag enum */
#include <linux/termios.h>
#include <linux/seq_file.h>
#include <linux/module.h>	/* THIS_MODULE for tty_alloc_driver() */

struct tty_struct;
struct tty_driver;
struct tty_port;
struct serial_icounter_struct;
struct serial_struct;
struct cdev;
struct proc_dir_entry;
struct device;
struct module;
struct workqueue_struct;
struct attribute_group;

enum tty_driver_flag {
	TTY_DRIVER_INSTALLED		= BIT(0),
	TTY_DRIVER_RESET_TERMIOS	= BIT(1),
	TTY_DRIVER_REAL_RAW		= BIT(2),
	TTY_DRIVER_DYNAMIC_DEV		= BIT(3),
	TTY_DRIVER_DEVPTS_MEM		= BIT(4),
	TTY_DRIVER_HARDWARE_BREAK	= BIT(5),
	TTY_DRIVER_DYNAMIC_ALLOC	= BIT(6),
	TTY_DRIVER_UNNUMBERED_NODE	= BIT(7),
	TTY_DRIVER_NO_WORKQUEUE		= BIT(8),
};

enum tty_driver_type {
	TTY_DRIVER_TYPE_SYSTEM,
	TTY_DRIVER_TYPE_CONSOLE,
	TTY_DRIVER_TYPE_SERIAL,
	TTY_DRIVER_TYPE_PTY,
	TTY_DRIVER_TYPE_SCC,
	TTY_DRIVER_TYPE_SYSCONS,
};

enum tty_driver_subtype {
	SYSTEM_TYPE_TTY = 1,
	SYSTEM_TYPE_CONSOLE,
	SYSTEM_TYPE_SYSCONS,
	SYSTEM_TYPE_SYSPTMX,

	PTY_TYPE_MASTER = 1,
	PTY_TYPE_SLAVE,

	SERIAL_TYPE_NORMAL = 1,
};

struct tty_operations {
	struct tty_struct* (*lookup)(struct tty_driver* driver,
			struct file* filp, int idx);
	int  (*install)(struct tty_driver* driver, struct tty_struct* tty);
	void (*remove)(struct tty_driver* driver, struct tty_struct* tty);
	int  (*open)(struct tty_struct* tty, struct file* filp);
	void (*close)(struct tty_struct* tty, struct file* filp);
	void (*shutdown)(struct tty_struct* tty);
	void (*cleanup)(struct tty_struct* tty);
	ssize_t (*write)(struct tty_struct* tty, const u8* buf, size_t count);
	int  (*put_char)(struct tty_struct* tty, u8 ch);
	void (*flush_chars)(struct tty_struct* tty);
	unsigned int (*write_room)(struct tty_struct* tty);
	unsigned int (*chars_in_buffer)(struct tty_struct* tty);
	int  (*ioctl)(struct tty_struct* tty,
		    unsigned int cmd, unsigned long arg);
	long (*compat_ioctl)(struct tty_struct* tty,
			     unsigned int cmd, unsigned long arg);
	void (*set_termios)(struct tty_struct* tty, const struct ktermios* old);
	void (*throttle)(struct tty_struct* tty);
	void (*unthrottle)(struct tty_struct* tty);
	void (*stop)(struct tty_struct* tty);
	void (*start)(struct tty_struct* tty);
	void (*hangup)(struct tty_struct* tty);
	int (*break_ctl)(struct tty_struct* tty, int state);
	void (*flush_buffer)(struct tty_struct* tty);
	int (*ldisc_ok)(struct tty_struct* tty, int ldisc);
	void (*set_ldisc)(struct tty_struct* tty);
	void (*wait_until_sent)(struct tty_struct* tty, int timeout);
	void (*send_xchar)(struct tty_struct* tty, u8 ch);
	int (*tiocmget)(struct tty_struct* tty);
	int (*tiocmset)(struct tty_struct* tty,
			unsigned int set, unsigned int clear);
	int (*resize)(struct tty_struct* tty, struct winsize* ws);
	int (*get_icount)(struct tty_struct* tty,
				struct serial_icounter_struct* icount);
	int  (*get_serial)(struct tty_struct* tty, struct serial_struct* p);
	int  (*set_serial)(struct tty_struct* tty, struct serial_struct* p);
	void (*show_fdinfo)(struct tty_struct* tty, struct seq_file* m);
	int (*proc_show)(struct seq_file* m, void* driver);
};

struct tty_driver {
	struct kref kref;
	struct cdev** cdevs;
	struct module* owner;
	const char* driver_name;
	const char* name;
	int name_base;
	int major;
	int minor_start;
	unsigned int num;
	enum tty_driver_type type;
	enum tty_driver_subtype subtype;
	struct ktermios init_termios;
	unsigned long flags;
	struct proc_dir_entry* proc_entry;
	struct tty_driver* other;
	struct workqueue_struct* flip_wq;

	/*
	 * Pointer to the tty data structures
	 */
	struct tty_struct** ttys;
	struct tty_port** ports;
	struct ktermios** termios;
	void* driver_state;

	/*
	 * Driver methods
	 */
	const struct tty_operations* ops;
	struct list_head tty_drivers;
};

extern struct list_head tty_drivers;

struct tty_driver* __tty_alloc_driver(unsigned int lines,
		struct module* owner, unsigned long flags);
struct tty_driver* tty_find_polling_driver(char* name, int* line);

void tty_driver_kref_put(struct tty_driver* driver);

/*
 * tty_alloc_driver -- allocate tty driver. Returns a PTR-encoded error (use
 * IS_ERR() and friends), which is why the macro exists rather than a plain
 * function: THIS_MODULE is (void*)0 in DCL, so the wrapper costs nothing but
 * keeps the call sites byte-identical to mainline.
 */
#define tty_alloc_driver(lines, flags) \
		__tty_alloc_driver(lines, THIS_MODULE, flags)

static inline struct tty_driver* tty_driver_kref_get(struct tty_driver* d)
{
	kref_get(&d->kref);
	return d;
}

static inline void tty_set_operations(struct tty_driver* driver,
		const struct tty_operations* op)
{
	driver->ops = op;
}

int tty_register_driver(struct tty_driver* driver);
void tty_unregister_driver(struct tty_driver* driver);
struct device* tty_register_device(struct tty_driver* driver, unsigned index,
		struct device* dev);
struct device* tty_register_device_attr(struct tty_driver* driver,
		unsigned index, struct device* device, void* drvdata,
		const struct attribute_group** attr_grp);
void tty_unregister_device(struct tty_driver* driver, unsigned index);

void proc_tty_register_driver(struct tty_driver*);
void proc_tty_unregister_driver(struct tty_driver*);

#endif /* _LINUX_TTY_DRIVER_H */
