#ifndef __LINUX_PRINTK_H__
#define __LINUX_PRINTK_H__

#include <Drivers/uart.h>
#include <aucon.h>
#include <stdarg.h>		/* va_list -- struct va_format below */

/*
 * <linux/stringify.h> is not a mainline printk.h include -- mainline reaches
 * __stringify() from <linux/device.h>, which drm_print.h also includes. DCL's
 * device.h is a stub that stops at kobject/sysfs/property and carries no such
 * chain, so drm_WARN_ON() (drm_print.h:789) expands to
 *
 *     "drm_WARN_ON(" __stringify(x) ")"
 *
 * with __stringify() undefined: it reads as an implicit function call sitting
 * between two string literals, and the diagnostic is "expected ')'" at the
 * *caller's* if-statement -- no mention of __stringify() anywhere in it.
 *
 * printk.h is where the include lands because it is the one header every
 * print-macro user in this tree already pulls, and __stringify()'s only users
 * are print and sysfs macros (serial_core.h, sysfs.h, linkage.h,
 * tty_ldisc.h), each of which currently survives only by the accident of
 * which TU includes them.
 */
#include <linux/stringify.h>

/*
 * struct va_format -- mainline's at printk.h:84, and <drm/drm_print.h> needs
 * it in two ways: :176 declares a printer callback taking `struct va_format *`
 * and :216 *builds* one, `struct va_format vaf = { .fmt = fmt, .va = va };`,
 * so an incomplete type there is "variable has incomplete type" rather than
 * an unknown name -- the struct is named but never defined anywhere in this
 * tree.  Both fields are required by that initializer.
 */
struct va_format {
	const char *fmt;
	va_list *va;
};

#define KERN_EMERG    ""
#define KERN_ALERT    ""
#define KERN_CRIT     ""
#define KERN_ERR      ""
#define KERN_WARNING  ""
#define KERN_NOTICE   ""
#define KERN_INFO     ""
#define KERN_DEBUG    ""

#define printk(fmt, ...) UARTDebugOut(fmt, ##__VA_ARGS__)

#define pr_emerg(fmt, ...)   printk(fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...)   printk(fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...)    printk(fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)     printk(fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)    printk(fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...)  printk(fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)    printk(fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)   do {} while (0)

#define dev_emerg(dev, fmt, ...)  printk(fmt, ##__VA_ARGS__)
#define dev_alert(dev, fmt, ...)  printk(fmt, ##__VA_ARGS__)
#define dev_crit(dev, fmt, ...)   printk(fmt, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...)    printk(fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...)   printk(fmt, ##__VA_ARGS__)
#define dev_notice(dev, fmt, ...) printk(fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...)   printk(fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...)    do {} while (0)

/*
 * The _ratelimited spelling: same output, minus the flood.  mainline routes
 * these through a ratelimit_state so a driver spinning on a fault cannot fill
 * the log; DCL has no ratelimit machinery, and the two call sites are
 * serial8250_set_termios()'s "LSR safety check engaged!" (:2306) and the
 * repeated-warning path at :2346 -- both once-per-change rather than
 * once-per-byte.  So the body is dev_info/dev_warn exactly, and what is *not*
 * claimed is the limiting: if a call site ever spins, the right fix is a
 * ratelimit, not a comment saying these were limited.
 */
#define dev_info_ratelimited(dev, fmt, ...)  dev_info((dev), fmt, ##__VA_ARGS__)
#define dev_warn_ratelimited(dev, fmt, ...)   dev_warn((dev), fmt, ##__VA_ARGS__)

/*
 * oops_in_progress is set *before* the message goes out, not after: the whole
 * point is that serial8250_console_write() reads it while printing this very
 * panic, and a console write that happens after the fact protects nothing.
 * Declared in <linux/kernel.h>, defined in DCL/linux_irq_shim.c.
 */
extern int oops_in_progress;

#define panic(fmt, ...) do { oops_in_progress = 1; \
	printk("PANIC: " fmt, ##__VA_ARGS__); while(1); } while(0)


/*
 * DUMP_PREFIX_* and hex_dump_to_buffer -- mainline spells these just above
 * its print_hex_dump() in <linux/printk.h>, and lib/seq_buf.c's
 * seq_buf_hex_dump() reaches them through <linux/printk.h> alone.  Two
 * consumers: the switch at seq_buf.c:423/:427 selects the per-row prefix, and
 * :419 calls hex_dump_to_buffer, whose body is Vendored/lib/hexdump.c.
 *
 * DCL has no print_hex_dump() and does not need one -- nothing in this tree
 * calls it, and adding a declaration with no definition is how a link error
 * gets found late rather than early.
 */
enum {
	DUMP_PREFIX_NONE,
	DUMP_PREFIX_ADDRESS,
	DUMP_PREFIX_OFFSET
};
extern int hex_dump_to_buffer(const void *buf, size_t len, int rowsize,
			      int groupsize, char *linebuf, size_t linebuflen,
			      bool ascii);

#define dev_notice_ratelimited(dev, fmt, ...)  dev_notice((dev), fmt, ##__VA_ARGS__)

/*
 * dev_err_probe(dev, err, fmt, ...) -- "report this probe failure, unless it
 * is -EPROBE_DEFER, which is normal".
 *
 * The whole behaviour is that one exception: mainline suppresses the
 * -EPROBE_DEFER message because a deferred probe is the driver asking to be
 * called again, not a fault, and printing every one would bury real errors.
 * The value comes back unchanged either way -- the caller at serial_core.c:3575
 * does `return dev_err_probe(...)`, so what matters is that the *original*
 * error reaches the caller, not what was printed.
 *
 * Statement expression because it has both jobs in one call: evaluate the
 * error once, print conditionally, yield the error.  `dev` is dropped by
 * dev_err below exactly as it is for every other dev_* in this header.
 */
#define dev_err_probe(dev, err, fmt, ...)				\
	({								\
		int __e = (err);					\
		if (__e != -EPROBE_DEFER)				\
			dev_err((dev), fmt, ##__VA_ARGS__);		\
		__e;							\
	})
#endif
