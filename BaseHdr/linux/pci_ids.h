#ifndef _LINUX_PCI_IDS_H
#define _LINUX_PCI_IDS_H

/*
 * DCL <linux/pci_ids.h> -- the two vendor entries this tree has a driver
 * for, in upstream's spelling.
 *
 * mainline's header is 2600 lines of vendor/subvendor constants. Only the
 * names below are read by any file here: bochs.c matches the first entry of
 * its id_table on the whole quadruple (1234:1111, subsystem 1af4:1100) to
 * tell QEMU's stdvga apart from the Simics model, so the subvendor and
 * subdevice constants are the matches' identity, not decoration.
 *
 * Values are upstream's byte for byte (pci_ids.h:2592-2593 in linux-7.2.6);
 * the file grows by demand like the rest of DCL's headers.
 */

#define PCI_SUBVENDOR_ID_REDHAT_QUMRANET 0x1af4
#define PCI_SUBDEVICE_ID_QEMU            0x1100

/*
 * Added with the virtio-gpu port. The vendored virtio_gpu_driver_init()
 * (Vendored/drivers/gpu/drm/virtio/virtgpu_drv.c:187) looks a function up by
 * the redhat *vendor* id, so the vendor spelling is needed beside the
 * subvendor one above -- its device id (0x1050) is defined locally in that
 * file. The two PCI_CLASS_* codes feed pci_is_vga() in <linux/pci.h>: they
 * are class<<8 values as the fabric stores them (pci_shim.c re-packs
 * config-space byte order into that form), values upstream's
 * (pci_ids.h:16/:39 in linux-7.2.6).
 */
#define PCI_VENDOR_ID_REDHAT_QUMRANET    0x1af4

#define PCI_CLASS_NOT_DEFINED_VGA        0x0001
#define PCI_CLASS_DISPLAY_VGA            0x0300

#endif /* _LINUX_PCI_IDS_H */
