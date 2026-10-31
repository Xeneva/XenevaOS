#ifndef __LINUX_OF_H__
#define __LINUX_OF_H__

/*
 * DCL <linux/of.h> -- one function, of_console_check().
 *
 * mainline's header is the flattened-device-tree interface: parsing,
 * phandles, properties, interrupt mapping, the aliases the bootconsole logic
 * reads.  Xeneva has no device tree in its kernel -- boards are compiled in --
 * so none of that exists and none of it is asked for.  serial_core.c calls
 * exactly one thing from it (uart_add_one_port at :3093):
 *
 *     of_console_check(uport->dev->of_node, uport->cons->name, uport->line);
 *
 * whose result is discarded: mainline uses it to decide whether this port's
 * *device tree node* names the console the boot loader set up, so a matching
 * alias keeps the boot console from being torn down.  With no aliases to
 * match, "no" is the answer -- false, same as mainline gives for a port with
 * no node at all, which is what every DCL port has.  The call stays because
 * it is inside a guard (`if (uport->cons && uport->dev)`) whose other half
 * does matter, and dropping it would mean editing a vendored file.
 *
 *   upstream  include/linux/of.h  (mainline v7.2)
 */

struct device_node;

static inline int of_console_check(const struct device_node* dn,
				   const char* name, int idx)
{
	(void)dn;
	(void)name;
	(void)idx;
	return 0;
}

#endif /* __LINUX_OF_H__ */
