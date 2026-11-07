/*
 * Process/gpusrv -- user-space virtio-GPU server.
 *
 * The process releases the fallback kernel driver, claims 1af4:1050 through
 * /dev/hwdev, and owns all virtio command submission from EL0. The kernel
 * /dev/gpusrv proxy only copies compositor damage into the DMA backing pages;
 * it never touches the GPU queues.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/_ketime.h>
#include <hwdev.h>
#include <gpusrv.h>
#include <aucon.h>
#include <drvctl.h>
#include <Drivers/virtio.h>

#define VIRTIO_F_VERSION_1        (1u << 0)       /* high feature word bit 0 */
#define VIRTQ_DESC_F_NEXT         1
#define VIRTQ_DESC_F_WRITE        2
#define VIRTIO_MSI_NO_VECTOR      0xFFFF

#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D  0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT         0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH      0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST2D  0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH     0x0106
#define VIRTIO_GPU_RESP_OK_NODATA          0x1100
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM   1

#define QSZ                 64
#define QUEUE_DMA_BYTES     65536
#define QUEUE_DESC_OFFSET   0
#define QUEUE_AVAIL_OFFSET  4096
#define QUEUE_USED_OFFSET   8192
#define QUEUE_CMD_OFFSET    12288
#define QUEUE_RESP_OFFSET   16384
#define QUEUE_IRQ_MS        2

#pragma pack(push, 1)
struct GpuHdr {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	uint32_t ctx_id;
	uint32_t padding;
};

struct GpuRect {
	uint32_t x, y, width, height;
};

struct GpuCreate2D {
	GpuHdr hdr;
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
};

struct GpuAttachEntry {
	uint64_t addr;
	uint32_t length;
	uint32_t padding;
};

struct GpuAttach {
	GpuHdr hdr;
	uint32_t resource_id;
	uint32_t nr_entries;
	GpuAttachEntry entries[GPUSRV_MAX_BACKING];
};

struct GpuSetScanout {
	GpuHdr hdr;
	GpuRect rect;
	uint32_t scanout_id;
	uint32_t resource_id;
};

struct GpuTransfer {
	GpuHdr hdr;
	GpuRect rect;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t padding;
};

struct GpuFlush {
	GpuHdr hdr;
	GpuRect rect;
	uint32_t resource_id;
	uint32_t padding;
};
#pragma pack(pop)

struct VqDesc {
	volatile uint64_t addr;
	volatile uint32_t len;
	volatile uint16_t flags;
	volatile uint16_t next;
};

struct VqAvail {
	volatile uint16_t flags;
	volatile uint16_t idx;
	volatile uint16_t ring[QSZ];
};

struct VqUsedElem {
	volatile uint32_t id;
	volatile uint32_t len;
};

struct VqUsed {
	volatile uint16_t flags;
	volatile uint16_t idx;
	volatile VqUsedElem ring[QSZ];
};

struct Queue {
	VqDesc* desc;
	VqAvail* avail;
	VqUsed* used;
	uint16_t size;
	uint16_t last_used;
	volatile uint16_t* notify;
	uint8_t* dma;
	uint64_t phys;
};

static int g_ctl = -1;
static int g_gpu = -1;
static int g_h = -1;
static volatile uint8_t* g_common;
static volatile uint8_t* g_notify;
static uint32_t g_notify_mult;
static Queue g_queue;
static uint8_t* g_cmd;
static uint8_t* g_resp;
static uint64_t g_cmd_phys;
static uint64_t g_resp_phys;
static uint32_t g_width;
static uint32_t g_height;
static uint32_t g_pitch;
static unsigned g_frames;
static unsigned g_crash_after;
static uint64_t g_generation;

static inline void dmb(void) { __asm__ volatile("dmb ish" ::: "memory"); }
static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }

static void emit(const char* line) {
	printf("%s", line);
	static int kmsg = -1;
	if (kmsg < 0)
		kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (kmsg >= 0)
		_KeWriteFile(kmsg, (void*)line, strlen(line));
}

static int drvctl(const char* command) {
	int fd = _KeOpenFile((char*)"/dev/drvctl", FILE_OPEN_WRITE);
	if (fd < 0)
		return -100;
	AuDriverCtlRequest req;
	memset(&req, 0, sizeof req);
	size_t n = strlen(command);
	if (n >= sizeof(req.command))
		n = sizeof(req.command) - 1;
	memcpy(req.command, (void*)command, n);
	int rc = _KeFileIoControl(fd, AU_DRVCTL_IOC_EXEC, &req);
	_KeCloseFile(fd);
	return rc;
}

static unsigned cfg_read(unsigned off, unsigned size) {
	HwdevCfg c = {off, size, 0};
	if (_KeFileIoControl(g_h, HWDEV_IOC_CFG_READ, &c) != HWDEV_OK)
		return 0;
	return c.value;
}

static int cfg_write(unsigned off, unsigned size, unsigned value) {
	HwdevCfg c = {off, size, value};
	return _KeFileIoControl(g_h, HWDEV_IOC_CFG_WRITE, &c);
}

static uint8_t* map_bar(unsigned bar) {
	HwdevBar info;
	HwdevMap map;
	if (bar >= 6)
		return 0;
	memset(&info, 0, sizeof info);
	info.bar = bar;
	if (_KeFileIoControl(g_h, HWDEV_IOC_BAR_INFO, &info) != HWDEV_OK || !info.size)
		return 0;
	memset(&map, 0, sizeof map);
	map.bar = bar;
	map.length = info.size;
	if (_KeFileIoControl(g_h, HWDEV_IOC_MAP_BAR, &map) != HWDEV_OK)
		return 0;
	return (uint8_t*)map.uaddr;
}

static volatile uint8_t* map_cap(unsigned type, unsigned* multiplier) {
	unsigned ptr = cfg_read(0x34, 1);
	for (unsigned guard = 0; ptr >= 0x40 && ptr < 0xFC && guard < 48; guard++) {
		unsigned id = cfg_read(ptr, 1);
		unsigned next = cfg_read(ptr + 1, 1);
		if (id == 0x09 && cfg_read(ptr + 3, 1) == type) {
			unsigned bar = cfg_read(ptr + 4, 1);
			unsigned offset = cfg_read(ptr + 8, 4);
			if (multiplier)
				*multiplier = cfg_read(ptr + 16, 4);
			uint8_t* base = map_bar(bar);
			return base ? (volatile uint8_t*)(base + offset) : 0;
		}
		ptr = next;
	}
	return 0;
}

#define C8(o)  (*(volatile uint8_t*)(g_common + (o)))
#define C16(o) (*(volatile uint16_t*)(g_common + (o)))
#define C32(o) (*(volatile uint32_t*)(g_common + (o)))

static void common_write64(unsigned lo, uint64_t value) {
	C32(lo) = (uint32_t)value;
	C32(lo + 4) = (uint32_t)(value >> 32);
}

static bool queue_up(unsigned index) {
	C16(0x16) = (uint16_t)index;
	dsb();
	unsigned max = C16(0x18);
	if (max < 3)
		return false;
	g_queue.size = (uint16_t)(max < QSZ ? max : QSZ);
	C16(0x18) = g_queue.size;

	g_queue.desc = (VqDesc*)(g_queue.dma + QUEUE_DESC_OFFSET);
	g_queue.avail = (VqAvail*)(g_queue.dma + QUEUE_AVAIL_OFFSET);
	g_queue.used = (VqUsed*)(g_queue.dma + QUEUE_USED_OFFSET);
	uint64_t desc_phys = g_queue.phys + QUEUE_DESC_OFFSET;
	uint64_t avail_phys = g_queue.phys + QUEUE_AVAIL_OFFSET;
	uint64_t used_phys = g_queue.phys + QUEUE_USED_OFFSET;
	common_write64(0x20, desc_phys);
	common_write64(0x28, avail_phys);
	common_write64(0x30, used_phys);
	C16(0x1A) = 0;                 /* hwdev routes queue vector 0 to our fd */
	dsb();
	C16(0x1C) = 1;
	dsb();
	g_queue.notify = (volatile uint16_t*)(g_notify +
		(uint32_t)C16(0x1E) * g_notify_mult);
	g_queue.last_used = 0;
	memset((void*)g_queue.avail, 0, sizeof(VqAvail));
	memset((void*)g_queue.used, 0, sizeof(VqUsed));
	return true;
}

static bool device_up(void) {
	unsigned multiplier = 0;
	if (cfg_write(0x04, 2, 0x6) != HWDEV_OK)
		return false;               /* memory decode + bus master */
	g_common = map_cap(VIRTIO_PCI_CAP_COMMON_CFG, 0);
	g_notify = map_cap(VIRTIO_PCI_CAP_NOTIFY_CFG, &multiplier);
	g_notify_mult = multiplier;
	if (!g_common || !g_notify || !g_notify_mult)
		return false;

	C8(0x14) = 0;
	dsb();
	C8(0x14) = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
	dsb();

	C32(0x00) = 0;
	dsb();
	uint32_t feature_lo = C32(0x04);
	C32(0x00) = 1;
	dsb();
	uint32_t feature_hi = C32(0x04);
	if (!(feature_hi & VIRTIO_F_VERSION_1))
		return false;
	(void)feature_lo;

	C32(0x00) = 0;
	C32(0x08) = 0;
	C32(0x0C) = 0;
	C32(0x00) = 1;
	C32(0x08) = 1;
	C32(0x0C) = VIRTIO_F_VERSION_1;
	dsb();
	C8(0x14) = C8(0x14) | VIRTIO_STATUS_FEATURES_OK;
	dsb();
	if (!(C8(0x14) & VIRTIO_STATUS_FEATURES_OK))
		return false;

	HwdevIrq irq = {0, 0};
	if (_KeFileIoControl(g_h, HWDEV_IOC_IRQ_SETUP, &irq) != HWDEV_OK)
		return false;
	C16(0x10) = VIRTIO_MSI_NO_VECTOR; /* no config-change interrupt needed */

	HwdevDma qmem = {QUEUE_DMA_BYTES, 0, 0};
	if (_KeFileIoControl(g_h, HWDEV_IOC_DMA_ALLOC, &qmem) != HWDEV_OK)
		return false;
	g_queue.dma = (uint8_t*)qmem.uaddr;
	g_queue.phys = qmem.phys;
	g_cmd = g_queue.dma + QUEUE_CMD_OFFSET;
	g_resp = g_queue.dma + QUEUE_RESP_OFFSET;
	g_cmd_phys = qmem.phys + QUEUE_CMD_OFFSET;
	g_resp_phys = qmem.phys + QUEUE_RESP_OFFSET;
	memset(g_queue.dma, 0, QUEUE_DMA_BYTES);
	if (!queue_up(0))
		return false;

	C8(0x14) = C8(0x14) | VIRTIO_STATUS_DRIVER_OK;
	dsb();
	return true;
}

static bool complete_command(const GpuHdr* request) {
	uint16_t slot = (uint16_t)(g_queue.avail->idx % g_queue.size);
	g_queue.avail->ring[slot] = 0;
	dmb();
	g_queue.avail->idx++;
	dsb();
	*g_queue.notify = 0;
	dsb();

	for (unsigned tries = 0; tries < 1000; tries++) {
		if (g_queue.last_used != g_queue.used->idx) {
			g_queue.last_used++;
			uint32_t response = ((GpuHdr*)g_resp)->type;
			if (response != VIRTIO_GPU_RESP_OK_NODATA) {
				char line[128];
				sprintf(line, "[gpusrv] command 0x%x returned response 0x%x\n",
					((const GpuHdr*)request)->type, response);
				emit(line);
			}
			return response == VIRTIO_GPU_RESP_OK_NODATA;
		}
		HwdevIrqWait wait = {QUEUE_IRQ_MS, 0};
		(void)_KeFileIoControl(g_h, HWDEV_IOC_IRQ_WAIT, &wait);
		HwdevIrqAck ack = {0, 0, 0};
		(void)_KeFileIoControl(g_h, HWDEV_IOC_IRQ_ACK, &ack);
	}
	emit("[gpusrv] virtio command timed out waiting for completion\n");
	return false;
}

static bool command(const void* request, size_t request_len) {
	if (!request || request_len > 4096)
		return false;
	memcpy(g_cmd, (void*)request, request_len);
	memset(g_resp, 0, 64);
	VqDesc* d = g_queue.desc;
	d[0].addr = g_cmd_phys;
	d[0].len = (uint32_t)request_len;
	d[0].flags = VIRTQ_DESC_F_NEXT;
	d[0].next = 1;
	d[1].addr = g_resp_phys;
	d[1].len = 64;
	d[1].flags = VIRTQ_DESC_F_WRITE;
	d[1].next = 0;
	return complete_command((const GpuHdr*)request);
}

static bool create_resource(void) {
	GpuCreate2D cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	cmd.resource_id = GPUSRV_RESOURCE;
	cmd.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
	cmd.width = g_width;
	cmd.height = g_height;
	return command(&cmd, sizeof cmd);
}

static bool attach_backing(const GpuSrvAcquire* acquired) {
	GpuAttach cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH;
	cmd.resource_id = GPUSRV_RESOURCE;
	cmd.nr_entries = acquired->backing_count;
	for (uint32_t i = 0; i < acquired->backing_count; i++) {
		cmd.entries[i].addr = acquired->backing[i].phys;
		cmd.entries[i].length = acquired->backing[i].length;
	}
	return command(&cmd, sizeof(GpuHdr) + 8 +
		(size_t)cmd.nr_entries * sizeof(GpuAttachEntry));
}

static bool set_scanout(void) {
	GpuSetScanout cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
	cmd.rect.width = g_width;
	cmd.rect.height = g_height;
	cmd.resource_id = GPUSRV_RESOURCE;
	return command(&cmd, sizeof cmd);
}

static bool present(const GpuSrvFrame* frame) {
	GpuTransfer transfer;
	memset(&transfer, 0, sizeof transfer);
	transfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST2D;
	transfer.rect.x = frame->x;
	transfer.rect.y = frame->y;
	transfer.rect.width = frame->width;
	transfer.rect.height = frame->height;
	transfer.offset = (uint64_t)frame->y * g_pitch + (uint64_t)frame->x * 4;
	transfer.resource_id = GPUSRV_RESOURCE;
	if (!command(&transfer, sizeof transfer))
		return false;

	GpuFlush flush;
	memset(&flush, 0, sizeof flush);
	flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	flush.rect = transfer.rect;
	flush.resource_id = GPUSRV_RESOURCE;
	return command(&flush, sizeof flush);
}

static void maybe_crash(void) {
#ifdef __XENEVA_SVC_TEST__
	if (g_crash_after && g_frames >= g_crash_after) {
		char line[128];
		sprintf(line, "[gpusrv] test hook: crashing on purpose after %u frames\n", g_frames);
		emit(line);
		*(volatile int*)0 = 1;
	}
#endif
}

static bool screen_size(void) {
	int fd = _KeOpenFile((char*)"/dev/graph", FILE_OPEN_READ_ONLY);
	if (fd < 0)
		return false;
	XEFileIOControl ctl;
	memset(&ctl, 0, sizeof ctl);
	ctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	if (_KeFileIoControl(fd, SCREEN_GETWIDTH, &ctl) != 0 || !ctl.uint_1)
		goto fail;
	g_width = ctl.uint_1;
	memset(&ctl, 0, sizeof ctl);
	ctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	if (_KeFileIoControl(fd, SCREEN_GETHEIGHT, &ctl) != 0 || !ctl.uint_1)
		goto fail;
	g_height = ctl.uint_1;
	if (g_width > GPUSRV_MAX_DIM || g_height > GPUSRV_MAX_DIM)
		goto fail;
	g_pitch = g_width * 4;
	_KeCloseFile(fd);
	return true;
fail:
	_KeCloseFile(fd);
	return false;
}

static bool allocate_backing(GpuSrvAcquire* acquired) {
	uint64_t remaining = (uint64_t)g_width * g_height * 4;
	uint32_t count = 0;
	while (remaining) {
		if (count >= GPUSRV_MAX_BACKING)
			return false;
		uint64_t chunk = remaining > GPUSRV_MAX_BACKING_BYTES
			? GPUSRV_MAX_BACKING_BYTES : remaining;
		HwdevDma dma = {chunk, 0, 0};
		if (_KeFileIoControl(g_h, HWDEV_IOC_DMA_ALLOC, &dma) != HWDEV_OK)
			return false;
		acquired->backing[count].phys = dma.phys;
		acquired->backing[count].length = (uint32_t)chunk;
		acquired->backing[count].reserved = 0;
		count++;
		remaining -= chunk;
	}
	acquired->backing_count = count;
	return true;
}

static void give_back(const char* why) {
	char line[180];
	sprintf(line, "[gpusrv] %s -- giving the device back to the kernel driver\n", why);
	emit(line);
	if (g_h >= 0) {
		_KeCloseFile(g_h);
		g_h = -1;
	}
	if (g_gpu >= 0) {
		_KeCloseFile(g_gpu);
		g_gpu = -1;
	}
	int rc = drvctl("load virtgpu");
	if (rc != AU_UNLOAD_OK && rc != AU_LOAD_ALREADY_PRESENT) {
		sprintf(line, "[gpusrv] kernel virtgpu handback failed rc=%d\n", rc);
		emit(line);
	}
}

int main(int argc, char* argv[]) {
	#ifdef __XENEVA_SVC_TEST__
	if (argc > 1 && argv && argv[1] && !strncmp(argv[1], "crash-after=", 12))
		for (const char* p = argv[1] + 12; *p >= '0' && *p <= '9'; p++)
			g_crash_after = g_crash_after * 10 + (unsigned)(*p - '0');
	#else
	(void)argc;
	(void)argv;
	#endif
	emit("[gpusrv] starting user-space virtio-gpu server\n");
#ifdef __XENEVA_SVC_TEST__
	if (g_crash_after)
		emit("[gpusrv] crash hook enabled\n");
#endif

	g_ctl = _KeOpenFile((char*)"/dev/hwdev", FILE_OPEN_READ_ONLY);
	g_gpu = _KeOpenFile((char*)"/dev/gpusrv", FILE_OPEN_READ_ONLY);
	if (g_ctl < 0 || g_gpu < 0) {
		emit("[gpusrv] no /dev/hwdev or /dev/gpusrv (not enabled or not in daemons group?)\n");
		return 1;
	}

	/* Release the fallback component before asking hwdev for the exclusive PCI
	 * capability. The call is idempotent during supervisor restarts: NOT_FOUND
	 * means the fallback is already absent, while all other errors are real
	 * handoff failures. */
	int unload_rc = drvctl("unload virtgpu");
	if (unload_rc != AU_UNLOAD_OK && unload_rc != AU_UNLOAD_NOT_FOUND) {
		char line[128];
		sprintf(line, "[gpusrv] kernel virtgpu unload failed rc=%d\n", unload_rc);
		emit(line);
		give_back("cannot release the kernel GPU driver");
		return 1;
	}
	HwdevPciOpen open;
	memset(&open, 0, sizeof open);
	open.vendor = GPUSRV_VENDOR;
	open.device = GPUSRV_DEVICE;
	open.instance = 0;
	open.handle = -1;
	if (_KeFileIoControl(g_ctl, HWDEV_IOC_PCI_OPEN, &open) != HWDEV_OK) {
		give_back("cannot claim the GPU");
		return 1;
	}
	g_h = open.handle;
	if (!screen_size()) {
		give_back("cannot read the display geometry");
		return 1;
	}
	if (!device_up()) {
		give_back("virtio setup failed");
		return 1;
	}

	GpuSrvAcquire acquire;
	memset(&acquire, 0, sizeof acquire);
	acquire.version = GPUSRV_VERSION;
	acquire.hw_handle = g_h;
	acquire.width = g_width;
	acquire.height = g_height;
	if (!allocate_backing(&acquire)) {
		give_back("cannot allocate GPU backing memory");
		return 1;
	}
	if (_KeFileIoControl(g_gpu, GPUSRV_IOC_ACQUIRE, &acquire) != GPUSRV_OK) {
		give_back("kernel refused the GPU backing");
		return 1;
	}
	g_width = acquire.width;
	g_height = acquire.height;
	g_pitch = acquire.pitch;
	g_generation = acquire.generation;
	{
		char line[128];
		sprintf(line, "[gpusrv] acquired generation=%u\n", (unsigned)g_generation);
		emit(line);
	}
	if (!create_resource()) {
		give_back("GPU resource creation failed");
		return 1;
	}
	if (!attach_backing(&acquire)) {
		give_back("GPU backing attachment failed");
		return 1;
	}
	if (!set_scanout()) {
		give_back("GPU scanout setup failed");
		return 1;
	}
	emit("[gpusrv] virtio-gpu hardware is now served from user space\n");

	for (;;) {
		GpuSrvFrame frame;
		memset(&frame, 0, sizeof frame);
		int rc = _KeFileIoControl(g_gpu, GPUSRV_IOC_TAKE, &frame);
		if (rc == GPUSRV_E_EMPTY || rc == GPUSRV_E_BUSY) {
			_KeProcessSleep(1);
			continue;
		}
		if (rc != GPUSRV_OK) {
			give_back("GPU proxy stopped serving frames");
			return 1;
		}
		if (!present(&frame)) {
			give_back("virtio-gpu presentation failed");
			return 1;
		}
		GpuSrvAck ack = {frame.sequence};
		if (_KeFileIoControl(g_gpu, GPUSRV_IOC_ACK, &ack) != GPUSRV_OK) {
			give_back("GPU proxy rejected frame completion");
			return 1;
		}
		if (++g_frames == 1 || (g_frames & 63) == 0) {
			char line[96];
			sprintf(line, "[gpusrv] presented generation=%u frame=%u\n",
				(unsigned)g_generation, g_frames);
			emit(line);
		}
		maybe_crash();
	}
}
