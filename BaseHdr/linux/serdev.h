#ifndef __LINUX_SERDEV_H__
#define __LINUX_SERDEV_H__

/*
 * DCL <linux/serdev.h> -- two functions, and mainline's own answer to both.
 *
 * serdev is the bus for serial devices that are *not* ttys: a modem, a
 * Bluetooth controller, a touch panel hanging off UART2, each claimed by a
 * client driver instead of appearing as a /dev/tty node.  It is configured
 * with CONFIG_SERIAL_DEV_CTRL_TTYPORT, and this kernel does not have that --
 * there is no device tree to say which port carries one (see <linux/of.h>),
 * no client drivers, and Xeneva's UARTs are wired to consoles.
 *
 * So this header is mainline's *#else* branch.  That is worth stating
 * precisely: the ERR_PTR(-ENODEV) below is not a DCL stand-in invented for a
 * function nobody ported, it is the answer upstream gives for exactly this
 * configuration -- the branch that runs when the option is off.  The value is
 * also the one both callers branch on, and both go the way this needs:
 *
 *   tty_port.c:206  `dev = serdev_tty_port_register(...);
 *                    if (PTR_ERR(dev) != -ENODEV) return dev;`
 *       -ENODEV fails the test, so the port falls through to
 *       tty_register_device_attr() and gets a /dev/tty node -- the correct
 *       outcome for a port with no serdev client.  Returning NULL would have
 *       taken the early return instead and created no device at all, with
 *       nothing to say why.
 *
 *   tty_port.c:233  `ret = serdev_tty_port_unregister(port);
 *                    if (ret == 0) return;`
 *       -ENODEV is not 0, so tty_unregister_device() still runs and the node
 *       goes away.  Returning 0 would have skipped it and left the device
 *       registered after the port was gone.
 *
 * The (void) casts on each parameter are the one departure from the upstream
 * text: DCL builds with -Wextra, which flags an unused parameter that a
 * kernel build does not, and a header that is quiet everywhere else should
 * not be the source of six warnings.
 *
 *   upstream  include/linux/serdev.h  (mainline v7.2), #else branch
 */

#include <linux/err.h>		/* ERR_PTR */
#include <linux/errno.h>	/* ENODEV */

struct tty_port;
struct device;
struct tty_driver;

static inline struct device* serdev_tty_port_register(struct tty_port* port,
						      struct device* host,
						      struct device* parent,
						      struct tty_driver* drv,
						      int idx)
{
	(void)port;
	(void)host;
	(void)parent;
	(void)drv;
	(void)idx;
	return ERR_PTR(-ENODEV);
}

static inline int serdev_tty_port_unregister(struct tty_port* port)
{
	(void)port;
	return -ENODEV;
}

#endif /* __LINUX_SERDEV_H__ */
