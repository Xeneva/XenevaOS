#ifndef __LINUX_DEVICE_H__
#define __LINUX_DEVICE_H__

/*
 * DCL <linux/device.h> -- the class/device_create surface mem.c needs to
 * publish /dev nodes. The bodies live in DCL/linux_cdev_shim.c (the bridge
 * into Xeneva devfs) and DCL/dcl_va_trampoline.s (device_create is variadic,
 * and BaseHdr/stdarg.h cannot rebuild a va_list under aarch64-unknown-windows,
 * so the asm entry saves x0..x7 for device_create_Call).
 */

#include <linux/kernel.h>	/* umode_t */
#include <linux/mutex.h>	/* struct mutex: <linux/input.h> puts one in input_dev */
#include <linux/devres.h>	/* devm_*: mainline's include for these is
				 * <linux/devres.h>, which no vendored drm
				 * .c names -- they reach device APIs through
				 * this header, so the surface rides here. */
#include <linux/kobject.h>	/* kobject_uevent_env() macro -- drm_drv.c
				 * reaches it through this header, as mainline
				 * does; see kobject.h for why it is a macro. */
#include <linux/fs.h>		/* struct device, MKDEV */
#include <linux/sysfs.h>	/* struct attribute, __ATTR */
#include <linux/property.h>	/* dev_fwnode(), property readers */
#include <linux/fwnode.h>	/* fwnode_handle_put() -- mainline keeps this
				 * in the fwnode/property pair; property.h is
				 * where DCL defines the struct and this header
				 * is where its callers reach it. */

struct device_type {
	const char* name;
	const struct attribute_group* const* groups;
	int (*uevent)(const struct device* dev, struct kobj_uevent_env* env);
	char* (*devnode)(const struct device* dev, umode_t* mode);
	void (*release)(struct device* dev);
	const struct dev_pm_ops* pm;
};

struct class {
	const char* name;
	char* (*devnode)(const struct device* dev, umode_t* mode);
};

int class_register(const void* cls);
void class_unregister(const void* cls);

struct device* device_create(const void* class, const void* parent,
							 unsigned int devt, void* drvdata,
							 const char* fmt, ...);
void device_destroy(const void* class, unsigned int devt);

/*
 * The driver-core pair drm_drv.c drives directly (device_add at :195,
 * device_del at :226): a struct device with kobj_name + devt already set
 * (drm_sysfs_minor_alloc() sets them) becomes a devfs node, same bridge
 * device_create() takes.  Bodies: DCL/linux_cdev_shim.c.
 */
int device_add(struct device* dev);
void device_del(struct device* dev);
void device_initialize(struct device* dev);
int dev_set_name(struct device* dev, const char* fmt, ...);


/*
 * struct device_attribute and DEVICE_ATTR_RW().
 *
 * mainline spells device_attribute with __SYSFS_FUNCTION_ALTERNATIVE to get
 * a const-correct show() alongside show_const(); DCL writes the plain two
 * members, because nothing in this tree takes the address of a const variant
 * and the alternative exists to serve a different property system. The field
 * *names* -- attr, show, store -- are mainline's, and they matter: 8250_port.c
 * reads the attribute back as &dev_attr_rx_trig_bytes.attr, so a differently
 * named member would not compile rather than silently misbehave.
 */
struct device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device* dev, struct device_attribute* attr,
			char* buf);
	ssize_t (*store)(struct device* dev, struct device_attribute* attr,
			 const char* buf, size_t count);
};

#define __DEVICE_ATTR_RW_MODE(_name, _mode) \
	__ATTR(_name, _mode, _name##_show, _name##_store)
#define __DEVICE_ATTR_RW(_name) __DEVICE_ATTR_RW_MODE(_name, 0644)

/*
 * static DEVICE_ATTR_RW(rx_trig_bytes);
 *     -> static struct device_attribute dev_attr_rx_trig_bytes =
 *            __ATTR(rx_trig_bytes, 0644, rx_trig_bytes_show,
 *                   rx_trig_bytes_store);
 *
 * The producing macro deliberately expands to a *declaration*, not an
 * expression: the call site is `static DEVICE_ATTR_RW(rx_trig_bytes);` with
 * the static already written, so the expansion has to carry the struct keyword
 * and end at the initializer. That is why this is not a value-style macro.
 */
#define DEVICE_ATTR_RW(_name) \
	struct device_attribute dev_attr_##_name = __DEVICE_ATTR_RW(_name)


/*
 * dev_get_drvdata() / dev_set_drvdata().
 *
 * The input core is the first user that needs a real answer: it stores its
 * per-device `struct virtio_input *` with input_set_drvdata() and reads it
 * back from the LED/status callback.  struct device carries the slot at
 * offset 88 (see <linux/fs.h>), inside the 800-byte ABI blob rather than
 * beyond it, so the accessors can be the plain mainline pair.
 */
static inline void* dev_get_drvdata(const struct device* dev)
{
	return dev ? dev->driver_data : NULL;
}

static inline void dev_set_drvdata(struct device* dev, void* data)
{
	if (dev)
		dev->driver_data = data;
}

#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)

/*
 * DEVICE_ATTR(_name, _mode, _show, _store) -- the four-argument spelling
 * mainline defines in <linux/device.h>. The three one-argument spellings
 * above cover everything else in this tree (8250 and serial_core use only
 * _RW/_RO); virtio_console.c:1250 is the first caller that names its own
 * mode -- `static DEVICE_ATTR(name, S_IRUGO, show_port_name, NULL);` -- so
 * this exists for it.
 *
 * It is a *declaration* macro exactly like DEVICE_ATTR_RW above: the
 * expansion has to carry the struct keyword, because the call site writes
 * `static` and expects the expansion to start at the type. The mode comes
 * from <linux/sysfs.h>, which this header already includes, and `store` is
 * allowed to be NULL (it is, at that one call site) because __ATTR puts it
 * straight into a struct member.
 */
#define DEVICE_ATTR(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, \
							   _show, _store)

#define DEVICE_ATTR_WO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_WO(_name)

/*
 * get_device() -- mainline returns the pointer it was handed, as a
 * refcount-taking trip. DCL's is in DCL/linux_cdev_shim.c:157 and does the
 * same thing for the same reason put_device() two blocks down does nothing:
 * devfs owns the lifetime and there is no refcount to take. Declared here
 * because that is where mainline declares it, and because virtio_console.c:438
 * reaches it through this header with no other one in scope.
 */
void* get_device(const void* dev);

/*
 * `static DEVICE_ATTR_RO(uartclk);` and its fifteen siblings at
 * serial_core.c:3004-3017 are the tty device's attribute table -- uartclk,
 * type, line, port, irq, flags, xmit_fifo_size, close_delay, closing_wait,
 * custom_divisor, io_type, iomem_base, iomem_reg_shift, console.  Each needs
 * a struct device_attribute whose show function the same file defines, so
 * this is a *declaration* macro exactly like DEVICE_ATTR_RW above: the struct
 * keyword carries `static`, and there is no trailing semicolon in the
 * expansion because the call site supplies it.
 */

/*
 * put_device() is a declaration, not a definition: DCL/linux_cdev_shim.c:160
 * already has the body (a no-op -- devfs owns the lifetime), and four call
 * sites in serial_core.c (:2308, :2311, :2387, :2390) pair it with the
 * get from device_find_child().  Declaring it anywhere else would be a
 * second definition; declaring it here is where mainline puts it.
 *
 * The parameter type is `const void *` because that is what the shim's
 * definition says, and the two have to agree -- otherwise the pair compiles
 * as two unrelated symbols and one of them silently fails to link.
 */
void put_device(const void* dev);

/*
 * dev_name() -- the device's name as devfs set it.  DCL's struct device has
 * the kobject name as its first member (kobj_name at offset 0, the same
 * slot mainline's dev->kobj.name occupies), and linux_cdev_shim.c:449 fills
 * it in, so this reads a live value rather than fabricating one.
 *
 * The NULL arm is belt-and-braces: the one caller (serial_core.c:2490) already
 * guards `port->dev ? dev_name(port->dev) : ""`, but `%s` handed a NULL on a
 * formatter with no "(null)" convention is a fault, and a name that is absent
 * is better reported as empty than as a crash.
 */
static inline const char* dev_name(const struct device* dev)
{
	if (!dev || !dev->kobj_name)
		return "";
	return dev->kobj_name;
}

/*
 * device_find_child() -- search a device's children for one the predicate
 * accepts.  DCL has no child-device tree: devices are created flat by
 * device_create() and never linked under a parent, so the answer is always
 * "none".
 *
 * The consequence is visible one line later at both call sites (serial_core.c
 * :2305 and :2383): `tty_dev` comes back NULL, so the wake-arming arm at
 * :2306 is skipped (it tests tty_dev first) and the one at :2383 calls
 * device_may_wakeup(NULL), which is written to tolerate it.  Both paths then
 * fall through to put_device(tty_dev) with NULL, which the shim ignores.
 *
 * Returning NULL rather than a fabricated device is what keeps
 * serial_match_port() -- whose job is to compare dev_t values against a real
 * child -- from ever being handed something it would have to make sense of.
 */
static inline struct device* device_find_child(
    struct device* parent, void* data,
    int (*match)(struct device* dev, const void* data))
{
	(void)parent;
	(void)data;
	(void)match;
	return NULL;
}

/*
 * ── the device-model symbols the vendored drm core links against ────────
 *
 * mainline keeps these across <linux/device.h>, <linux/dev_printk.h> and
 * <linux/platform_device.h>.  DCL has no dev_printk.h at all and its
 * platform_device.h is an empty stub, while every vendored drm .c reaches
 * this header through drm_print.h -- so this is where the surface rides,
 * the same routing devres.h and kobject.h already got above.  The bodies
 * that cannot be static inlines are in DCL/linux_device_shim.c.
 */

/*
 * device_is_registered() -- mainline reads kobject.state_in_sysfs, which DCL
 * does not model (struct device is the offset-forged blob in fs.h and the
 * kobject state bits have no named fields).  The one caller,
 * drm_minor_unregister() (drm_drv.c:220), asks whether the minor's kdev was
 * ever added so it can skip device_del() for one that was not.  What can be
 * answered truthfully here is the shape device_add() (DCL/linux_cdev_shim.c
 * :685) publishes: a name and a dev_t, both set by
 * drm_sysfs_minor_alloc() before the add.  device_del() tolerates a device it
 * never saw, so this stays on the safe side of the same question.
 */
static inline int device_is_registered(struct device* dev)
{
	return dev && dev->kobj_name && dev->devt;
}

/*
 * dev_to_node() -- mainline's answer without CONFIG_NUMA is NUMA_NO_NODE,
 * and DCL has no NUMA at all: one memory pool and no per-node allocator for
 * the result to select.  drm_managed.c:147/:233 hand it to alloc_dr()'s node
 * argument, where -1 (mainline's NUMA_NO_NODE) changes nothing.  The real
 * body would read dev->numa_node, a field the pinned fs.h layout does not
 * name.
 */
static inline int dev_to_node(struct device* dev)
{
	(void)dev;
	return -1;
}

/*
 * dev_driver_string() -- mainline answers driver->name, or the bus name, or
 * "".  DCL attaches neither a driver nor a bus to struct device (see the
 * dev_get_drvdata() note above), so the no-driver arm is the only one that
 * can fire.  Its users are the drm_WARN/drm_WARN_ONCE macros in
 * <drm/drm_print.h>:780/:785, which print "%s %s: [drm] ..." with
 * dev_driver_string() and dev_name(); a fabricated driver name would put a
 * second, wrong identifier in every warning, and the empty string does not.
 */
const char* dev_driver_string(const struct device* dev);

/*
 * dev_printk() -- the formatting entry point behind DRM's device-scoped
 * logging.  drm_print.c:194/:197 call it directly with a level string, the
 * device and a message built from a struct va_format into "%pV".  mainline
 * v7.2 spells dev_printk as a macro over _dev_printk(), but its header was
 * never ported, so this call has been an implicit `int dev_printk()` --
 * accepted here because -Wno-implicit-function-declaration is on, which is
 * exactly why the declaration has to exist: the call must be checked against
 * something.  The body is in DCL/linux_device_shim.c.
 */
int dev_printk(const char* level, const struct device* dev, const char* fmt,
	       ...);

/*
 * dev_warn_once() -- mainline defines this per call site in
 * <linux/dev_printk.h> so that the once-flag is the statement's own static;
 * a function could only remember across every caller.  DCL has no
 * dev_printk.h, so it lives here.  `int` rather than mainline's
 * `bool __read_mostly` follows kernel.h:635's WARN_ON_ONCE, the in-tree
 * precedent (DCL has no __read_mostly).  The expander is drm_warn_once()
 * (BaseHdr/drm/drm_print.h:637) at drm_plane.c:1708, drm_dumb_buffers.c:160
 * and drm_format_helper.c:509 -- all conditions that can repeat per
 * plane/framebuffer, which is why the flag is not decorative.
 */
#ifndef dev_warn_once
#define dev_warn_once(dev, fmt, ...)					\
	do {								\
		static int __dcl_warned_once;				\
		if (!__dcl_warned_once) {				\
			__dcl_warned_once = 1;				\
			dev_warn((dev), fmt, ##__VA_ARGS__);		\
		}							\
	} while (0)
#endif

/*
 * platform_device_unregister() -- mainline drivers/base/platform.c, declared
 * in <linux/platform_device.h>; DCL's copy of that header is an empty stub
 * and struct platform_device is never defined, only pointed at
 * (drm_connector.h:1884).  The one caller is drm_connector_cleanup()
 * (drm_connector.c:770), unregistering connector->hdmi_audio.codec_pdev --
 * which nothing in this tree assigns, because the HDMI-audio codec binding
 * that would is not ported.  The body in DCL/linux_device_shim.c is a no-op
 * that also answers NULL.
 *
 * The tag is forward-declared at file scope on purpose: a struct tag first
 * named inside a parameter list is scoped to that prototype, and two such
 * spellings are not the same type (the trap <linux/scatterlist.h> documents
 * for struct sg_table).
 */
struct platform_device;
void platform_device_unregister(struct platform_device* pdev);

/*
 * dmem_cgroup_register_region() / dmem_cgroup_unregister_region() --
 * mainline's dma-buf cgroup accounting, declared in <linux/cgroup_dmem.h>.
 * DCL's cgroup_dmem.h is an empty shell (no memcg is ported), and its two
 * callers in the vendored core -- drm_drv.c:971 (drmm_cgroup_register_region)
 * and :954 (drmm_cg_unregister_region) -- reach this header through
 * drm_print.h, so the declarations ride here beside the stubs in
 * DCL/linux_device_shim.c.  The region tag is mainline's opaque forward
 * declaration; nothing in this tree dereferences it.
 */
struct dmem_cgroup_region;
struct dmem_cgroup_region* dmem_cgroup_register_region(u64 size,
						       const char* name_fmt,
						       ...);
void dmem_cgroup_unregister_region(struct dmem_cgroup_region* region);

/*
 * check_move_unevictable_folios() -- vmscan's "put these pinned folios back
 * on the right LRU" walk, declared in mainline's <linux/swap.h> -- a header
 * DCL does not carry.  drm_gem.c:635 calls it from
 * drm_gem_check_release_batch() on the way out of drm_gem_get_pages()/
 * drm_gem_put_pages(), a path reached through shmem backing that is itself
 * stubbed (DCL/drm_stubs.c).  The no-op body is in DCL/linux_device_shim.c;
 * its counterpart, __folio_batch_release(), is already a no-op there.
 */
struct folio_batch;
void check_move_unevictable_folios(struct folio_batch* fbatch);
#endif /* __LINUX_DEVICE_H__ */
