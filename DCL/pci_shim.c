/*
 * DCL/pci_shim.c -- the PCI bus vendored drivers register against.
 *
 * Xeneva's PCI layer (KernelAA64/pcie.c) has always had ECAM access, but its
 * shape is native: a driver is a DLL the manager loads, and the manager
 * walks the bus for it. Vendored mainline code cannot use that shape -- a
 * mainline driver hands the bus a `struct pci_driver` and the bus calls its
 * probe. This file is that bus for the slice of mainline drivers ported into
 * this tree. The first, and for now only, customer is bochs.c.
 *
 * Four jobs, and nothing else:
 *
 *   - scan the ECAM window once, lazily, on the first registration: one
 *     struct pci_dev per populated function, filled with identity fields
 *     only (vendor, device, subsystem pair, class, revision). No writes
 *     happen at scan time.
 *
 *   - match a registering driver's id_table against those devices with
 *     mainline's pci_match_one_device() rule -- vendor/device and the
 *     subsystem quadruple, then the class/class_mask filter -- run ->probe,
 *     and mark the device taken so no second driver can claim it.
 *
 *   - fill BAR resources for a matched device, at match time. That fill is
 *     the one place the bus writes to a device before a driver owns it:
 *     AuPCIEReadBAR() sizes a window by writing ~0 and restoring, which is
 *     what any enumerator does -- but it is confined to devices a driver
 *     actually asked for, not sprayed over every function during the scan.
 *
 *   - answer what a driver's runtime asks the bus: pcim_enable_device()
 *     (decode bits), devm_ioremap[_wc]() (bar windows, by AuMapMMIO -- PCI
 *     windows are device memory and are not promised to live inside the
 *     linear direct map), devm_request_*_region() (the claim table from
 *     linux_irq_shim.c, under devm's name), and dev_is_pci()'s registry
 *     query, which is how the vendored drm core tells a real PCI device
 *     from the virtio/platform devices that are everything else here.
 *
 * Single segment, PCI scan order (bus, device, function), first-registered
 * driver wins a contested device -- same as mainline's bus.
 */
#include <stddef.h>
#include <stdint.h>
#include <linux/string.h>	/* memset */

#include <pcie.h>		/* AuPCIE*: GetDevice, Read, ReadBAR */
#include <Mm/vmmngr.h>		/* AuMapMMIO, PAGE_SIZE */
#include <Drivers/uart.h>	/* UARTDebugOut */

#include <linux/pci.h>		/* struct pci_dev/pci_driver, PCI_ANY_ID */
#include <linux/devres.h>	/* the devm_* prototypes this file defines */
#include <linux/ioport.h>	/* request_mem_region/request_region */
#include <linux/errno.h>	/* -EINVAL, -ENODEV */

/*
 * Two config offsets pcie.h stops short of, plus the command-register bits
 * mainline keeps in pci_regs.h. The offset names are mainline's; pcie.h owns
 * PCI_COMMAND itself, the bits are this file's only users.
 */
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C
#define PCI_COMMAND_IO		0x1
#define PCI_COMMAND_MEMORY	0x2

/*
 * A ceiling on how many populated functions the bus remembers, not a limit
 * on the scan. QEMU's virt machine presents a handful (root complex, a
 * display, virtio functions); i.MX8MP a few more. The first boot reported
 * exactly sixteen, which was the old ceiling -- a count and a truncation
 * are the same number there -- so the ceiling moved to thirty-two and the
 * scan now reports its drops: a device past the ceiling is left unbound,
 * and the scan line says whether the bus filled it.
 */
#define DCL_PCI_MAX_DEVS 32

struct dcl_pci_slot {
	struct pci_dev dev;
	struct pci_bus bus;
	const struct pci_driver* driver;
	uint64_t ecam;			/* ECAM VA of this function */
	char busid[13];			/* "0000:bb:dd.f" */
	unsigned char bound;
};

static struct dcl_pci_slot dcl_pci_slots[DCL_PCI_MAX_DEVS];
static int dcl_pci_count;
static int dcl_pci_dropped;
static int dcl_pci_scanned;

/* ── config space ───────────────────────────────────────────────────────── */

/*
 * AuPCIERead64() takes the size explicitly, so one reader covers byte, word
 * and dword. bus/dev/func are not used by the payload (the call is an offset
 * into ECAM) but are passed through honestly.
 */
static uint64_t dcl_cfg_read(uint64_t ecam, int bus, int dev, int func,
			     int reg, int size)
{
	return AuPCIERead64(ecam, reg, size, bus, dev, func);
}

static void dcl_pci_mkbusid(char* out, int bus, int dev, int func)
{
	static const char hex[] = "0123456789abcdef";

	out[0] = '0';
	out[1] = '0';
	out[2] = '0';
	out[3] = '0';
	out[4] = ':';
	out[5] = hex[(bus >> 4) & 0xf];
	out[6] = hex[bus & 0xf];
	out[7] = ':';
	out[8] = hex[(dev >> 4) & 0xf];
	out[9] = hex[dev & 0xf];
	out[10] = '.';
	out[11] = hex[func & 0xf];
	out[12] = '\0';
}

static struct dcl_pci_slot* dcl_pci_slot_by_dev(const struct device* dev)
{
	int i;

	for (i = 0; i < dcl_pci_count; i++) {
		if (&dcl_pci_slots[i].dev.dev == dev)
			return &dcl_pci_slots[i];
	}
	return NULL;
}

static struct dcl_pci_slot* dcl_pci_slot_of(struct pci_dev* pdev)
{
	return pdev ? dcl_pci_slot_by_dev(&pdev->dev) : NULL;
}

/* ── scan ───────────────────────────────────────────────────────────────── */

/*
 * One function's identity. class arrives as one dword at 0x08 -- revision,
 * prog-if, subclass, class, little-endian -- and is re-packed the way
 * mainline stores it: class<<8 | subclass. The subsystem pair is one dword
 * at 0x2C for the same reason.
 */
static void dcl_pci_add(uint64_t ecam, int bus, int dev, int func)
{
	struct dcl_pci_slot* s;
	uint32_t classrev;
	uint32_t subsys;

	if (dcl_pci_count >= DCL_PCI_MAX_DEVS) {
		dcl_pci_dropped++;
		return;
	}

	s = &dcl_pci_slots[dcl_pci_count++];
	memset(s, 0, sizeof(*s));

	s->ecam = ecam;
	s->bus.number = (unsigned char)bus;
	s->dev.bus = &s->bus;
	s->dev.devfn = (unsigned int)((dev << 3) | func);
	s->dev.vendor = (unsigned short)dcl_cfg_read(ecam, bus, dev, func,
						     PCI_VENDOR_ID, 2);
	s->dev.device = (unsigned short)dcl_cfg_read(ecam, bus, dev, func,
						     PCI_DEVICE_ID, 2);

	classrev = (uint32_t)dcl_cfg_read(ecam, bus, dev, func,
					  PCI_REVISION_ID, 4);
	s->dev.revision = (unsigned char)(classrev & 0xff);
	s->dev.class = (((classrev >> 24) & 0xff) << 8) |
		       ((classrev >> 16) & 0xff);

	subsys = (uint32_t)dcl_cfg_read(ecam, bus, dev, func,
					PCI_SUBSYSTEM_VENDOR_ID, 4);
	s->dev.subsystem_vendor = (unsigned short)(subsys & 0xffff);
	s->dev.subsystem_device = (unsigned short)(subsys >> 16);

	dcl_pci_mkbusid(s->busid, bus, dev, func);
	s->dev.dev.kobj_name = s->busid;
}

/*
 * Read one function's identity and add it when the slot is populated.
 * Returns the function's ECAM address so the caller can read the header type
 * without looking it up again; 0 means no populated function there.
 */
static uint64_t dcl_pci_add_function(int bus, int dev, int func)
{
	uint64_t ecam = AuPCIEGetDevice(0, bus, dev, func);
	uint16_t vendor;

	if (!ecam)
		return 0;
	vendor = (uint16_t)dcl_cfg_read(ecam, bus, dev, func, PCI_VENDOR_ID, 2);
	if (vendor == 0xffff)
		return 0;
	dcl_pci_add(ecam, bus, dev, func);
	return ecam;
}

/*
 * Walk every bus the ECAM window covers (PCI_MAX_BUS of them, matching what
 * the native scanners walk), and for each device probe function 0. Further
 * functions are read only when the header type's multifunction bit says they
 * exist -- the same single extra read an enumerator spends.
 */
static void dcl_pci_scan(void)
{
	int bus, dev, func;
	int buses;

	dcl_pci_scanned = 1;

	if (!AuIsPCIeInitialized()) {
		UARTDebugOut("[dcl-pci]: no ECAM window, 0 devices\r\n");
		return;
	}

	/*
	 * Only the buses the ECAM window actually covers. AuPCIEGetDevice()
	 * will compose an address for any bus, but past the window that
	 * address is not configuration space: on QEMU's virt machine the
	 * 16 MiB window is followed by RAM, and a scan that runs off the end
	 * reads memory as vendor ids -- the first boot with the ceiling
	 * raised enumerated five hundred of those. A window of unknown size
	 * keeps the old ceiling.
	 */
	buses = AuPCIEBusCount();
	if (buses <= 0 || buses > PCI_MAX_BUS)
		buses = PCI_MAX_BUS;

	dcl_pci_count = 0;
	dcl_pci_dropped = 0;

	for (bus = 0; bus < buses; bus++) {
		for (dev = 0; dev < PCI_DEVICE_PER_BUS; dev++) {
			uint64_t ecam = dcl_pci_add_function(bus, dev, 0);

			if (!ecam)
				continue;

			if (!(dcl_cfg_read(ecam, bus, dev, 0,
					   PCI_HEADER_TYPE, 1) & 0x80))
				continue;

			for (func = 1; func < PCI_FUNCTION_PER_DEVICE; func++)
				dcl_pci_add_function(bus, dev, func);
		}
	}

	UARTDebugOut("[dcl-pci]: scanned %d device(s), %d dropped\r\n",
		     dcl_pci_count, dcl_pci_dropped);
}

/* ── BAR resources ──────────────────────────────────────────────────────── */

/*
 * Fill resource[0..5] for a device a driver has matched. The raw BAR dword
 * classifies the window before anything is written: bit 0 is I/O space,
 * bits 2:1 == 0b10 is a 64-bit memory window whose next dword is the high
 * half. Sizes come from AuPCIEReadBAR()'s probe for memory windows; I/O
 * windows are recorded with flags and base but without a size probe, because
 * no caller on this target reads one (bochs's I/O arm cannot run here -- see
 * <asm/io.h>).
 */
static void dcl_pci_setup_resources(struct dcl_pci_slot* s)
{
	int dev = PCI_SLOT(s->dev.devfn);
	int func = PCI_FUNC(s->dev.devfn);
	int bar;

	for (bar = 0; bar < 6; bar++) {
		struct resource* res = &s->dev.resource[bar];
		uint32_t raw = (uint32_t)dcl_cfg_read(s->ecam, s->bus.number,
						      dev, func,
						      PCI_BAR0 + bar * 4, 4);

		if (!raw || raw == 0xffffffff)
			continue;

		if (raw & 0x1) {
			res->flags = IORESOURCE_IO;
			res->start = raw & ~0x3u;
			res->end = res->start;
			continue;
		}

		{
			size_t size = 0;
			uint64_t start = AuPCIEReadBAR(s->ecam, s->bus.number,
						       (uint16_t)dev,
						       (uint16_t)func,
						       (size_t)bar, &size);

			res->flags = IORESOURCE_MEM;
			res->start = start;
			res->end = size ? start + size - 1 : start;
		}

		if (((raw >> 1) & 0x3) == 0x2)
			bar++;		/* 64-bit window: skip the high half */
	}
}

/* ── matching ───────────────────────────────────────────────────────────── */

/*
 * mainline's pci_match_one_device(): a table entry matches when both
 * vendor and device match (or are PCI_ANY_ID), the subsystem pair does, and
 * the class filter finds no bit outside class_mask. The zeroed entry ends
 * the table.
 */
static const struct pci_device_id*
dcl_pci_match_id(const struct pci_device_id* ids, const struct pci_dev* dev)
{
	for (; ids && (ids->vendor || ids->device || ids->subvendor ||
		       ids->subdevice || ids->class || ids->class_mask);
	     ids++) {
		if ((ids->vendor == PCI_ANY_ID || ids->vendor == dev->vendor) &&
		    (ids->device == PCI_ANY_ID || ids->device == dev->device) &&
		    (ids->subvendor == PCI_ANY_ID ||
		     ids->subvendor == dev->subsystem_vendor) &&
		    (ids->subdevice == PCI_ANY_ID ||
		     ids->subdevice == dev->subsystem_device) &&
		    !((ids->class ^ dev->class) & ids->class_mask))
			return ids;
	}
	return NULL;
}

/* ── registration ───────────────────────────────────────────────────────── */

int pci_register_driver(struct pci_driver* drv)
{
	int i;

	if (!drv)
		return -EINVAL;

	if (!dcl_pci_scanned)
		dcl_pci_scan();

	for (i = 0; i < dcl_pci_count; i++) {
		struct dcl_pci_slot* s = &dcl_pci_slots[i];
		const struct pci_device_id* id;
		int rc;

		if (s->bound)
			continue;

		id = dcl_pci_match_id(drv->id_table, &s->dev);
		if (!id)
			continue;

		dcl_pci_setup_resources(s);

		rc = drv->probe ? drv->probe(&s->dev, id) : 0;
		if (rc) {
			UARTDebugOut("[dcl-pci]: %s probe %s failed (%d)\r\n",
				     drv->name ? drv->name : "?",
				     s->busid, rc);
			continue;
		}

		s->driver = drv;
		s->bound = 1;
		UARTDebugOut("[dcl-pci]: %s bound to %s\r\n", s->busid,
			     drv->name ? drv->name : "?");
	}

	/*
	 * Success means the driver is registered, not that it found a device
	 * -- mainline's contract, and the caller (a drm initcall) keeps its
	 * result unread either way. A driver that found nothing is visible
	 * by its absence from the bind lines above.
	 */
	return 0;
}

/*
 * Nothing in this tree unregisters a driver; the shape exists because a
 * driver's registration may one day be paired with an unload, and unwinding
 * to mainline's rule (remove runs for every device the driver owns) is
 * cheaper to write now than to reconstruct later.
 */
void pci_unregister_driver(struct pci_driver* drv)
{
	int i;

	for (i = 0; i < dcl_pci_count; i++) {
		struct dcl_pci_slot* s = &dcl_pci_slots[i];

		if (!s->bound || s->driver != drv)
			continue;

		if (drv->remove)
			drv->remove(&s->dev);
		s->bound = 0;
		s->driver = NULL;
	}
}

/* ── lookup ─────────────────────────────────────────────────────────────── */

/*
 * pci_get_device() -- mainline's pair walk, over the fabric's slot table.
 *
 * The match here is identity alone: mainline returns the first device
 * carrying (vendor, device), with no subsystem or class filtering, and
 * virtio-gpu's init uses it for exactly that -- "is a 1AF4:1050 function on
 * this bus?" -- before it registers the driver that the virtio side matches
 * later. So PCI_ANY_ID is accepted on either field, as mainline does, and
 * the slot's fixed order is the walk order.
 *
 * The scan is triggered on first call rather than assumed: the contract is a
 * self-contained lookup, and a caller must not have to know that some other
 * driver's registration already enumerated the bus. from != NULL resumes
 * after the device handed in, per mainline's iteration contract; a from that
 * is not on this bus ends the search (NULL), which is a lookup answering "no
 * more devices" rather than pretending to a position it never had.
 *
 * The reference mainline takes is the no-op of a bus whose devices outlive
 * every driver -- nothing is ever removed from the slot table, so a returned
 * pointer stays valid and pci_dev_put() has nothing to drop. Both spellings
 * exist because the driver's unwind path is mainline's.
 */
struct pci_dev* pci_get_device(unsigned int vendor, unsigned int device,
			       struct pci_dev* from)
{
	int i = 0;

	if (!dcl_pci_scanned)
		dcl_pci_scan();

	if (from) {
		struct dcl_pci_slot* s = dcl_pci_slot_of(from);

		if (!s)
			return NULL;
		i = (int)(s - dcl_pci_slots) + 1;
	}

	for (; i < dcl_pci_count; i++) {
		if ((vendor == PCI_ANY_ID ||
		     vendor == dcl_pci_slots[i].dev.vendor) &&
		    (device == PCI_ANY_ID ||
		     device == dcl_pci_slots[i].dev.device))
			return &dcl_pci_slots[i].dev;
	}

	return NULL;
}

void pci_dev_put(struct pci_dev* pdev)
{
	(void)pdev;
}

int pcim_enable_device(struct pci_dev* pdev)
{
	struct dcl_pci_slot* s = dcl_pci_slot_of(pdev);
	int dev, func;
	uint32_t cmd;

	if (!s)
		return -ENODEV;

	dev = PCI_SLOT(s->dev.devfn);
	func = PCI_FUNC(s->dev.devfn);

	/* Firmware has usually enabled decode already; the set is idempotent
	 * and is the state the driver is entitled to assume after this. */
	cmd = AuPCIERead(s->ecam, PCI_COMMAND, s->bus.number, dev, func);
	AuPCIEWrite(s->ecam, PCI_COMMAND, cmd | PCI_COMMAND_IO |
		    PCI_COMMAND_MEMORY, s->bus.number, dev, func);
	return 0;
}

/* ── the devm_* answers ─────────────────────────────────────────────────── */

/*
 * A PCI window is mapped for the device, not looked up in a linear map:
 * AuMapMMIO() is the same call the native drivers make for their BARs (the
 * mapped ECAM in pcie.c, virtgpu's registers), and it is also the only
 * correct one -- the direct map covers RAM, and a BAR is somewhere else.
 * Pages are rounded up because the call takes pages; a size of zero maps
 * nothing and reports failure, which is the one answer a caller tests.
 */
static void* dcl_pci_ioremap(uint64_t phys, uint64_t size)
{
	uint64_t pages;

	if (!size)
		return NULL;

	pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	return AuMapMMIO(phys, (size_t)pages);
}

void* devm_ioremap(struct device* dev, resource_size_t offset,
		   resource_size_t size)
{
	(void)dev;
	return dcl_pci_ioremap(offset, size);
}

/* No cache-attribute split exists here: AuMapMMIO always produces device
 * memory, which is what write-combining would be used to get. */
void* devm_ioremap_wc(struct device* dev, resource_size_t offset,
		      resource_size_t size)
{
	(void)dev;
	return dcl_pci_ioremap(offset, size);
}

/*
 * "Managed" only changes who releases the claim; nothing in this tree ever
 * releases (see the note in <linux/devres.h>), so the claim itself is the
 * whole behavior -- and routing through the real claim table keeps the NULL
 * that bochs's probe tests ("someone else owns this window") meaningful.
 */
struct resource* devm_request_mem_region(struct device* dev,
					 resource_size_t start,
					 resource_size_t n, const char* name)
{
	(void)dev;
	return request_mem_region(start, n, name);
}

struct resource* devm_request_region(struct device* dev, resource_size_t start,
				     resource_size_t n, const char* name)
{
	(void)dev;
	return request_region(start, n, name);
}

/* ── dev_is_pci()'s registry ────────────────────────────────────────────── */

/*
 * The vendored drm core asks this (through the inline in <linux/pci.h>) to
 * decide whether a drm device's parent is a PCI device -- it gates the BDF
 * string in drm_file.c and the switcheroo paths in drm_edid.c. The answer
 * is membership in this file's table: only devices this bus built are PCI
 * devices, and everything else (virtio's and vkms's embedded struct device
 * blobs included) stays as it was. Pointer comparison only, so a NULL or a
 * foreign device cannot fault here.
 */
int dcl_pci_device_is_pci(const struct device* dev)
{
	return dcl_pci_slot_by_dev(dev) != NULL;
}
