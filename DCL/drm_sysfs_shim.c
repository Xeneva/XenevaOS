/*
 * DCL/drm_sysfs_shim.c -- what drm_sysfs.c does, contracted to devfs.
 *
 * Vendored/drivers/gpu/drm/drm_sysfs.c is the one drm core .c deliberately
 * left out of Vendored/ (see Vendored/README.md): on mainline it owns the
 * drm class, /sys/class/drm, and the uevent traffic over them, and every
 * line of that is sysfs this tree does not have.  What the contract still
 * owes is the node mainline's devtmpfs would have made.  drm_minor_register()
 * calls drm_sysfs_minor_alloc() for a struct device and device_add() for the
 * node, and device_add() in DCL/linux_cdev_shim.c is where "/dev/dri/card0"
 * appears -- so the allocator below does mainline's four things (allocate,
 * name, devt, parent) and the event half answers on the channel
 * kobject_uevent() already drops (BaseHdr/linux/kobject.h).
 *
 * The connector-side entries are bookkeeping no-ops: a connector has no
 * devt, so mainline's device_register() there would have been a sysfs
 * directory and nothing devfs can hold; drm_connector.c only wants the
 * calls to succeed.
 *
 * The internal declarations mirror Vendored/drivers/gpu/drm/drm_internal.h
 * :162-173 and :201's neighbours, which this file cannot include (DCL
 * sources reach headers through BaseHdr/ only); the drm_sysfs.h five come
 * from <drm/drm_sysfs.h> the normal way.
 */
#include <stddef.h>
#include <linux/err.h>		/* ERR_PTR */
#include <linux/slab.h>	/* kmalloc */
#include <linux/string.h>	/* memset */
#include <linux/fs.h>		/* struct device, MKDEV */
#include <drm/drm_ioctl.h>	/* DRM_MAJOR */
#include <drm/drm_file.h>	/* struct drm_minor, enum drm_minor_type */
#include <drm/drm_device.h>	/* minor->dev->dev */
#include <drm/drm_sysfs.h>	/* hotplug/property entry points */
#include <drm/drm_connector.h>	/* struct drm_connector / drm_property */

/* Vendored drm_internal.h:165-173 -- see the file comment. */
int drm_sysfs_init(void);
void drm_sysfs_destroy(void);
struct device *drm_sysfs_minor_alloc(struct drm_minor *minor);
int drm_sysfs_connector_add(struct drm_connector *connector);
int drm_sysfs_connector_add_late(struct drm_connector *connector);
void drm_sysfs_connector_remove_early(struct drm_connector *connector);
void drm_sysfs_connector_remove(struct drm_connector *connector);
void drm_sysfs_lease_event(struct drm_device *dev);

int drm_sysfs_init(void)
{
	/* mainline creates the drm class here; DCL's class_register() is a
	 * no-op and nothing dereferences drm_class, so there is nothing to
	 * create -- the work is register_chrdev() in drm_core_init(), which
	 * runs before any minor exists. */
	return 0;
}

void drm_sysfs_destroy(void)
{
}

/*
 * Name one minor the way devtmpfs would have: "dri/card%d" for primary and
 * control minors, "dri/renderD%d" for render (128 + index, the offset mainline
 * gives render nodes), "accel%d" for the accel type.  The digits are written
 * by hand because DCL's formatters live behind UARTDebugOut and this needs a
 * buffer, not a port.
 */
static char *drm_sysfs_minor_name(struct drm_minor *minor)
{
	const char *prefix;
	int num;
	char buf[24];
	int n = 0;
	char digits[12];
	int d = 0;
	char *out;

	if (minor->type == DRM_MINOR_RENDER) {
		prefix = "dri/renderD";
		num = 128 + minor->index;
	} else if (minor->type == DRM_MINOR_ACCEL) {
		prefix = "accel";
		num = minor->index;
	} else {
		prefix = "dri/card";
		num = minor->index;
	}
	while (*prefix)
		buf[n++] = *prefix++;
	do {
		digits[d++] = (char)('0' + num % 10);
		num /= 10;
	} while (num && d < (int)sizeof(digits));
	while (d > 0 && n < (int)sizeof(buf) - 1)
		buf[n++] = digits[--d];
	buf[n] = '\0';

	out = (char *)kmalloc((size_t)n + 1, GFP_KERNEL);
	if (!out)
		return NULL;
	memcpy(out, buf, (size_t)n + 1);
	return out;
}

struct device *drm_sysfs_minor_alloc(struct drm_minor *minor)
{
	struct device *kdev;

	kdev = (struct device *)kmalloc(sizeof(struct device), GFP_KERNEL);
	if (!kdev)
		return ERR_PTR(-ENOMEM);
	memset(kdev, 0, sizeof(struct device));

	kdev->devt = MKDEV(DRM_MAJOR, minor->index);
	if (minor->dev)
		kdev->parent = minor->dev->dev;
	kdev->kobj_name = drm_sysfs_minor_name(minor);
	if (!kdev->kobj_name) {
		kfree(kdev);
		return ERR_PTR(-ENOMEM);
	}
	return kdev;
}

int drm_sysfs_connector_add(struct drm_connector *connector)
{
	(void)connector;
	return 0;
}

int drm_sysfs_connector_add_late(struct drm_connector *connector)
{
	(void)connector;
	return 0;
}

void drm_sysfs_connector_remove_early(struct drm_connector *connector)
{
	(void)connector;
}

void drm_sysfs_connector_remove(struct drm_connector *connector)
{
	(void)connector;
}

void drm_sysfs_lease_event(struct drm_device *dev)
{
	(void)dev;
}

/* The drm_sysfs.h half: events only -- mainline formats a netlink message
 * and calls kobject_uevent(), which DCL answers as (0) (kobject.h). */
void drm_sysfs_hotplug_event(struct drm_device *dev)
{
	(void)dev;
}

void drm_sysfs_connector_hotplug_event(struct drm_connector *connector)
{
	(void)connector;
}

void drm_sysfs_connector_property_event(struct drm_connector *connector,
					struct drm_property *property)
{
	(void)connector;
	(void)property;
}
