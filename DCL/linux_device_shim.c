/*
 * DCL/linux_device_shim.c -- the device-model entry points the vendored drm
 * core calls at link time and DCL has no body for.
 *
 * Why these are functions and not macros or header inlines: mainline
 * declares every one of them as a function (device_is_registered and
 * dev_to_node are the two inlines, and those are in device.h), and the
 * callers compile them as ordinary calls.  With
 * -Wno-implicit-function-declaration on, a missing declaration is silent and
 * a missing body is a link error -- which is how this file came to exist
 * (the link named 19 undefined symbols).
 *
 * The declarations live where the calling file reaches them, each with its
 * why-comment: <linux/device.h> carries dev_driver_string, dev_printk,
 * dev_warn_once, platform_device_unregister, the dmem pair and
 * check_move_unevictable_folios; <linux/fwnode.h> carries fwnode_handle_put;
 * <drm/drm_utils.h> already carried drm_get_panel_orientation_quirk.
 * Nothing here re-declares one, so each definition below is checked against
 * the header prototype the caller saw.
 *
 * No license header and no doxygen, like the other DCL shims.
 */
#include <stdarg.h>
#include <linux/string.h>
#include <linux/err.h>		/* ERR_PTR */
#include <linux/device.h>	/* the declarations this file defines */
#include <linux/fwnode.h>	/* fwnode_handle_put() */
#include <linux/sprintf.h>	/* _vsnprintf() */
#include <drm/drm_utils.h>	/* drm_get_panel_orientation_quirk() */
#include <drm/drm_connector.h>	/* enum drm_panel_orientation */
#include <Drivers/uart.h>	/* UARTDebugOut */

/*
 * dev_driver_string() -- see the declaration in <linux/device.h> for what
 * mainline answers and who calls it.  There is no driver or bus object to
 * read a name from in this kernel, and mainline's own fallback for that case
 * is the empty string, so that is what the drm_WARN() lines get.
 */
const char* dev_driver_string(const struct device* dev)
{
	(void)dev;
	return "";
}

/*
 * dev_printk() -- render a device-scoped message to the UART.
 *
 * drm_print.c:194/:197 are the callers, through __drm_dev_vprintk(), which
 * packs its message as a struct va_format and hands it over in a "%pV"
 * argument.  DCL's formatter does not know %pV (<Log/_print.c>'s _xeprint
 * handles the C conversions; the printk() arm of the same function at
 * drm_print.c:202 goes through UARTDebugOut, which lacks even more), so the
 * vaf payload renders as its pointer and the surrounding literal text still
 * comes out.  Making that whole is a %pV case inside the formatter, not a
 * second varargs walker here -- one would have to reconstruct a va_list
 * DCL's stdarg.h cannot rebuild (the note at the top of device.h says the
 * same about device_create()).
 *
 * The device name is printed because mainline's dev_printk prefixes
 * dev->kobj.name too; the level is dropped because every KERN_* is the empty
 * string in <linux/printk.h> and UARTDebugOut has no level channel.  The
 * buffer is bounded the way kvasprintf()'s is (DCL/linux_alloc_shim.c):
 * _vsnprintf caps the output, and a truncated log line is the right failure
 * for a diagnostic.
 */
int dev_printk(const char* level, const struct device* dev, const char* fmt,
	       ...)
{
	char buf[256];
	va_list ap;
	int n;

	(void)level;

	va_start(ap, fmt);
	n = _vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	UARTDebugOut("%s: %s\r\n", dev ? dev_name(dev) : "", buf);
	return n;
}

/*
 * platform_device_unregister() -- no platform bus exists in this kernel
 * (<linux/platform_device.h> is an empty stub and struct platform_device is
 * only ever pointed at).  The one caller, drm_connector_cleanup()
 * (drm_connector.c:770), unregisters connector->hdmi_audio.codec_pdev, and
 * nothing in this tree ever assigns that member: the HDMI-audio codec
 * binding that would is not ported.  A no-op that accepts NULL is exactly
 * the work there is to do.
 */
void platform_device_unregister(struct platform_device* pdev)
{
	(void)pdev;
}

/*
 * fwnode_handle_put() -- drop a firmware-node reference.
 *
 * There are no fwnode objects in this kernel: dev_fwnode() (property.h)
 * returns NULL for every device and nothing creates a node, so the one call
 * site, drm_connector_cleanup() (drm_connector.c:798), only ever passes
 * NULL.  mainline's body is a static inline over fwnode->ops->put, which
 * cannot be spelled here because DCL never defines struct fwnode_operations;
 * <linux/fwnode.h> owns the full note.
 */
void fwnode_handle_put(struct fwnode_handle* fwnode)
{
	(void)fwnode;
}

/*
 * dmem_cgroup_register_region() -- mainline's dma-buf cgroup region
 * registration (<linux/cgroup_dmem.h>), reached from drm_drv.c:971
 * (drmm_cgroup_register_region).  There is no cgroup memory accounting in
 * this kernel -- DCL's cgroup_dmem.h is an empty shell -- so the honest
 * answer is -ENOSYS, DCL's convention for "not implemented" (see
 * DCL/drm_stubs.c) rather than mainline's !CONFIG NULL, because the caller's
 * IS_ERR_OR_NULL() arm propagates the errno and the log then says why.
 * drmm_cgroup_register_region() itself has no caller in this tree, so this
 * is a link-time answer rather than a live path.
 */
struct dmem_cgroup_region* dmem_cgroup_register_region(u64 size,
						       const char* name_fmt,
						       ...)
{
	(void)size;
	(void)name_fmt;
	return ERR_PTR(-ENOSYS);
}

/*
 * dmem_cgroup_unregister_region() -- the devres counterpart, run from
 * drm_drv.c:954 when a region was registered.  None ever is (the stub above
 * answers an error), so there is nothing to undo; the no-op cannot be
 * reached with a real region.
 */
void dmem_cgroup_unregister_region(struct dmem_cgroup_region* region)
{
	(void)region;
}

/*
 * check_move_unevictable_folios() -- vmscan's walk that returns pinned
 * folios to the right LRU list, called by drm_gem.c:635
 * (drm_gem_check_release_batch) on the way out of drm_gem_get_pages() and
 * drm_gem_put_pages().  DCL has no LRU lists and no vmscan: its pages are
 * kmalloc'd blocks that are descriptor and storage at once (see
 * <linux/pagemap.h>), so there is no list membership to fix.  The reference
 * drop the walk would pair with is __folio_batch_release() next to the call,
 * already a free-and-forget body in DCL/drm_stubs.c:296.  A no-op here
 * leaves the two halves consistent.
 */
void check_move_unevictable_folios(struct folio_batch* fbatch)
{
	(void)fbatch;
}

/*
 * drm_get_panel_orientation_quirk() -- the DMI quirk lookup from mainline
 * drivers/gpu/drm/drm_panel_orientation_quirks.c.  That file is one large
 * table of x86 board data plus dmi_first_match()/dmi_get_system_info()
 * calls, and DCL has no DMI layer, so it is not vendored and there is no
 * table to consult.  The caller, drm_connector.c:3065
 * (drm_connector_set_panel_orientation_with_quirk), tests the result against
 * DRM_MODE_PANEL_ORIENTATION_UNKNOWN and keeps the orientation the driver
 * passed in when it matches -- the correct outcome on a kernel that cannot
 * read firmware board data.  Returning the unknown value is therefore an
 * answer, not a placeholder: a wrong quirk would *change* the mode the
 * driver chose.
 */
int drm_get_panel_orientation_quirk(int width, int height)
{
	(void)width;
	(void)height;
	return DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
}
