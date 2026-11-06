#ifndef __LINUX_CONSOLE_H__
#define __LINUX_CONSOLE_H__

/*
 * DCL <linux/console.h> -- the console registration surface serial_core and
 * 8250 use.
 *
 * The load-bearing decision here: register_console() records a console so
 * that console_is_registered() answers true and callers stop asking -- but it
 * does *not* hand the UART over. Xeneva's boot log is written to the same
 * serial port by UARTDebugOut(), and letting mainline's uart console drive
 * those registers as well would mean two writers interleaving characters at
 * register level: a garbled console rather than a working one, and a fault
 * that would only show up under load. Native output keeps ownership; the
 * registration API is satisfied because serial_core.c:2574 calls
 * register_console() and then expects console_is_registered() to be true, and
 * because uart_add_one_port() returns through it (serial_core.c:2991, :2999).
 *
 * console_lock()/console_unlock() are the printk console semaphore. There is
 * exactly one writer here, so the lock is uncontended by construction -- but
 * they must exist as calls, because serial_core.c:2530 and tty_io.c:3589 take
 * them around tty-device lookups, and a missing symbol would take the whole
 * registration path down.
 *
 * console_suspend() is called from uart_suspend_port()/uart_resume_port();
 * Xeneva has no suspend path, so it is a no-op that keeps the call site
 * readable rather than sending anyone to find out why the symbol is missing.
 *
 * console_suspend_enabled mirrors mainline's default for a kernel without
 * CONFIG_PM_SLEEP: false. serial_core.c:2319 branches on it -- with false the
 * suspend path stops RX and returns early, which is the behaviour that makes
 * sense for a console that is never suspended.
 */

struct tty_port;
struct device;
struct tty_device;

struct console {
	char name[16];
	void (*write)(struct console* con, const char* s, unsigned int count);
	/* tty_io.c:3570 region resolves a console back to its tty through this */
	struct tty_device* (*device)(struct console* con, int* index);
	int (*setup)(struct console* con, char* options);
	int (*exit)(struct console* con);
	int (*match)(struct console* con, struct device** dev, int idx);
	void (*cleanup)(struct console* con);
	void* data;
	int index;
	int cflag;
	unsigned int ispeed;
	unsigned int ospeed;
	unsigned int flags;
};

/* mainline include/linux/console.h flag values */
#define CON_PRINTBUFFER 0x00000001
#define CON_CONSDEV     0x00000002
#define CON_ENABLED     0x00000004
#define CON_BOOT        0x00000008
#define CON_ANYTIME     0x00000010
#define CON_BUILTIN     0x00000020
#define CON_SUSPENDED   0x00000040
#define CON_DRIVERBLANK 0x00000080
#define CON_SYSFS       0x00000100
/* tty_io.c:3575 tests this to reject atomic consoles. DCL registers no nbcon
 * console, so the bit only has to exist and not collide with the others. */
#define CON_NBCON       0x00000200

extern int console_suspend_enabled;

int register_console(struct console* con);
int unregister_console(struct console* con);
int console_is_registered(struct console* con);

void console_lock(void);
void console_unlock(void);
void console_trylock(void);
void console_suspend(struct console* con);


/*
 * console_is_registered_locked() -- the same answer as
 * console_is_registered(), taken with the console list lock held.
 *
 * mainline splits these because the unlocked one has to take a lock itself,
 * and calling it while already holding it would deadlock.  DCL's
 * console_lock()/console_unlock() are empty (DCL/linux_irq_shim.c) and the
 * registry is a plain array scan, so there is no lock to re-enter and no
 * separate locked path to maintain -- one body, two names, and the reason the
 * second one does not take a lock is that neither does the first.
 *
 * The caller is uart_console_registered_locked() in serial_core.h:1376, which
 * asks whether the port's console is on the list while already holding the
 * port lock.  Answering the same thing twice under two names is fine;
 * answering it differently would be the bug.
 */
static inline bool console_is_registered_locked(struct console* con)
{
	return console_is_registered(con) != 0;
}

/*
 * console_resume() -- the other half of console_suspend() above, which this
 * header already declares.  It is called with console_suspend_enabled
 * already tested (serial_core.c:2419) and its job upstream is to undo the
 * CON_SUSPENDED/CON_ENABLED bookkeeping suspend() did.
 *
 * DCL has no suspend path -- console_suspend() is a body in
 * DCL/linux_irq_shim.c:570 saying so, next to console_lock() -- so there is
 * no suspended state to clear, and clearing one would mean writing CON_ENABLED
 * back on a console that was never taken off.  The declaration is here because
 * mainline's is, the body goes beside console_suspend()'s so the pair stays
 * readable together, and both agree with the no-op lock functions in the same
 * shim: mainline's calls are paired, and so are DCL's.
 */
void console_resume(struct console* con);
#endif /* __LINUX_CONSOLE_H__ */
