#ifndef __LINUX_DEVICE_DRIVER_H__
#define __LINUX_DEVICE_DRIVER_H__

/*
 * DCL <linux/device/driver.h> -- upstream's home for struct device_driver,
 * in the one shape DCL can lay out.
 *
 * The struct used to be defined in <linux/virtio.h>, because virtio_driver
 * was the first bus driver that needed to write `.driver.name` and that is
 * where the flattened bytes happened to live. pci_driver needs it now --
 * bochs.c initialises `.driver.pm` -- and a second definition under a second
 * header is a landmine that goes off the day a translation unit includes
 * both buses. The vendored virtio-gpu driver (next up after the scanout
 * stage) is exactly that translation unit: virtgpu_drv.c includes
 * <linux/pci.h> and reaches <linux/virtio.h> through its own header. So the
 * tag moved to upstream's path and both buses include it from here.
 *
 * Layout promises, in the order they bind:
 *
 *   - `name` stays at offset 0. `.driver.name = KBUILD_MODNAME` relies on it
 *     (virtio-rng.c, virtio_console.c) and the prebuilt modules were
 *     assembled against it.
 *
 *   - the struct stays 152 bytes. virtio_driver's own members sit after the
 *     whole struct -- id_table at 152, probe at 200 -- so a size change would
 *     move every one of them. `_rest` is what pins the size, and `pm` took
 *     its eight bytes out of it.
 *
 *   - `pm` is the only named member besides `name`. mainline continues with
 *     bus, owner, mod_name, probe, the of/acpi match tables and the rest of
 *     the bind machinery; all of those would be permanently-NULL members
 *     nothing in this tree reads. When one is actually needed it gets
 *     promoted out of `_rest` the same way `pm` did -- that is what the
 *     padding is for.
 */

struct dev_pm_ops;

struct device_driver {
	const char* name;
	const struct dev_pm_ops* pm;
	unsigned char _rest[136];
};

#endif /* __LINUX_DEVICE_DRIVER_H__ */
