/*
 * DCL device/faux.h -- faux devices are just allocations.
 *
 * vkms_drv.c creates a parentless faux device to hang its connectors off
 * (mainline routes that through the driver core's faux bus).  DCL devices
 * only matter through devfs, and a faux device never gets a devt, so the
 * object exists to be stored and destroyed; its size is a fixed stand-in
 * because vkms only keeps the pointer.
 */
#ifndef _LINUX_DEVICE_FAUX_H
#define _LINUX_DEVICE_FAUX_H

#include <linux/slab.h>
#include <linux/device.h>	/* struct device -- mainline's faux_device
				 * embeds one, and vkms_drv.c takes &dev */

struct faux_device {
	struct device dev;
	char _tail[64];		/* stand-in for groups/glue_dir/bus; vkms
				 * only ever takes &dev and destroys it */
};

static inline struct faux_device *
faux_device_create(const char *name, void *parent, void *groups)
{
	struct faux_device *dev;

	(void)name; (void)parent; (void)groups;
	dev = (struct faux_device *)kmalloc(sizeof(struct faux_device), 0);
	if (dev)
		*dev = (struct faux_device){ 0 };
	return dev;
}

static inline void faux_device_destroy(struct faux_device *dev)
{
	kfree(dev);
}

#endif /* _LINUX_DEVICE_FAUX_H */
