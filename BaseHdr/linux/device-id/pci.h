#ifndef __LINUX_DEVICE_ID_PCI_H__
#define __LINUX_DEVICE_ID_PCI_H__

/*
 * DCL <linux/device-id/pci.h> -- where mainline 7.2 keeps PCI_ANY_ID and the
 * id_table entry. It used to live in <linux/mod_devicetable.h>; 7.2 split it
 * out and <linux/pci.h> includes this path, so a ported driver's include
 * chain (pci.h supplies everything; bochs.c names no id header itself) lands
 * where upstream's does.
 *
 * One delta: upstream typedefs kernel_ulong_t here, under __KERNEL__. DCL's
 * <linux/kernel.h> already owns that typedef (kernel.h:75), so this header
 * includes kernel.h and rides on it -- repeating the typedef would be a
 * redefinition warning on every include.
 *
 * The struct is upstream's field-for-field except the trailing
 * `override_only`: it exists for vfio's driver_override matching, which this
 * tree has no code or configuration for. Like the device_driver members that
 * stayed padding, it is left out until something reads it; a static
 * initializer that ignores a member and one that zeroes it produce the same
 * bytes.
 */

#include <linux/kernel.h>	/* kernel_ulong_t, __u32 */

#define PCI_ANY_ID (~0)

struct pci_device_id {
	__u32 vendor, device;		/* Vendor and device ID or PCI_ANY_ID */
	__u32 subvendor, subdevice;	/* Subsystem ID's or PCI_ANY_ID */
	__u32 class, class_mask;	/* (class,subclass,prog-if) triplet */
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* __LINUX_DEVICE_ID_PCI_H__ */
