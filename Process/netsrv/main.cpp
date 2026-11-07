/*
 * Process/netsrv/main.cpp -- user-space virtio-net driver.
 *
 * Takes the virtio-net PCI function away from the kernel's driver and drives it
 * from EL0:
 *
 *   1. release the in-kernel driver (/dev/drvctl "unload virtnet"), then claim
 *      the PCI function through /dev/hwdev
 *   2. map the BAR, walk the virtio capabilities, run the virtio 1.0 handshake,
 *      set up the receive and transmit queues and the receive interrupt
 *   3. allocate the frame rings (BaseHdr/netsrv.h) and attach them to
 *      /dev/netsrv; the kernel registers the e1000/virtio-net adapters
 *   4. loop: frames the network stack queued in the TX ring go to the device,
 *      frames the device received go into the RX ring for the stack
 *
 * Runs as a MISC_DAEMONS process under svcmgr. If setup fails after the
 * in-kernel driver was released it loads it back, so networking is never lost.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/_ketime.h>
#include <hwdev.h>
#include <netsrv.h>

#define NET_VENDOR     0x1AF4
#define NET_DEVICE     0x1041

#define VIRTIO_STATUS_ACK         1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTQ_DESC_F_WRITE        2

#define QSZ        32                    /* descriptors per queue */
#define BUF_SIZE   2048
#define NET_HDR    12                    /* virtio_net_hdr with num_buffers */

struct VqDesc  { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct VqAvail { uint16_t flags; uint16_t idx; uint16_t ring[QSZ]; };
struct VqUsed  { uint16_t flags; uint16_t idx; struct { uint32_t id; uint32_t len; } ring[QSZ]; };

struct Queue {
	VqDesc* desc;
	VqAvail* avail;
	VqUsed* used;
	uint8_t* bufs;                       /* QSZ buffers of BUF_SIZE */
	uint16_t last_used;
	volatile uint16_t* notify;
};

static int g_ctl = -1, g_netsrv = -1, g_h = -1, g_kmsg = -1;
static uint8_t* g_bar[6];
static volatile uint8_t *g_common, *g_notify, *g_devcfg;
static uint32_t g_notify_mult;
static Queue g_rx, g_tx;
static NetRingHdr* g_ring;
static uint8_t* g_txslots;
static uint8_t* g_rxslots;
static uint16_t g_tx_posted;             /* tx descriptors handed to the device */
static unsigned g_tx_frames, g_rx_frames, g_irqs, g_rx_dropped, g_tx_bad;
static unsigned g_crash_after;           /* test hook, 0 = off */

static inline void dmb(void) { __asm__ volatile("dmb ish" ::: "memory"); }
static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }

static void emit(const char* line) {
	printf("%s", line);
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

static int drvctl(const char* cmd) {
	int fd = _KeOpenFile((char*)"/dev/drvctl", FILE_OPEN_WRITE);
	if (fd < 0)
		return -1;
	int n = (int)_KeWriteFile(fd, (void*)cmd, strlen(cmd));
	_KeCloseFile(fd);
	return n;
}

static unsigned cfg_read(unsigned off, unsigned size) {
	HwdevCfg c = {off, size, 0};
	_KeFileIoControl(g_h, HWDEV_IOC_CFG_READ, &c);
	return c.value;
}

static int cfg_write(unsigned off, unsigned size, unsigned val) {
	HwdevCfg c = {off, size, val};
	return _KeFileIoControl(g_h, HWDEV_IOC_CFG_WRITE, &c);
}

static uint8_t* map_bar(unsigned bar) {
	if (bar >= 6)
		return 0;
	if (g_bar[bar])
		return g_bar[bar];
	HwdevBar bi;
	memset(&bi, 0, sizeof bi);
	bi.bar = bar;
	if (_KeFileIoControl(g_h, HWDEV_IOC_BAR_INFO, &bi) != HWDEV_OK || !bi.size)
		return 0;
	HwdevMap mp;
	memset(&mp, 0, sizeof mp);
	mp.bar = bar;
	mp.length = bi.size;
	if (_KeFileIoControl(g_h, HWDEV_IOC_MAP_BAR, &mp) != HWDEV_OK)
		return 0;
	g_bar[bar] = (uint8_t*)mp.uaddr;
	return g_bar[bar];
}

static volatile uint8_t* map_cap(unsigned type, unsigned* extra) {
	unsigned ptr = cfg_read(0x34, 1);
	for (int guard = 0; ptr >= 0x40 && ptr < 0xFC && guard < 48; guard++) {
		unsigned id = cfg_read(ptr, 1), nxt = cfg_read(ptr + 1, 1);
		if (id == 0x09 && cfg_read(ptr + 3, 1) == type) {
			unsigned bar = cfg_read(ptr + 4, 1), off = cfg_read(ptr + 8, 4);
			if (extra)
				*extra = (type == 2) ? cfg_read(ptr + 16, 4) : 0;
			uint8_t* base = map_bar(bar);
			return base ? (volatile uint8_t*)(base + off) : 0;
		}
		ptr = nxt;
	}
	return 0;
}

#define C8(o)   (*(volatile uint8_t*)(g_common + (o)))
#define C16(o)  (*(volatile uint16_t*)(g_common + (o)))
#define C32(o)  (*(volatile uint32_t*)(g_common + (o)))

/* one queue = three ring pages + QSZ buffers, carved from a single DMA block */
static bool queue_up(Queue* q, unsigned index, int vector, uint8_t* m, uint64_t phys, bool rx) {
	C16(0x16) = (uint16_t)index;
	dsb();
	unsigned max = C16(0x18);
	if (max < QSZ)
		return false;
	C16(0x18) = QSZ;
	q->desc = (VqDesc*)m;
	q->avail = (VqAvail*)(m + 4096);
	q->used = (VqUsed*)(m + 8192);
	q->bufs = m + 12288;
	uint64_t pd = phys, pa = phys + 4096, pu = phys + 8192, pb = phys + 12288;
	C32(0x20) = (uint32_t)pd; C32(0x24) = (uint32_t)(pd >> 32);
	C32(0x28) = (uint32_t)pa; C32(0x2C) = (uint32_t)(pa >> 32);
	C32(0x30) = (uint32_t)pu; C32(0x34) = (uint32_t)(pu >> 32);
	C16(0x1A) = (uint16_t)vector;
	dsb();
	C16(0x1C) = 1;                                       /* queue_enable */
	dsb();
	q->notify = (volatile uint16_t*)(g_notify + (uint32_t)C16(0x1E) * g_notify_mult);
	q->last_used = 0;
	for (unsigned i = 0; i < QSZ; i++) {
		q->desc[i].addr = pb + (uint64_t)i * BUF_SIZE;
		q->desc[i].len = BUF_SIZE;
		q->desc[i].flags = rx ? VIRTQ_DESC_F_WRITE : 0;
		q->desc[i].next = 0;
		if (rx)
			q->avail->ring[i] = (uint16_t)i;
	}
	dmb();
	if (rx)
		q->avail->idx = QSZ;
	return true;
}

static bool device_up(uint8_t mac[6]) {
	unsigned mult = 0;
	cfg_write(0x04, 2, 0x6);                             /* memory decode + bus master */
	g_common = map_cap(1, 0);
	g_notify = map_cap(2, &mult);
	g_devcfg = map_cap(4, 0);
	g_notify_mult = mult;
	if (!g_common || !g_notify || !g_devcfg)
		return false;

	/* virtio 1.0 handshake: MAC (bit 5) if offered, VERSION_1 (bit 32) required */
	C8(0x14) = 0;
	dsb();
	C8(0x14) = VIRTIO_STATUS_ACK;
	C8(0x14) = C8(0x14) | VIRTIO_STATUS_DRIVER;
	dsb();
	C32(0x00) = 0;
	dsb();
	uint32_t lo = C32(0x04) & (1u << 5);
	C32(0x00) = 1;
	dsb();
	if (!(C32(0x04) & 1))
		return false;
	C32(0x08) = 0; C32(0x0C) = lo;
	C32(0x08) = 1; C32(0x0C) = 1;
	dsb();
	C8(0x14) = C8(0x14) | VIRTIO_STATUS_FEATURES_OK;
	dsb();
	if (!(C8(0x14) & VIRTIO_STATUS_FEATURES_OK))
		return false;

	HwdevIrq irq = {0, 0};
	if (_KeFileIoControl(g_h, HWDEV_IOC_IRQ_SETUP, &irq) != HWDEV_OK)
		return false;
	C16(0x10) = 0xFFFF;                                  /* no config-change vector */

	/* both queues in one block: [rx rings+bufs][tx rings+bufs] */
	const unsigned per_queue = 12288 + QSZ * BUF_SIZE;
	HwdevDma mem = {2ULL * per_queue, 0, 0};
	if (_KeFileIoControl(g_h, HWDEV_IOC_DMA_ALLOC, &mem) != HWDEV_OK)
		return false;
	if (!queue_up(&g_rx, 0, 0, (uint8_t*)mem.uaddr, mem.phys, true))
		return false;
	if (!queue_up(&g_tx, 1, 0xFFFF, (uint8_t*)mem.uaddr + per_queue, mem.phys + per_queue, false))
		return false;

	for (int i = 0; i < 6; i++)
		mac[i] = *(volatile uint8_t*)(g_devcfg + i);
	C8(0x14) = C8(0x14) | VIRTIO_STATUS_DRIVER_OK;
	dsb();
	*g_rx.notify = 0;
	dsb();
	return true;
}

/* device -> kernel: received frames go to the RX ring */
static void rx_drain(void) {
	dmb();
	bool any = false;
	while (g_rx.last_used != g_rx.used->idx) {
		uint32_t id = g_rx.used->ring[g_rx.last_used % QSZ].id;
		uint32_t len = g_rx.used->ring[g_rx.last_used % QSZ].len;
		if (id < QSZ) {
			uint8_t* buf = g_rx.bufs + (uint64_t)id * BUF_SIZE;
			uint32_t flen = len > NET_HDR ? len - NET_HDR : 0;
			uint32_t head = g_ring->rx_head;
			if (flen >= 14 && flen <= NETSRV_MAX_FRAME) {
				if (head - g_ring->rx_tail >= NETSRV_SLOTS) {
					g_ring->rx_dropped++;
					g_rx_dropped++;
				} else {
					uint8_t* slot = g_rxslots + (head & (NETSRV_SLOTS - 1)) * NETSRV_SLOT_SIZE;
					memcpy(slot + NETSRV_SLOT_HDR, buf + NET_HDR, flen);
					*(volatile uint32_t*)slot = flen;
					dmb();
					g_ring->rx_head = head + 1;
					g_rx_frames++;
				}
			}
			g_rx.avail->ring[g_rx.avail->idx % QSZ] = (uint16_t)id;
			dmb();
			g_rx.avail->idx = (uint16_t)(g_rx.avail->idx + 1);
			any = true;
		}
		g_rx.last_used++;
	}
	if (any) {
		dsb();
		*g_rx.notify = 0;
	}
}

/* kernel -> device: frames the stack queued go onto the wire */
static void tx_drain(void) {
	dmb();
	uint16_t done = g_tx.used->idx;                      /* completed transmissions */
	bool any = false;
	for (;;) {
		uint32_t tail = g_ring->tx_tail;
		dmb();
		if (tail == g_ring->tx_head)
			break;
		if ((uint16_t)(g_tx_posted - done) >= QSZ)
			break;                                       /* device still owns every buffer */
		uint8_t* slot = g_txslots + (tail & (NETSRV_SLOTS - 1)) * NETSRV_SLOT_SIZE;
		uint32_t len = *(volatile uint32_t*)slot;
		if (len >= 14 && len <= NETSRV_MAX_FRAME) {
			unsigned d = g_tx_posted % QSZ;
			uint8_t* buf = g_tx.bufs + (uint64_t)d * BUF_SIZE;
			memset(buf, 0, NET_HDR);
			memcpy(buf + NET_HDR, slot + NETSRV_SLOT_HDR, len);
			g_tx.desc[d].len = NET_HDR + len;
			g_tx.avail->ring[g_tx.avail->idx % QSZ] = (uint16_t)d;
			dmb();
			g_tx.avail->idx = (uint16_t)(g_tx.avail->idx + 1);
			g_tx_posted++;
			g_tx_frames++;
			any = true;
		} else {
			g_tx_bad++;
		}
		g_ring->tx_tail = tail + 1;
	}
	if (any) {
		dsb();
		*g_tx.notify = 1;
	}
}

static void maybe_crash(void) {
	if (g_crash_after && g_tx_frames + g_rx_frames >= g_crash_after) {
		char line[120];
		sprintf(line, "[netsrv] test hook: crashing on purpose after %u frames\n", g_tx_frames + g_rx_frames);
		emit(line);
		*(volatile int*)0 = 1;
	}
}

static void give_back(const char* why) {
	char line[160];
	sprintf(line, "[netsrv] %s -- giving the device back to the kernel driver\n", why);
	emit(line);
	if (g_h > 0)
		_KeCloseFile(g_h);
	drvctl("load virtnet");
}

int main(int argc, char* argv[]) {
#ifdef __XENEVA_SVC_TEST__
	if (argc > 1 && argv && argv[1] && !strncmp(argv[1], "crash-after=", 12))
		for (const char* p = argv[1] + 12; *p >= '0' && *p <= '9'; p++)
			g_crash_after = g_crash_after * 10 + (unsigned)(*p - '0');
#else
	(void)argv;
#endif
	{
		char line[100];
		sprintf(line, "[netsrv] starting (args=%d%s)\n", argc, g_crash_after ? ", crash hook on" : "");
		emit(line);
	}

	g_ctl = _KeOpenFile((char*)"/dev/hwdev", FILE_OPEN_READ_ONLY);
	g_netsrv = _KeOpenFile((char*)"/dev/netsrv", FILE_OPEN_READ_ONLY);
	if (g_ctl < 0 || g_netsrv < 0) {
		emit("[netsrv] no /dev/hwdev or /dev/netsrv (not in the daemons group?)\n");
		return 1;
	}

	/* the in-kernel driver holds the device: release it (a no-op if already gone) */
	drvctl("unload virtnet");

	HwdevPciOpen req;
	memset(&req, 0, sizeof req);
	req.vendor = NET_VENDOR;
	req.device = NET_DEVICE;
	req.handle = -1;
	int rc = _KeFileIoControl(g_ctl, HWDEV_IOC_PCI_OPEN, &req);
	if (rc != HWDEV_OK) {
		char line[100];
		sprintf(line, "[netsrv] pci_open failed (%d)\n", rc);
		emit(line);
		give_back("cannot claim the NIC");
		return 1;
	}
	g_h = req.handle;

	uint8_t mac[6];
	if (!device_up(mac)) {
		give_back("device setup failed");
		return 1;
	}

	/* frame rings, owned by this process so the kernel can verify them */
	HwdevDma rmem = {NETSRV_RING_BYTES, 0, 0};
	if (_KeFileIoControl(g_h, HWDEV_IOC_DMA_ALLOC, &rmem) != HWDEV_OK) {
		give_back("cannot allocate the frame rings");
		return 1;
	}
	g_ring = (NetRingHdr*)rmem.uaddr;
	g_txslots = (uint8_t*)rmem.uaddr + NETSRV_TX_OFFSET;
	g_rxslots = (uint8_t*)rmem.uaddr + NETSRV_RX_OFFSET;
	g_ring->magic = NETSRV_RING_MAGIC;
	g_ring->slots = NETSRV_SLOTS;
	g_ring->slot_size = NETSRV_SLOT_SIZE;
	g_ring->link = 1;
	memcpy(g_ring->mac, mac, 6);
	dmb();
	NetsrvAttach at = {rmem.phys, NETSRV_RING_BYTES};
	rc = _KeFileIoControl(g_netsrv, NETSRV_IOC_ATTACH, &at);
	if (rc != NETSRV_OK) {
		char line[100];
		sprintf(line, "[netsrv] attach failed (%d)\n", rc);
		emit(line);
		give_back("kernel refused the rings");
		return 1;
	}
	{
		char line[120];
		sprintf(line, "[netsrv] mac %x:%x:%x:%x:%x:%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		emit(line);
	}
	emit("[netsrv] network is now served from user space\n");

	unsigned last_total = 0, spins = 0;
	for (;;) {
		HwdevIrqWait w = {1, 0};
		_KeFileIoControl(g_h, HWDEV_IOC_IRQ_WAIT, &w);
		HwdevIrqAck ack = {0, 0, 0};
		_KeFileIoControl(g_h, HWDEV_IOC_IRQ_ACK, &ack);
		g_irqs += ack.pending;
		rx_drain();
		tx_drain();
		maybe_crash();
		if ((++spins & 63) == 0) {
			unsigned total = g_tx_frames + g_rx_frames;
			if (total != last_total) {
				char line[200];
				sprintf(line, "[netsrv] tx=%u rx=%u irqs=%u rx_dropped=%u tx_bad=%u\n", g_tx_frames, g_rx_frames,
						g_irqs, g_rx_dropped, g_tx_bad);
				emit(line);
				last_total = total;
			}
		}
	}
	return 0;
}
