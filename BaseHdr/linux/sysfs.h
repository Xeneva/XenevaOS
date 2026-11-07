#ifndef __LINUX_SYSFS_H__
#define __LINUX_SYSFS_H__

/*
 * DCL <linux/sysfs.h> -- the two attribute structs and the __ATTR family.
 *
 * mainline splits this across sysfs.h and device.h; DCL has no kobject layer,
 * so what is here is only what a ported driver *declares*: a static
 * device_attribute produced by DEVICE_ATTR_RW() and a static attribute_group
 * to hang it in (8250_port.c:3089-3097). Nothing creates a sysfs file from
 * them yet -- registration goes through uart_port.attr_group, whose consumer
 * is the stage that brings uart_add_one_port over.
 *
 * struct attribute is mainline's two members verbatim (the lockdep fields are
 * behind CONFIG_DEBUG_LOCK_ALLOC, which DCL does not set). struct
 * attribute_group is deliberately shorter: see the comment inside it.
 *
 *   upstream  include/linux/sysfs.h  (mainline v7.2)
 */

#include <linux/kernel.h>	/* umode_t, size_t */

struct attribute {
	const char		*name;
	umode_t			mode;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	bool			ignore_lockdep:1;
	struct lock_class_key	*key;
	struct lock_class_key	skey;
#endif
};

struct attribute_group {
	const char		*name;
	/* NULL-terminated. 8250_port.c:3096 initialises this one field with a
	 * designated initializer, and uart_port.attr_group points at the group
	 * from outside -- so the member is named and typed exactly as mainline's
	 * union arm names it, while the rest of mainline's group (is_visible,
	 * bin_attrs, bin_size) is not carried: no DCL caller supplies them. */
	struct attribute		**attrs;
};

/*
 * __ATTR(_name, _mode, _show, _store) -- mainline's, minus
 * VERIFY_OCTAL_PERMISSIONS(): DCL has no permission checker to run the mode
 * through, and a mode that gets through unverified is what mainline's macro
 * produces on a kernel without that check too.
 *
 * The stringification is #_name rather than mainline's __stringify(_name);
 * both produce "rx_trig_bytes" for the only call in the tree, and DCL's
 * stringify lives in a header this one does not need to pull in.
 */
#define __ATTR(_name, _mode, _show, _store) {\
	.attr = { .name = #_name, .mode = _mode },\
	.show = _show,\
	.store = _store,\
}

#define __ATTR_RW_MODE(_name, _mode) \
	__ATTR(_name, _mode, _name##_show, _name##_store)
#define __ATTR_RW(_name) __ATTR_RW_MODE(_name, 0644)


/*
 * sysfs_emit(buf, fmt, ...) -- write a formatted string into a sysfs buffer.
 *
 * mainline bounds by PAGE_SIZE, which is the contract sysfs gives the show
 * callback: the buffer is always one page.  DCL's PAGE_SIZE lives in
 * <Mm/vmmngr.h>, and pulling the MM header into a sysfs header to read one
 * constant would be a poor trade, so the bound is spelled out and the reason
 * is here: 4096 is DCL's page size on every target in scope (Mm/vmmngr.h:90),
 * and _snprintf additionally caps at _print.c's MAX_STRING_LENGTH, so the
 * bound is not the only thing standing between this and an overrun.
 *
 * One caller: rx_trig_bytes_show at 8250_port.c:3033, writing "%d\n".
 */
#define sysfs_emit(buf, fmt, ...) \
	snprintf((buf), 4096, (fmt), ##__VA_ARGS__)

/*
 * __ATTR_RO / __ATTR_WO -- the read-only and write-only halves of __ATTR.
 *
 * They cannot be written as __ATTR(_name, 0444, _name##_show, _name##_store),
 * because no store function exists for them to name: __ATTR's expansion
 * embeds `_store` directly in a struct initialiser, so the macro would fail
 * at the *declaration*, not at a call site.  Hence the three separate
 * spellings mainline uses too.
 *
 * The modes are mainline's -- 0444 for a show-only attribute, 0200 for a
 * store-only one -- and unlike __ATTR these skip the octal permission check
 * for the same reason __ATTR does: DCL has no VERIFY_OCTAL_PERMISSIONS.
 */
#define __ATTR_RO(_name) { \
	.attr = { .name = #_name, .mode = 0444 }, \
	.show = _name##_show, \
}

#define __ATTR_WO(_name) { \
	.attr = { .name = #_name, .mode = 0200 }, \
	.store = _name##_store, \
}

/*
 * S_IRUGO -- 0444, "world-readable". mainline defines the whole S_I* family
 * in <uapi/linux/stat.h>, which this tree has no copy of: nothing needed a
 * permission *name*, because DEVICE_ATTR_RW()/__ATTR_RO() spell their modes
 * as literals (0644, 0444). virtio_console.c:1250 is the first caller that
 * names one -- `static DEVICE_ATTR(name, S_IRUGO, show_port_name, NULL);` --
 * so the constant is defined here, next to the __ATTR that consumes it, with
 * mainline's value. S_IWUSR (0200) is spelled out by __ATTR_WO already.
 */
#define S_IRUGO 0444

/*
 * sysfs_create_group() / sysfs_remove_group() -- macros, not functions, and
 * the reason is worth the whole block.
 *
 * DCL/linux_cdev_shim.c used to define these as ordinary functions taking
 * `const void*`. That works until a mainline caller writes
 *
 *     err = sysfs_create_group(&port->dev->kobj, &port_attribute_group);
 *                                                    (virtio_console.c:1642)
 *
 * because the *argument* still has to parse, and `port->dev->kobj` is a member
 * that does not exist: <linux/fs.h>'s struct device (the one file this tree
 * must not edit) carries `kobj_name` at offset 0 -- the slot mainline's
 * kobj.name occupies -- and no `kobj`. Making struct device grow one is not
 * available, and rewriting the vendored source is what this tree avoids.
 *
 * A macro whose parameters never appear in its replacement list never parses
 * them. The preprocessor substitutes the whole invocation with `0` and
 * `&port->dev->kobj` never reaches the parser at all -- which is exactly how
 * this header's spinlock sibling already handles a lock it does not take
 * (`#define spin_lock(l) do {} while (0)`, spinlock.h:36).
 *
 * The behaviour is unchanged, because the function being replaced was itself
 * a no-op: "no sysfs tree; success keeps add_port() on the happy path". A
 * caller that branches on the result still sees 0 (success), and
 * sysfs_remove_group() still does nothing -- there is no group to remove.
 */
#define sysfs_create_group(dev, grp)  (0)
#define sysfs_remove_group(dev, grp)  ((void)0)
/*
 * The same deal for the drm class links, drm_drv.c:1009/:1034:
 *
 *     ret = sysfs_create_link(minor->kdev->kobj.parent, &minor->kdev->kobj, name);
 *
 * ->kobj does not exist on this build's struct device (see the group
 * comment above), the parameters never parse, and there is no sysfs tree
 * to link into -- sysfs_create_link() answered 0 for every real caller
 * upstream that had no bus to begin with, so 0 here keeps the "primary
 * node is registered" branch taken.  remove is a statement-expression
 * free no-op for the matching teardown.
 */
#define sysfs_create_link(parent, target, name)  (0)
#define sysfs_remove_link(parent, name)  ((void)0)

#endif /* __LINUX_SYSFS_H__ */
