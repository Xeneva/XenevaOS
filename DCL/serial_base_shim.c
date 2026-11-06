// SPDX-License-Identifier: GPL-2.0
/*
 * DCL/serial_base_shim.c -- the serial core's device bus, minus the bus.
 *
 * serial_core.c links against seven symbols that mainline spreads across
 * three files -- serial_base_bus.c, serial_ctrl.c and serial_port.c -- and
 * none of them can come from source here, because all three are *driver
 * core* code.  They call device_initialize(), device_add(), bus_register(),
 * driver_register(), dev_set_name() and fwnode_handle_get().  DCL has no
 * driver core to call: <linux/fs.h>'s struct device is a pinned-layout blob
 * (kobj_name at 0, devt at 708, 800 bytes, with everything between declared
 * padding -- see the comment above it), and the driver core is what walks
 * those bytes.  Reconstructing it is a later stage, and is not what makes
 * 8250_port.c and serial_core.c work.
 *
 * So this file is the driver-core *contract* and nothing more.  What
 * serial_core.c actually needs from those three files is a small, closed
 * pointer graph, and that is what is implemented:
 *
 *   serial_core.c:3267  serial_base_ctrl_add(port, port->dev)
 *   serial_core.c:3275  serial_base_port_add(port, ctrl_dev)
 *   serial_core.c:3232  serial_core_get_ctrl_dev():
 *                           `struct device *dev = &port_dev->dev;`
 *                           `return to_serial_base_ctrl_device(dev->parent);`
 *
 * The third line is the one that decides this file's shape.  It is called
 * from serial_core_unregister_port() at :3345 with no NULL check, one
 * statement before the port is torn down.  Returning NULL from
 * serial_base_port_add() was the tempting answer -- every caller tolerates
 * it on the way *in*, because `IS_ERR(NULL)` is false -- and it would have
 * compiled, booted and linked clean, then read through a NULL pointer at
 * offset 64 the first time anything unregistered a port.  So both objects
 * are allocated for real, and port_dev->dev.parent is set to
 * &ctrl_dev->dev, which is what lets container_of() come back to the same
 * controller that registered the port.
 *
 * Sharing falls out of that rather than being implemented: serial_core_
 * ctrl_find() at :3247 walks the driver's state array comparing
 * uart_port->dev and ctrl_id, then *derives* the controller from
 * port_dev->dev.parent.  Two ports under one controller therefore end up
 * with one ctrl device, and :3359 drops it only once find() reports that no
 * ports are left -- the same lifetime mainline gets, without a refcount.
 *
 * All allocation is kzalloc: struct device carries kobj_name and devt, both
 * of which Xeneva's devfs reads back, and leaving them zero is what makes an
 * unregistered device look unregistered rather than look like device 0.
 *
 *   upstream  drivers/tty/serial/serial_base_bus.c (ctrl_add, port_add,
 *             both *_remove), serial_port.c (port_startup, port_shutdown),
 *             serial_core.c's own #else arm at include/linux/serial_core.h
 *             -- the console matcher.  The pointer graph, not the sysfs
 *             side, is what is ported; when the driver core lands, this
 *             whole file is the thing to delete.
 */

#include <linux/err.h>		/* ERR_PTR */
#include <linux/errno.h>	/* ENOMEM */
#include <linux/kernel.h>	/* container_of, used by serial_base.h */
#include <linux/slab.h>		/* kzalloc, kfree */
#include <linux/device.h>	/* struct device */
#include <linux/idr.h>		/* struct ida, embedded by serial_base.h */
#include <linux/serial_core.h>	/* struct uart_port, uart_port_lock_irqsave */
#include <linux/printk.h>	/* dev_warn */

#include "../Vendored/drivers/tty/serial/serial_base.h"

struct serial_ctrl_device* serial_base_ctrl_add(struct uart_port* port,
						struct device* parent)
{
	struct serial_ctrl_device* ctrl_dev;

	/* The controller is keyed by (parent, port->ctrl_id) in serial_core_
	 * ctrl_find(), which reaches it back through port_dev->dev.parent --
	 * so `parent` here is not bookkeeping, it is the lookup key.  DCL
	 * passes port->dev, which is NULL for ports not described by a
	 * device; that is fine because find() compares NULL == NULL. */
	(void)port;

	ctrl_dev = kzalloc(sizeof(*ctrl_dev), GFP_KERNEL);
	if (!ctrl_dev)
		return ERR_PTR(-ENOMEM);

	ctrl_dev->dev.parent = parent;
	return ctrl_dev;
}

struct serial_port_device* serial_base_port_add(struct uart_port* port,
						struct serial_ctrl_device* parent)
{
	struct serial_port_device* port_dev;

	port_dev = kzalloc(sizeof(*port_dev), GFP_KERNEL);
	if (!port_dev)
		return ERR_PTR(-ENOMEM);

	port_dev->port = port;
	/* Non-NULL whenever the controller is, so serial_core_get_ctrl_dev()
	 * at :3232 lands on &ctrl_dev->dev and container_of() returns the
	 * object serial_base_ctrl_add() handed out -- not on offset 0 of a
	 * NULL.  This is the whole reason both functions allocate. */
	port_dev->dev.parent = parent ? &parent->dev : NULL;
	return port_dev;
}

void serial_base_ctrl_device_remove(struct serial_ctrl_device* ctrl_dev)
{
	/* serial_core.c:3332 and :3359 both call this with a value that may
	 * be NULL -- new_ctrl_dev starts at NULL on the error path, and the
	 * success path passes whatever find() returned.  Guarding here rather
	 * than at the two call sites keeps the vendored source untouched. */
	if (ctrl_dev)
		kfree(ctrl_dev);
}

void serial_base_port_device_remove(struct serial_port_device* port_dev)
{
	if (port_dev)
		kfree(port_dev);
	/* port->port_dev is not cleared here, and should not be: mainline's
	 * caller does it, serial_core_remove_one_port() at :3190 sets
	 * `uport->port_dev = NULL` and `state->uart_port = NULL` before this
	 * object is ever touched again.  Clearing it here too would race
	 * nothing but would also hide a caller that got the order wrong. */
}

/*
 * serial_base_port_startup()/shutdown() -- the pair serial_core.c:376 and
 * :400/:1732 bracket open and close with.
 *
 * mainline's bodies are one bit each (serial_port.c:91):
 *
 *     uart_port_lock_irqsave(port, &flags);
 *     port_dev->tx_enabled = enabled;
 *     uart_port_unlock_irqrestore(port, flags);
 *
 * The lock is kept, because the reader it protects is not hypothetical on
 * the mainline side -- serial_port_runtime_suspend() looks at the bit to
 * decide whether the UART stays clocked -- and if DCL gains runtime PM
 * later, a version of this that dropped the lock would be wrong then in a
 * way nothing would report.  What is *not* brought across is the
 * pm_runtime_get_sync()/put() that used to sit either side of it: DCL's
 * pm_runtime_enabled() is 0 (<linux/pm_runtime.h>), so mainline's own
 * `if (!pm_runtime_enabled(dev))` guards would not have run there either,
 * and getting a runtime-PM reference on a device with no PM domain would be
 * a call that succeeds at doing nothing while looking like it did.
 */
void serial_base_port_startup(struct uart_port* port)
{
	struct serial_port_device* port_dev = port->port_dev;
	unsigned long flags;

	if (!port_dev)
		return;

	uart_port_lock_irqsave(port, &flags);
	port_dev->tx_enabled = 1;
	uart_port_unlock_irqrestore(port, flags);
}

void serial_base_port_shutdown(struct uart_port* port)
{
	struct serial_port_device* port_dev = port->port_dev;
	unsigned long flags;

	if (!port_dev)
		return;

	uart_port_lock_irqsave(port, &flags);
	port_dev->tx_enabled = 0;
	uart_port_unlock_irqrestore(port, flags);
}

/*
 * serial_base_match_and_update_preferred_console() -- only linked because
 * BaseHdr/linux/autoconf.h defines CONFIG_SERIAL_CORE_CONSOLE, which moves
 * this from serial_base.h's inline `return 0` arm (:60) to the real
 * declaration at :55 and leaves nobody to define it.
 *
 * mainline's job is to read `console=ttyS0:1.0` off the command line, match
 * the device name against the driver's controller and move cons->index to
 * the port that answered.  DCL has no such string: the console is chosen by
 * the board layer before this code runs, and there is no command line to
 * parse it out of.  Returning 0 is therefore not a stub, it is the answer --
 * and it is the same value serial_base.h's own CONFIG_SERIAL_CORE_CONSOLE=n
 * arm returns, so serial_core_register_port() takes the identical
 * `ret == 0` path it would if the option were off.
 */
int serial_base_match_and_update_preferred_console(struct uart_driver* drv,
						   struct uart_port* port)
{
	(void)drv;
	(void)port;
	return 0;
}
