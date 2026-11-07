/*
 * DCL pci.h -- the PCI surface the drm set names, and now a bus behind it.
 *
 * This header was compile-only for its first act: there was no PCI on the
 * boards this kernel targets, drm_file.c:987 and drm_ioctl.c:152 asked
 * dev_is_pci(dev->dev) before every bus-id setup, the answer was always
 * false, and the struct below was a three-member sketch sized for the BDF
 * string those reads print. The scanout stage changed what it has to carry:
 * QEMU's bochs-display is a real PCI device, and the vendored driver that
 * owns it (Vendored/drivers/gpu/drm/tiny/bochs.c) compiles and links whole
 * against this surface -- probe/remove/shutdown records, the id_table match
 * quadruple, BAR resources with start/length/flags, and the
 * pci_set/get_drvdata pair.
 *
 * The bus behind the surface is DCL/pci_shim.c: it walks the ECAM window the
 * native pcie code already maps, fills one struct pci_dev per function, and
 * runs id_table matching plus probe when a vendored driver registers. This
 * header stays a pure data contract except for one participant:
 * dev_is_pci() now has to answer for real devices (drm_file.c prints the BDF
 * string when it is true), and the only honest registry of "which devices
 * this bus made" lives in the fabric, so the inline forwards there. That
 * query is also what keeps the vendored drm core's own PCI paths (drm_edid's
 * switcheroo wrappers) answering correctly for a device that is now actually
 * behind a PCI bus, and keeps answering false for every device that is not.
 *
 * struct device lives in the pinned <linux/fs.h> (fs.h:184), which is why
 * this header includes fs.h rather than declaring the member opaque: an
 * incomplete struct device cannot be a member. The pinned struct is 800
 * bytes whether it describes a PCI device or not -- the fabric embeds it
 * inside its own pci_dev and never grows it, per the note in device.h about
 * writes at offset 800 being a heap overrun.
 */
#ifndef _LINUX_PCI_H
#define _LINUX_PCI_H

#include <linux/types.h>
#include <linux/fs.h>		/* struct device, at fs.h:184 */

/*
 * <linux/capability.h> for capable(), which the drm sources that name this
 * header also name without including it -- drm_file.c:149 and drm_ioctl.c:152
 * both write `capable(CAP_SYS_ADMIN)`. In mainline the chain runs through
 * this header's own <linux/device.h> (mainline pci.h:38); DCL's device layer
 * is struct device in the pinned <linux/fs.h> with nothing but a struct body
 * behind it, so the chain stops here and this header names the source
 * directly.
 *
 * Carried here rather than in <linux/file.h>, which is DCL's other carrier
 * of mainline fs.h's vocabulary, because drm_ioctl.c needs capable() and
 * does not include file.h -- and this header is the one the consumers all
 * name. Leaf, like every header on this path: capability.h includes only
 * <stdbool.h>.
 */
#include <linux/capability.h>

/*
 * The id structures, from upstream's 7.2 homes: PCI_ANY_ID and
 * struct pci_device_id live in <linux/device-id/pci.h>, and
 * struct device_driver -- the tag pci_driver.driver is -- in
 * <linux/device/driver.h>. Both are split out of mainline's pci.h set
 * because a second bus (virtio) shares the driver tag.
 */
#include <linux/device-id/pci.h>
#include <linux/device/driver.h>
#include <linux/ioport.h>	/* struct resource, IORESOURCE_MEM/IO */
#include <linux/pm.h>		/* struct dev_pm_ops, for pci_driver.driver.pm */
#include <linux/pci_ids.h>	/* the subvendor/subdevice pair id_tables match */

/*
 * struct pci_bus -- just the bus number. drm_file.c:991 prints
 * `pdev->bus->number` as the middle field of %02x in a BDF string, and
 * pci_domain_nr() below takes the bus, not the device. The fabric owns the
 * one instance per slot, next to the device it belongs to.
 */
struct pci_bus {
	unsigned char number;
};

/*
 * struct pci_dev -- identity plus BARs, which is the whole of what a driver
 * ported into this tree reads.
 *
 * mainline's pci_dev is a kernel object: device lists, PM state, MSI
 * vectors, config-space accessors, the driver bound to it. None of that has
 * code here -- the fabric matches once at initcall time and nothing ever
 * unbinds -- so this carries the DCL delta on purpose, after the pinned
 * struct device:
 *
 *   - vendor/device/subsystem_vendor/subsystem_device and class, so
 *     pci_match_one_device()'s quadruple test and class filter can run.
 *     class is class<<8 | subclass, mainline's spelling.
 *   - revision, because bochs_hw_init() selects its register window from
 *     `pdev->revision >= 2` (the qext block exists from revision 2 on).
 *   - resource[6], filled for a matched device only (the fabric probes sizes
 *     by writing ~0 to a BAR -- a write effect no unmatched device should
 *     see) with mainline's start/end/flags fields.
 *   - driver_data, which is where pci_set_drvdata()/pci_get_drvdata()
 *     keep the drm_device pointer. A field here rather than mainline's
 *     dev->driver_data because struct device is the pinned 800-byte blob
 *     and device.h's note explains why nothing may grow it.
 */
struct pci_dev {
	struct pci_bus* bus;
	unsigned int devfn;
	struct device dev;

	unsigned short vendor;
	unsigned short device;
	unsigned short subsystem_vendor;
	unsigned short subsystem_device;
	unsigned int class;		/* class<<8 | subclass */
	unsigned char revision;
	struct resource resource[6];	/* BARs 0..5, mainline's indices */
	void* driver_data;
};

/*
 * PCI_SLOT / PCI_FUNC -- the two bitfields packed into devfn. mainline spells
 * them the same way: slot is bits 7:3, function is bits 2:0. drm_file.c:992
 * prints both, and the fabric packs devices the same way.
 */
#define PCI_SLOT(devfn)		(((devfn) >> 3) & 0xff)
#define PCI_FUNC(devfn)		((devfn) & 0x07)

/*
 * to_pci_dev() -- mainline's spelling, container_of back from the embedded
 * struct device. drm_file.c:988 and drm_edid.c:2909/:2938 call it before
 * their dev_is_pci() test, so for a non-PCI device it still produces a
 * pointer into whatever object embeds the struct device -- which is why the
 * test, not this, is the gate.
 */
#define to_pci_dev(n) container_of(n, struct pci_dev, dev)

/*
 * dev_is_pci() -- forwards to the fabric's registry. mainline compares
 * `dev->bus` against &pci_bus_type; there is no bus_type in DCL, and the
 * truthful answer for "did this bus create that device" is membership in the
 * fabric's slot table, which DCL/pci_shim.c owns.
 *
 * Still a function taking the argument (and still NULL-safe): drm_file.c:410
 * and :987 pass dev->dev, which can be NULL, and mainline survives that only
 * because its callers never do -- this one does not have to be as hopeful.
 */
extern int dcl_pci_device_is_pci(const struct device* dev);

static inline int dev_is_pci(const struct device* dev)
{
	if (!dev)
		return 0;
	return dcl_pci_device_is_pci(dev);
}

/*
 * pci_domain_nr() -- 0, which is the correct answer for a single-segment
 * system: the fabric scans segment 0 only (AuPCIEGetDevice(0, ...)), so
 * every bus it hands out is domain 0. The argument is a struct pci_bus *
 * (drm_file.c:991 passes pdev->bus).
 */
static inline unsigned int pci_domain_nr(const struct pci_bus* bus)
{
	(void)bus;
	return 0;
}

/* ── driver binding ─────────────────────────────────────────────────────── */

/*
 * struct pci_driver -- the registration record, reduced to what the fabric
 * runs and what a driver initialises. mainline's err_handler, dynids,
 * driver_managed_dma and friends have no code here to call them.
 *
 * The nested struct device_driver is the shared tag from
 * <linux/device/driver.h> (also used by virtio_driver); only its `name` and
 * `pm` members exist, and bochs.c writes `.driver.pm` through this nesting.
 * `name` at the pci_driver level is separate and is what probes pass to
 * aperture_remove_conflicting_pci_devices().
 */
struct pci_driver {
	const char* name;
	const struct pci_device_id* id_table;
	int (*probe)(struct pci_dev* dev, const struct pci_device_id* id);
	void (*remove)(struct pci_dev* dev);
	void (*shutdown)(struct pci_dev* dev);
	struct device_driver driver;
};

/*
 * Registration is match-and-probe, run immediately: the fabric scans once,
 * then for every device whose id_table entry matches and which no earlier
 * driver took, calls ->probe. There is no deferred bus callback to simulate
 * because there is no other time for a device to appear.
 *
 * The return value is 0 even when probes fail -- mainline's
 * pci_register_driver() reports whether the *driver* registered, not whether
 * its probes succeeded, and a vendored driver keeps its initcall result
 * unread either way. A driver whose probe failed on every device is visible
 * in the boot log, not as an error code.
 */
int pci_register_driver(struct pci_driver* drv);
void pci_unregister_driver(struct pci_driver* drv);

/*
 * pcim_enable_device() -- set the command register's I/O and memory decode
 * bits. mainline's version also allocates MSI state and does power-state
 * bookkeeping; none of that exists here, and bochs's use of it is exactly
 * this decode gate before its BARs may be touched. Firmware has usually
 * enabled decode already, but the write is idempotent and is what the
 * driver is entitled to assume afterward.
 */
int pcim_enable_device(struct pci_dev* pdev);

/* ── BAR accessors ──────────────────────────────────────────────────────── */

#define pci_resource_start(dev, bar)	((dev)->resource[(bar)].start)
#define pci_resource_end(dev, bar)	((dev)->resource[(bar)].end)
#define pci_resource_flags(dev, bar)	((dev)->resource[(bar)].flags)

static inline resource_size_t pci_resource_len(const struct pci_dev* dev, int bar)
{
	return dev->resource[bar].end - dev->resource[bar].start + 1;
}

/*
 * pci_set_drvdata() / pci_get_drvdata() -- mainline stores the pointer in
 * dev->driver_data; see the note on struct pci_dev above for why this tree
 * keeps it on the pci_dev instead.
 */
static inline void pci_set_drvdata(struct pci_dev* pdev, void* data)
{
	pdev->driver_data = data;
}

static inline void* pci_get_drvdata(const struct pci_dev* pdev)
{
	return pdev->driver_data;
}

/* ── device lookup ──────────────────────────────────────────────────────── */

/*
 * pci_get_device() / pci_dev_put(), as virtio-gpu's init names them: mainline
 * walks the device list and returns a match with a reference held, the caller
 * dropping it with pci_dev_put() when done. The DCL fabric's slot table is
 * the list (DCL/pci_shim.c walks ECAM into it; nothing is ever removed), and
 * the from-argument keeps mainline's iteration contract -- resuming *after*
 * the device handed to the previous call -- because the one caller passes
 * NULL and a second caller, if it ever comes, will expect the same walk.
 *
 * The reference itself is the no-op of a bus whose devices outlive every
 * driver: there is no removal path to fence against, so get is "the pointer
 * is still valid" and put has nothing to drop. Both bodies are in
 * DCL/pci_shim.c beside the scan.
 */
struct pci_dev* pci_get_device(unsigned int vendor, unsigned int device,
			       struct pci_dev* from);
void pci_dev_put(struct pci_dev* pdev);

/*
 * pci_is_vga() -- mainline's legacy-VGA class test, body verbatim
 * (include/linux/pci.h:784 in linux-7.2.6): the device either announces the
 * display-VGA class or the old not-defined-VGA one. Only the class matters
 * here; mainline's ioport/parity extras are x86 machinery this port has no
 * registers for.
 *
 * What reads it: virtio_gpu_driver_init()'s VGA-quirk branch. The device
 * this port runs against (virtio-gpu-pci) reports 0x0380, so the answer is
 * false and the branch -- vga_get_interruptible()/vga_put() -- is skipped;
 * virtio_gpu_probe()'s aperture_remove_conflicting_pci_devices() is skipped
 * for the same reason.
 */
static inline bool pci_is_vga(struct pci_dev* pdev)
{
	if ((pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA)
		return true;

	if ((pdev->class >> 8) == PCI_CLASS_NOT_DEFINED_VGA)
		return true;

	return false;
}

#endif /* _LINUX_PCI_H */
