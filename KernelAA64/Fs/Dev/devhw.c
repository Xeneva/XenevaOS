/**
 * devhw.c -- /dev/hwdev: capability-gated hardware access for user-space drivers
 *
 * See BaseHdr/hwdev.h for the contract. Summary: PCI_OPEN on the controller
 * node returns a new fd that is a CAP_OBJ_PCIDEV capability for one PCI
 * function; config access, BAR mapping and DMA buffers are ioctls on that fd,
 * each bounded by the device. The capability table releases everything when
 * the fd is closed or the process exits (AuHwdevCapReleased).
 */
#include <Fs/Dev/devhw.h>
#include <Fs/Dev/devfs.h>
#include <Fs/vfs.h>
#include <Cap/capability.h>
#include <Cred/group.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/gic.h>
#include <Serv/sysserv.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Drivers/uart.h>
#include <Drivers/virtio.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <aucomponent.h>
#include <Sound/sndproxy.h>
#include <Net/netproxy.h>
#include <Fs/Dev/devgpu.h>
#include <hwdev.h>
#include <pcie.h>
#include <string.h>
#include <_null.h>

#define HW_MAX_CLAIMS    32
#define HW_MAX_MAPS      16
#define HW_MAX_DMA_PAGES 256     /* 1 MiB per buffer */
#define HW_MAX_DMAS      16

#define PCI_CMD_MEM      (1u << 1)
#define PCI_CMD_MASTER   (1u << 2)
#define PCI_CMD_INTXOFF  (1u << 10)
#define PCI_CMD_IO       (1u << 0)

typedef struct _hw_map_ {
	struct _hw_map_* next;
	uint64_t uaddr;
	uint64_t len;
	uint8_t bar;
	uint64_t offset;
} HwMap;

typedef struct _hw_dma_ {
	struct _hw_dma_* next;
	uint64_t uaddr;
	uint64_t phys;
	uint64_t pages;
} HwDma;

typedef struct _hw_bar_ {
	uint64_t base;
	uint64_t size;
	uint32_t flags;
} HwBar;

typedef struct _hw_pci_ {
	int bus, dev, func;
	uint16_t vendor, device_id;
	uint64_t device;
	HwBar bars[6];
	HwMap* maps;
	HwDma* dmas;
	int nmaps, ndmas;
	AuProcess* owner;
	/* interrupt delivery to user space */
	int spi;                          /* -1 until IRQ_SETUP */
	volatile uint32_t irq_pending;
	volatile uint64_t irq_total;
	AA64Thread* volatile irq_waiter;
} HwPci;

static HwPci* claims[HW_MAX_CLAIMS];

#define HW_MAX_SPI 1024
static HwPci* spi_owner[HW_MAX_SPI];

/* Default deny: only PCI functions the integrator has explicitly handed to
 * user space can be opened. In-kernel claim tracking cannot see devices bound
 * by the Linux-compat (DCL) drivers, so it is a second layer, not the gate. */
static const struct { uint16_t vendor, device; } hw_allow[] = {
	{0x1AF4, 0x1050},   /* virtio-gpu (modern) */
	{0x1AF4, 0x1059},   /* virtio-sound (modern) */
	{0x1AF4, 0x1052},   /* virtio-input (modern): keyboard and tablet */
	{0x1AF4, 0x1041},   /* virtio-net (modern) */
};

static bool hw_allowed_device(uint16_t vendor, uint16_t device) {
	for (size_t i = 0; i < sizeof(hw_allow) / sizeof(hw_allow[0]); i++)
		if (hw_allow[i].vendor == vendor && hw_allow[i].device == device)
			return true;
	return false;
}

bool AuHwdevClaimsPci(int bus, int dev, int func) {
	for (int i = 0; i < HW_MAX_CLAIMS; i++) {
		HwPci* h = claims[i];
		if (h && h->bus == bus && h->dev == dev && h->func == func)
			return true;
	}
	return false;
}

AuProcess* AuHwdevCurrentProcess(void) {
	AA64Thread* thr = AuGetCurrentThread();
	if (!thr)
		return NULL;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc)
		proc = AuProcessFindSubThread(thr);
	return proc;
}

bool AuHwdevCallerAllowed(AuProcess* proc) {
	if (!proc)
		return false;
	GID_NUM want = AuCredGetGroupID(AURORA_GID_MISC_DAEMONS);
	if (proc->creds.gid == want)
		return true;
	for (int i = 0; i < proc->creds.num_sgid; i++)
		if (proc->creds.sgid[i] == want)
			return true;
	return false;
}

/* kernel addresses live in the upper half; a user pointer must not */
static bool hw_user_ptr(const void* p, size_t len) {
	uint64_t a = (uint64_t)p;
	return a && a < 0x0000800000000000ULL && a + len > a && a + len < 0x0000800000000000ULL;
}

static uint32_t hw_cfg_read(HwPci* h, uint32_t off, uint32_t size) {
	return (uint32_t)AuPCIERead64(h->device, off, size, h->bus, h->dev, h->func);
}

static void hw_cfg_write(HwPci* h, uint32_t off, uint32_t size, uint32_t val) {
	AuPCIEWrite64(h->device, off, size, val, h->bus, h->dev, h->func);
}

static bool hw_find_virtio_cap(HwPci* h, uint8_t type, uint8_t* bar,
	uint32_t* offset, uint32_t* length) {
	uint32_t ptr = hw_cfg_read(h, PCI_CAPABILITIES_PTR, 1) & 0xFF;
	for (uint32_t count = 0; ptr && count < 48; count++) {
		if (ptr < 0x40 || ptr > 0xFC || (ptr & 1))
			return false;
		uint32_t id = hw_cfg_read(h, ptr, 1) & 0xFF;
		uint32_t next = hw_cfg_read(h, ptr + 1, 1) & 0xFF;
		if (id == VIRTIO_PCI_CAP_VENDOR_ID) {
			uint32_t cap_len = hw_cfg_read(h, ptr + 2, 1) & 0xFF;
			if (cap_len < 16 || ptr + cap_len > 0x100)
				return false;
			if ((hw_cfg_read(h, ptr + 3, 1) & 0xFF) == type) {
				*bar = (uint8_t)(hw_cfg_read(h, ptr + 4, 1) & 0xFF);
				*offset = hw_cfg_read(h, ptr + 8, 4);
				*length = hw_cfg_read(h, ptr + 12, 4);
				return true;
			}
		}
		ptr = next;
	}
	return false;
}

/* The server maps the modern common-config capability before it allocates
 * queues. Use that device mapping during teardown so the kernel can reset the
 * device without consuming another permanent AuMapMMIO window per restart. */
static bool hw_reset_virtio_gpu(HwPci* h) {
	if (h->vendor != 0x1AF4 || h->device_id != 0x1050)
		return true;
	uint8_t bar;
	uint32_t offset, length;
	if (!hw_find_virtio_cap(h, VIRTIO_PCI_CAP_COMMON_CFG, &bar, &offset, &length) ||
		bar >= 6 || !length || offset + 0x15 < offset)
		return false;
	for (HwMap* m = h->maps; m; m = m->next) {
		if (m->bar != bar || offset < m->offset)
			continue;
		uint64_t within = (uint64_t)offset - m->offset;
		if (within + 0x15 < within || within + 0x15 > m->len)
			continue;
		volatile uint8_t* common = (volatile uint8_t*)(m->uaddr + within);
		common[0x14] = 0;
		dsb_ish();
		isb_flush();
		uint8_t status = common[0x14];
		if (status != 0)
			UARTDebugOut("[hwdev]: virtio-gpu reset status remained %x\r\n", status);
		else
			UARTDebugOut("[hwdev]: virtio-gpu reset complete\r\n");
		return status == 0;
	}
	/* No common-config mapping means the server could not have configured a
	 * queue through this capability. The PCI stop below still prevents new DMA,
	 * but report the missing reset for diagnosis rather than dereferencing an
	 * unknown virtual address. */
	UARTDebugOut("[hwdev]: virtio-gpu release has no common-config mapping\r\n");
	return false;
}

static void hw_stop_device(HwPci* h) {
	/* In-flight virtio descriptors must be invalidated while the common config
	 * BAR is still decoded. Only then is it safe to disable bus mastering and
	 * release the server-owned descriptor and backing pages. */
	if (!hw_reset_virtio_gpu(h))
		UARTDebugOut("[hwdev]: continuing GPU release after reset could not be verified\r\n");
	/* no more DMA, no more MMIO decode, before any of its memory is freed */
	uint32_t cmd = hw_cfg_read(h, 0x04, 2);
	cmd &= ~(PCI_CMD_MEM | PCI_CMD_MASTER | PCI_CMD_IO);
	hw_cfg_write(h, 0x04, 2, cmd);
}

bool AuHwdevOwnsDma(AuProcess* proc, uint64_t phys, uint64_t len) {
	if (!proc || !len || phys + len < phys)
		return false;
	for (int i = 0; i < FILE_DESC_PER_PROCESS; i++) {
		AuCapability* cap = &proc->caps[i];
		if (!cap->valid || cap->object_type != CAP_OBJ_PCIDEV)
			continue;
		HwPci* h = (HwPci*)cap->object;
		for (HwDma* d = h ? h->dmas : NULL; d; d = d->next)
			if (phys >= d->phys && phys + len <= d->phys + d->pages * 4096)
				return true;
	}
	return false;
}

/* find the capability for this handle node in the caller's table */
static AuCapability* hw_cap_for(AuProcess* proc, AuVFSNode* file) {
	for (int i = 0; i < FILE_DESC_PER_PROCESS; i++)
		if (proc->fds[i] == file && proc->caps[i].valid &&
			proc->caps[i].object_type == CAP_OBJ_PCIDEV)
			return &proc->caps[i];
	return NULL;
}

static HwPci* hw_handle_for(AuProcess* proc, int fd) {
	if (!proc || fd < 0 || fd >= FILE_DESC_PER_PROCESS)
		return NULL;
	AuVFSNode* file = proc->fds[fd];
	if (!file)
		return NULL;
	AuCapability* cap = hw_cap_for(proc, file);
	return cap ? (HwPci*)cap->object : NULL;
}

bool AuHwdevHandleIsDevice(AuProcess* proc, int fd, uint16_t vendor,
	uint16_t device) {
	HwPci* h = hw_handle_for(proc, fd);
	return h && h->vendor == vendor && h->device_id == device;
}

void* AuHwdevHandleObject(AuProcess* proc, int fd) {
	return (void*)hw_handle_for(proc, fd);
}

bool AuHwdevHandleOwnsDma(AuProcess* proc, int fd, uint64_t phys,
	uint64_t len) {
	HwPci* h = hw_handle_for(proc, fd);
	if (!h || !len || phys + len < phys)
		return false;
	for (HwDma* d = h->dmas; d; d = d->next)
		if (phys >= d->phys && phys + len <= d->phys + d->pages * 4096)
			return true;
	return false;
}

static uint64_t hw_reserve_va(AuProcess* proc, uint64_t len) {
	AuAcquireSpinlock(proc->mmap_lock);
	uint64_t va = proc->mmap_next;
	proc->mmap_next += len;
	proc->proc_mmap_len += len;
	AuReleaseSpinlock(proc->mmap_lock);
	return va;
}

/* probe BAR sizes with decode off, per the PCI spec, then restore */
static void hw_probe_bars(HwPci* h) {
	uint32_t cmd = hw_cfg_read(h, 0x04, 2);
	hw_cfg_write(h, 0x04, 2, cmd & ~(PCI_CMD_MEM | PCI_CMD_IO));
	for (int b = 0; b < 6; b++) {
		size_t sz = 0;
		uint32_t raw = hw_cfg_read(h, 0x10 + b * 4, 4);
		h->bars[b].flags = 0;
		if (raw == 0 || raw == 0xFFFFFFFF)
			continue;
		if (raw & 1) {            /* I/O BAR: reported, not mappable */
			h->bars[b].flags = HWDEV_BAR_IO;
			continue;
		}
		uint64_t base = AuPCIEReadBAR(h->device, h->bus, h->dev, h->func, b, &sz);
		h->bars[b].base = base;
		h->bars[b].size = sz;
		h->bars[b].flags = HWDEV_BAR_MEM | (((raw >> 1) & 3) == 2 ? HWDEV_BAR_64BIT : 0);
		if (((raw >> 1) & 3) == 2)
			b++;                  /* the next BAR slot is the high half */
	}
	hw_cfg_write(h, 0x04, 2, cmd);
}

/* Find the Nth PCI function with the given ids. Returns its config address, or 0. */
static uint64_t hw_find_pci(uint16_t vendor, uint16_t device, uint32_t instance,
							int* bus_, int* dev_, int* func_) {
	uint32_t seen = 0;
	for (int bus = 0; bus < 0x20; bus++)
		for (int dev = 0; dev < 32; dev++)
			for (int func = 0; func < 8; func++) {
				uint64_t addr = AuPCIEGetDevice(0, bus, dev, func);
				if (!addr || addr == 0xFFFFFFFFFFFFFFFFULL)
					continue;
				uint32_t v = (uint32_t)AuPCIERead(addr, PCI_VENDOR_ID, bus, dev, func);
				uint32_t d = (uint32_t)AuPCIERead(addr, PCI_DEVICE_ID, bus, dev, func);
				if ((v & 0xFFFF) != vendor || (d & 0xFFFF) != device)
					continue;
				if (seen++ == instance) {
					*bus_ = bus;
					*dev_ = dev;
					*func_ = func;
					return addr;
				}
			}
	return 0;
}

/* Runs in interrupt context: count it and wake the waiter. The vector code
 * sends the end-of-interrupt itself after the handler returns. */
static void hw_irq_handler(int spi) {
	HwPci* h = (spi >= 0 && spi < HW_MAX_SPI) ? spi_owner[spi] : NULL;
	if (h) {
		h->irq_pending++;
		h->irq_total++;
		AA64Thread* w = h->irq_waiter;
		if (w) {
			h->irq_waiter = NULL;
			AuThreadMakeReady(w);
		}
	}
}

/* ---------------------------------------------------------------------
 * ioctls on a device handle
 * --------------------------------------------------------------------- */
static int hw_handle_ioctl(AuVFSNode* file, int code, void* arg) {
	AuProcess* proc = AuHwdevCurrentProcess();
	HwPci* h = file ? (HwPci*)file->private_data : NULL;
	if (!proc || !h || h->owner != proc)
		return HWDEV_E_PERM;
	AuCapability* cap = hw_cap_for(proc, file);
	if (!cap)
		return HWDEV_E_PERM;

	switch (code) {
	case HWDEV_IOC_CFG_READ:
	case HWDEV_IOC_CFG_WRITE: {
		if (!(cap->rights & CAP_HW_CFG))
			return HWDEV_E_PERM;
		if (!hw_user_ptr(arg, sizeof(HwdevCfg)))
			return HWDEV_E_INVAL;
		HwdevCfg* c = (HwdevCfg*)arg;
		uint32_t off = c->offset, size = c->size;
		if ((size != 1 && size != 2 && size != 4) || (off % size) || off + size > 0x100)
			return HWDEV_E_INVAL;
		if (code == HWDEV_IOC_CFG_READ) {
			c->value = hw_cfg_read(h, off, size);
			return HWDEV_OK;
		}
		if (off == 0x04 && size == 2) {
			/* command register: only memory decode, bus master, INTx disable */
			uint32_t cur = hw_cfg_read(h, 0x04, 2);
			uint32_t allowed = PCI_CMD_MEM | PCI_CMD_MASTER | PCI_CMD_INTXOFF;
			hw_cfg_write(h, 0x04, 2, (cur & ~allowed) | (c->value & allowed));
			return HWDEV_OK;
		}
		if (off >= 0x40) {        /* capability space (MSI-X, virtio caps, ...) */
			hw_cfg_write(h, off, size, c->value);
			return HWDEV_OK;
		}
		return HWDEV_E_INVAL;     /* BARs, ROM, bridge windows, ids: never */
	}
	case HWDEV_IOC_BAR_INFO: {
		if (!hw_user_ptr(arg, sizeof(HwdevBar)))
			return HWDEV_E_INVAL;
		HwdevBar* b = (HwdevBar*)arg;
		if (b->bar >= 6)
			return HWDEV_E_INVAL;
		b->base = h->bars[b->bar].base;
		b->size = h->bars[b->bar].size;
		b->flags = h->bars[b->bar].flags;
		return HWDEV_OK;
	}
	case HWDEV_IOC_MAP_BAR: {
		if (!(cap->rights & CAP_HW_MAP))
			return HWDEV_E_PERM;
		if (!hw_user_ptr(arg, sizeof(HwdevMap)))
			return HWDEV_E_INVAL;
		HwdevMap* m = (HwdevMap*)arg;
		if (m->bar >= 6 || !(h->bars[m->bar].flags & HWDEV_BAR_MEM) || !h->bars[m->bar].size)
			return HWDEV_E_INVAL;
		HwBar* bar = &h->bars[m->bar];
		uint64_t len = (m->length + 4095) & ~4095ULL;
		if ((m->offset & 4095) || !m->length || len < m->length ||
			m->offset + len < m->offset || m->offset + len > ((bar->size + 4095) & ~4095ULL))
			return HWDEV_E_INVAL;
		if (h->nmaps >= HW_MAX_MAPS)
			return HWDEV_E_NOMEM;
		HwMap* rec = (HwMap*)kmalloc(sizeof(HwMap));
		if (!rec)
			return HWDEV_E_NOMEM;
		uint64_t va = hw_reserve_va(proc, len);
		for (uint64_t p = 0; p < len; p += 4096)
			AuMapPage(bar->base + m->offset + p, va + p, PTE_DEVICE_MEM | PTE_AP_RW_USER);
		isb_flush();
		dsb_ish();
		rec->uaddr = va;
		rec->len = len;
		rec->bar = m->bar;
		rec->offset = m->offset;
		rec->next = h->maps;
		h->maps = rec;
		h->nmaps++;
		m->uaddr = va;
		return HWDEV_OK;
	}
	case HWDEV_IOC_DMA_ALLOC: {
		if (!(cap->rights & CAP_HW_DMA))
			return HWDEV_E_PERM;
		if (!hw_user_ptr(arg, sizeof(HwdevDma)))
			return HWDEV_E_INVAL;
		HwdevDma* d = (HwdevDma*)arg;
		uint64_t pages = (d->length + 4095) / 4096;
		if (!d->length || pages > HW_MAX_DMA_PAGES)
			return HWDEV_E_INVAL;
		if (h->ndmas >= HW_MAX_DMAS)
			return HWDEV_E_NOMEM;
		HwDma* rec = (HwDma*)kmalloc(sizeof(HwDma));
		if (!rec)
			return HWDEV_E_NOMEM;
		uint64_t phys = AuPmmngrAllocPages((uint32_t)pages, 1, UINT64_MAX, AURORA_PAGE_NORMAL);
		if (!phys || phys == PMM_INVALID_PHYS) {
			kfree(rec);
			return HWDEV_E_NOMEM;
		}
		memset((void*)P2V(phys), 0, pages * 4096);
		aa64_data_cache_clean_range((void*)P2V(phys), pages * 4096);
		uint64_t va = hw_reserve_va(proc, pages * 4096);
		/* DMA buffers are shared with devices. Keep the user mapping
		 * non-cacheable so user-space drivers do not need privileged cache
		 * maintenance instructions for descriptor, command, or response data. */
		for (uint64_t p = 0; p < pages; p++)
			AuMapPage(phys + p * 4096, va + p * 4096,
				PTE_NORMAL_NON_CACHEABLE | PTE_AP_RW_USER);
		isb_flush();
		dsb_ish();
		rec->uaddr = va;
		rec->phys = phys;
		rec->pages = pages;
		rec->next = h->dmas;
		h->dmas = rec;
		h->ndmas++;
		d->uaddr = va;
		d->phys = phys;
		return HWDEV_OK;
	}
	case HWDEV_IOC_IRQ_SETUP: {
		if (!(cap->rights & CAP_HW_IRQ))
			return HWDEV_E_PERM;
		if (!hw_user_ptr(arg, sizeof(HwdevIrq)))
			return HWDEV_E_INVAL;
		if (h->spi >= 0)
			return HWDEV_E_BUSY;
		int spi = AuGICAllocateSPI();
		if (spi <= 0 || spi >= HW_MAX_SPI || spi_owner[spi])
			return HWDEV_E_NOMEM;
		/* program the device's MSI/MSI-X to raise this SPI */
		if (!AuPCIEAllocMSI(h->device, (size_t)spi, h->bus, h->dev, h->func))
			return HWDEV_E_NODEV;
		spi_owner[spi] = h;
		h->spi = spi;
		h->irq_pending = 0;
		GICRegisterSPIHandler((void*)hw_irq_handler, spi);
		GICEnableSPIIRQ((uint32_t)spi);
		((HwdevIrq*)arg)->spi = (uint32_t)spi;
		UARTDebugOut("[hwdev]: irq %d set up for %x:%x.%x\r\n", spi, h->bus, h->dev, h->func);
		return HWDEV_OK;
	}
	case HWDEV_IOC_IRQ_WAIT: {
		if (!(cap->rights & CAP_HW_IRQ) || h->spi < 0)
			return HWDEV_E_PERM;
		if (!hw_user_ptr(arg, sizeof(HwdevIrqWait)))
			return HWDEV_E_INVAL;
		uint32_t ms = ((HwdevIrqWait*)arg)->timeout_ms;
		if (ms < 1) ms = 1;
		if (ms > 1000) ms = 1000;
		AA64Thread* thr = AuGetCurrentThread();
		/* Check and sleep with interrupts masked so a delivery cannot slip in
		 * between them. If one is already pending, return at once; otherwise
		 * sleep until the handler wakes this thread or the timeout expires.
		 * The previous interrupt state is restored, never forced on: the syscall
		 * exit path trusts the live SPSR, which an interrupt taken inside a
		 * handler that unmasked on its own would overwrite. */
		uint64_t daif = read_daif();
		mask_irqs();
		if (h->irq_pending || !thr) {
			restore_daif(daif);
			return HWDEV_OK;
		}
		h->irq_waiter = thr;
		AuSleepThread(thr, ms);
		AuScheduleThread(AA64GetCurrentRegCtx());   /* resumes after the svc */
		return HWDEV_OK;                            /* value is lost: use IRQ_ACK */
	}
	case HWDEV_IOC_IRQ_ACK: {
		if (!(cap->rights & CAP_HW_IRQ) || h->spi < 0)
			return HWDEV_E_PERM;
		if (!hw_user_ptr(arg, sizeof(HwdevIrqAck)))
			return HWDEV_E_INVAL;
		HwdevIrqAck* a = (HwdevIrqAck*)arg;
		uint64_t daif = read_daif();
		mask_irqs();
		a->pending = h->irq_pending;
		h->irq_pending = 0;
		a->total = h->irq_total;
		restore_daif(daif);
		return HWDEV_OK;
	}
	}
	return HWDEV_E_INVAL;
}

/* ---------------------------------------------------------------------
 * release: runs from the capability table on close and on process exit
 * --------------------------------------------------------------------- */
void AuHwdevCapReleased(void* procptr, AuCapability* cap) {
	AuProcess* proc = (AuProcess*)procptr;
	HwPci* h = cap ? (HwPci*)cap->object : NULL;
	if (!proc || !h)
		return;
	/* the sound proxy may be reading a ring inside this handle's DMA */
	AuSndProxyOwnerGone(proc);
	AuNetProxyOwnerGone(proc);
	hw_stop_device(h);
	/* GPU proxy teardown must happen after bus mastering is stopped and before
	 * this handle's DMA buffers are released. It is a no-op until GPUSRV's
	 * hardware handoff slice is enabled. */
	AuGpuProxyOwnerGone(proc, h);
	if (h->spi >= 0) {
		/* the device is quiet now; stop routing its interrupt here */
		uint64_t daif = read_daif();
		mask_irqs();
		spi_owner[h->spi] = NULL;
		GICClearSPIHandler(h->spi);
		h->irq_waiter = NULL;
		restore_daif(daif);
		AuGICDeallocateSPI(h->spi);        /* give the interrupt number back for reuse */
		h->spi = -1;
	}
	for (HwMap* m = h->maps; m;) {
		HwMap* next = m->next;
		AuFreePagesEx(proc->cr3, m->uaddr, false, m->len);
		kfree(m);
		m = next;
	}
	for (HwDma* d = h->dmas; d;) {
		HwDma* next = d->next;
		AuFreePagesEx(proc->cr3, d->uaddr, false, d->pages * 4096);
		AuPmmngrReleasePages(d->phys);
		kfree(d);
		d = next;
	}
	for (int i = 0; i < HW_MAX_CLAIMS; i++)
		if (claims[i] == h)
			claims[i] = NULL;
	UARTDebugOut("[hwdev]: released %x:%x.%x (pid %d)\r\n", h->bus, h->dev, h->func, proc->proc_id);
	kfree(h);
}

/* ---------------------------------------------------------------------
 * controller node: /dev/hwdev
 * --------------------------------------------------------------------- */
static int hw_ctrl_ioctl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	if (code != HWDEV_IOC_PCI_OPEN)
		return HWDEV_E_INVAL;
	AuProcess* proc = AuHwdevCurrentProcess();
	if (!AuHwdevCallerAllowed(proc)) {
		UARTDebugOut("[hwdev]: pci_open denied for pid %d\r\n", proc ? proc->proc_id : -1);
		return HWDEV_E_PERM;
	}
	if (!hw_user_ptr(arg, sizeof(HwdevPciOpen)))
		return HWDEV_E_INVAL;
	HwdevPciOpen* req = (HwdevPciOpen*)arg;
	if (req->instance > 7)
		return HWDEV_E_INVAL;
	if (!hw_allowed_device(req->vendor, req->device)) {
		UARTDebugOut("[hwdev]: %x:%x is not on the allowlist\r\n", req->vendor, req->device);
		return HWDEV_E_PERM;
	}

	int bus = 0, dev = 0, func = 0;
	uint64_t device = hw_find_pci(req->vendor, req->device, req->instance, &bus, &dev, &func);
	if (!device)
		return HWDEV_E_NODEV;

	/* an in-kernel driver or another handle already owns it */
	if (AuDriverClaimsPci(bus, dev, func))
		return HWDEV_E_BUSY;
	int slot = -1;
	for (int i = 0; i < HW_MAX_CLAIMS; i++) {
		if (!claims[i] && slot < 0)
			slot = i;
	}
	if (AuHwdevClaimsPci(bus, dev, func))
		return HWDEV_E_BUSY;
	if (slot < 0)
		return HWDEV_E_NOMEM;

	int fd = AuProcessGetFileDesc(proc);
	if (fd < 0)
		return HWDEV_E_NOMEM;
	HwPci* h = (HwPci*)kmalloc(sizeof(HwPci));
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!h || !node) {
		if (h) kfree(h);
		if (node) kfree(node);
		return HWDEV_E_NOMEM;
	}
	memset(h, 0, sizeof(*h));
	memset(node, 0, sizeof(*node));
	h->bus = bus; h->dev = dev; h->func = func; h->vendor = req->vendor;
	h->device_id = req->device; h->device = device; h->owner = proc;
	h->spi = -1;
	hw_probe_bars(h);

	strcpy(node->filename, "hwdev-pci");
	node->flags = FS_FLAG_GENERAL;       /* freed by the normal close / exit paths */
	node->uid = proc->creds.uid;
	node->gid = proc->creds.gid;
	node->fileCopyCount = 0;
	node->private_data = h;
	node->iocontrol = hw_handle_ioctl;

	proc->fds[fd] = node;
	BordoisilaCapCreate(proc, fd, h, CAP_OBJ_PCIDEV, CAP_HW_CFG | CAP_HW_MAP | CAP_HW_DMA | CAP_HW_IRQ);
	claims[slot] = h;
	req->handle = fd;
	UARTDebugOut("[hwdev]: pid %d opened %x:%x.%x (%x:%x) as fd %d\r\n", proc->proc_id,
				 bus, dev, func, req->vendor, req->device, fd);
	return HWDEV_OK;
}

void AuHwdevInitialize(void) {
	AuVFSNode* fsys = AuVFSFind("/dev");
	if (!fsys)
		return;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "hwdev");
	node->flags = FS_FLAG_DEVICE;
	node->uid = 0;
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_DAEMONS);
	node->device = fsys;
	node->iocontrol = hw_ctrl_ioctl;
	AuDevFSAddFile(fsys, "/", node);
	UARTDebugOut("[hwdev]: /dev/hwdev registered\r\n");
}
