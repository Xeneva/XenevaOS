#ifndef _LINUX_VIRTIO_RNG_H
#define _LINUX_VIRTIO_RNG_H

/*
 * DCL <linux/virtio_rng.h> -- the virtio entropy device's IDs.
 *
 * mainline's uapi header includes <linux/virtio_ids.h> and
 * <linux/virtio_config.h> and defines nothing of its own; its whole job is to
 * give a driver the device type it matches on. DCL keeps one <linux/virtio.h>
 * instead of that pair, so this header is the same shape minus the middle
 * hop: include virtio.h, then define VIRTIO_ID_RNG.
 *
 * The id is mainline's, from include/uapi/linux/virtio_ids.h:
 *
 *     #define VIRTIO_ID_RNG 4
 *
 * and it has to be 4 rather than any local numbering, because that is what
 * the device reports: virtio_shim.c's virtio_device_type() computes the type
 * as (PCI device id - 0x1040), and QEMU's virtio-rng-pci with
 * disable-legacy=on enumerates as 1AF4:1044 -> 0x1044 - 0x1040 = 4. Get this
 * number wrong and virtio_rng_detect() finds the device, probe never runs, and
 * the only symptom is that hwrng_register() is never called.
 */

#include <linux/virtio.h>

#ifndef VIRTIO_ID_RNG
#define VIRTIO_ID_RNG 4
#endif

#endif /* _LINUX_VIRTIO_RNG_H */
