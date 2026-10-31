/*
 * Process/sndsrv/main.cpp -- user-space virtio-sound driver.
 *
 * Takes over the virtio-sound PCI function from the in-kernel driver and
 * drives it from EL0 through /dev/hwdev (see BaseHdr/hwdev.h):
 *
 *   1. unload the in-kernel driver (/dev/drvctl), claim the PCI function
 *   2. map its BAR, walk the virtio capabilities, run the virtio 1.0 handshake,
 *      set up the four virtqueues in DMA memory
 *   3. query the PCM streams, pick the output stream, set params, prepare, start
 *   4. allocate a ring in DMA memory and hand it to /dev/sndsrv; the kernel
 *      registers a sound card whose write() fills that ring
 *   5. drain the ring one period at a time into the device's TX queue
 *
 * Runs as a MISC_DAEMONS process. If setup fails after the in-kernel driver
 * was unloaded, it loads it back so the system keeps its sound.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/_ketime.h>
#include <hwdev.h>
#include <sndsrv.h>

#define PERIOD_BYTES 4096
#define BUFFER_BYTES (PERIOD_BYTES * 8)
#define RING_DATA	 65536
#define SND_VENDOR	 0x1AF4
#define SND_DEVICE	 0x1059

/* ---- virtio constants (virtio 1.0 spec, pci transport + sound device) ---- */
#define VIRTIO_STATUS_ACK		  1
#define VIRTIO_STATUS_DRIVER	  2
#define VIRTIO_STATUS_DRIVER_OK	  4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED	  0x80
#define VIRTQ_DESC_F_NEXT		  1
#define VIRTQ_DESC_F_WRITE		  2

#define SND_R_PCM_INFO		 0x0100
#define SND_R_PCM_SET_PARAMS 0x0101
#define SND_R_PCM_PREPARE	 0x0102
#define SND_R_PCM_RELEASE	 0x0103
#define SND_R_PCM_START		 0x0104
#define SND_R_PCM_STOP		 0x0105
#define SND_S_OK			 0x8000
#define SND_FMT_S16			 5
#define SND_RATE_48000		 7

struct VqDesc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};
struct VqAvail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[1];
};
struct VqUsed {
	uint16_t flags;
	uint16_t idx;
	struct {
		uint32_t id;
		uint32_t len;
	} ring[1];
};

struct PcmInfo { /* virtio_snd_pcm_info, 32 bytes */
	uint32_t hda_fn_nid;
	uint32_t features;
	uint64_t formats;
	uint64_t rates;
	uint8_t directions, channels_min, channels_max, padding[5];
};

static int g_kmsg = -1;

static void emit(const char* line) {
	printf("%s", line);
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

static void logf1(const char* fmt, unsigned a) {
	char line[160];
	sprintf(line, fmt, a);
	emit(line);
}

static void logf3(const char* fmt, unsigned a, unsigned b, unsigned c) {
	char line[160];
	sprintf(line, fmt, a, b, c);
	emit(line);
}

static inline void dmb(void) {
	__asm__ volatile("dmb ish" ::: "memory");
}
static inline void dsb(void) {
	__asm__ volatile("dsb sy" ::: "memory");
}

/* ---- device state ---- */
static int g_ctl = -1, g_h = -1;
static uint8_t* g_bar[6];
static volatile uint8_t* g_common;
static volatile uint8_t* g_notify;
static volatile uint8_t* g_devcfg;
static uint32_t g_notify_mult;

#define C8(o)  (*(volatile uint8_t*)(g_common + (o)))
#define C16(o) (*(volatile uint16_t*)(g_common + (o)))
#define C32(o) (*(volatile uint32_t*)(g_common + (o)))

struct Vq {
	uint16_t size;
	VqDesc* desc;
	VqAvail* avail;
	VqUsed* used;
	uint16_t last_used;
	volatile uint16_t* notify;
};
static Vq g_q[4];

static uint8_t* g_cmd;
static uint64_t g_cmd_phys; /* requests, xfer header */
static uint8_t* g_resp;
static uint64_t g_resp_phys; /* responses, tx status */
static uint8_t* g_pcm;
static uint64_t g_pcm_phys; /* one period */
static uint32_t g_stream;
static bool g_irq; /* completions arrive as interrupts, not by polling */
static unsigned g_irq_events, g_irq_waits, g_irq_spurious;
static unsigned g_last_status; /* last TX status word the device returned */
static SndRingHdr* g_ring;
static uint8_t* g_ring_data;

static int pci_open(unsigned vendor, unsigned device, int* handle) {
	HwdevPciOpen req;
	memset(&req, 0, sizeof req);
	req.vendor = (uint16_t)vendor;
	req.device = (uint16_t)device;
	req.handle = -1;
	int rc = _KeFileIoControl(g_ctl, HWDEV_IOC_PCI_OPEN, &req);
	*handle = req.handle;
	return rc;
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
	mp.offset = 0;
	mp.length = bi.size;
	if (_KeFileIoControl(g_h, HWDEV_IOC_MAP_BAR, &mp) != HWDEV_OK)
		return 0;
	g_bar[bar] = (uint8_t*)mp.uaddr;
	return g_bar[bar];
}

/* find a virtio capability of the given cfg_type; returns 1 and fills bar/off/len */
static int find_cap(unsigned type, unsigned* bar, unsigned* off, unsigned* len, unsigned* extra) {
	unsigned ptr = cfg_read(0x34, 1);
	for (int guard = 0; ptr >= 0x40 && ptr < 0xFC && guard < 48; guard++) {
		unsigned id = cfg_read(ptr, 1), nxt = cfg_read(ptr + 1, 1);
		if (id == 0x09 && cfg_read(ptr + 3, 1) == type) {
			*bar = cfg_read(ptr + 4, 1);
			*off = cfg_read(ptr + 8, 4);
			*len = cfg_read(ptr + 12, 4);
			if (extra)
				*extra = (type == 2) ? cfg_read(ptr + 16, 4) : 0;
			return 1;
		}
		ptr = nxt;
	}
	return 0;
}

static volatile uint8_t* map_cap(unsigned type, unsigned* extra) {
	unsigned bar, off, len;
	if (!find_cap(type, &bar, &off, &len, extra))
		return 0;
	uint8_t* base = map_bar(bar);
	return base ? (volatile uint8_t*)(base + off) : 0;
}

static int vq_setup(unsigned q, uint8_t* mem, uint64_t phys) {
	C16(0x16) = (uint16_t)q;
	dsb();
	uint16_t size = C16(0x18);
	if (!size || size > 256)
		return 1;
	Vq* v = &g_q[q];
	v->size = size;
	v->desc = (VqDesc*)mem;
	v->avail = (VqAvail*)(mem + 4096);
	v->used = (VqUsed*)(mem + 8192);
	v->last_used = 0;
	uint64_t pd = phys, pa = phys + 4096, pu = phys + 8192;
	C32(0x20) = (uint32_t)pd;
	C32(0x24) = (uint32_t)(pd >> 32);
	C32(0x28) = (uint32_t)pa;
	C32(0x2C) = (uint32_t)(pa >> 32);
	C32(0x30) = (uint32_t)pu;
	C32(0x34) = (uint32_t)(pu >> 32);
	/* control (0) and tx (2) complete via interrupt when available; the unused
	 * event (1) and rx (3) queues raise none */
	unsigned vec = (g_irq && (q == 0 || q == 2)) ? 0 : 0xFFFF;
	C16(0x1A) = (uint16_t)vec;
	dsb();
	if (vec != 0xFFFF && C16(0x1A) != vec) /* device could not allocate the vector */
		g_irq = false;
	C16(0x1C) = 1; /* queue_enable */
	dsb();
	v->notify = (volatile uint16_t*)(g_notify + (uint32_t)C16(0x1E) * g_notify_mult);
	return 0;
}

/* submit descriptor chain starting at 0, kick, and poll for completion */
static int vq_submit_wait(unsigned q, unsigned limit_ms) {
	Vq* v = &g_q[q];
	v->avail->ring[v->avail->idx % v->size] = 0;
	dmb();
	v->avail->idx = (uint16_t)(v->avail->idx + 1);
	dsb();
	*v->notify = (uint16_t)q;
	dsb();
	if (g_irq && (q == 0 || q == 2)) {
		uint64_t t0 = _KeGetCurrentMS();
		while (v->used->idx == v->last_used) {
			dmb();
			HwdevIrqWait w = {10, 0};
			_KeFileIoControl(g_h, HWDEV_IOC_IRQ_WAIT, &w);
			HwdevIrqAck ack = {0, 0, 0};
			_KeFileIoControl(g_h, HWDEV_IOC_IRQ_ACK, &ack);
			g_irq_waits++;
			g_irq_events += ack.pending;
			dmb();
			if (v->used->idx == v->last_used) {
				if (ack.pending)
					g_irq_spurious++; /* interrupt arrived but this queue had nothing yet */
				if (_KeGetCurrentMS() - t0 > limit_ms)
					return 1;
			}
		}
		v->last_used++;
		return 0;
	}
	unsigned spins = 0, ms = 0;
	while (v->used->idx == v->last_used) {
		dmb();
		if (++spins > 20000) {
			_KeProcessSleep(1);
			if (++ms > limit_ms)
				return 1;
		}
	}
	v->last_used++;
	return 0;
}

static int snd_ctrl(unsigned req_len, unsigned resp_len) {
	Vq* v = &g_q[0];
	memset(g_resp, 0, resp_len);
	dmb();
	v->desc[0].addr = g_cmd_phys;
	v->desc[0].len = req_len;
	v->desc[0].flags = VIRTQ_DESC_F_NEXT;
	v->desc[0].next = 1;
	v->desc[1].addr = g_resp_phys;
	v->desc[1].len = resp_len;
	v->desc[1].flags = VIRTQ_DESC_F_WRITE;
	v->desc[1].next = 0;
	if (vq_submit_wait(0, 2000))
		return 1;
	return *(volatile uint32_t*)g_resp == SND_S_OK ? 0 : 1;
}

static int pcm_simple(uint32_t code) {
	uint32_t* p = (uint32_t*)g_cmd;
	p[0] = code;
	p[1] = g_stream;
	return snd_ctrl(8, 4);
}

static int pcm_set_params(void) {
	uint32_t* p = (uint32_t*)g_cmd;
	memset(g_cmd, 0, 32);
	p[0] = SND_R_PCM_SET_PARAMS;
	p[1] = g_stream;
	p[2] = BUFFER_BYTES;
	p[3] = PERIOD_BYTES;
	p[4] = 0;	   /* features */
	g_cmd[20] = 2; /* channels */
	g_cmd[21] = SND_FMT_S16;
	g_cmd[22] = SND_RATE_48000;
	return snd_ctrl(24, 4);
}

static int tx_period(uint8_t* data, unsigned len) {
	Vq* v = &g_q[2];
	if (len > PERIOD_BYTES)
		len = PERIOD_BYTES;
	memcpy(g_pcm, data, len);
	if (len < PERIOD_BYTES)
		memset(g_pcm + len, 0, PERIOD_BYTES - len);
	*(volatile uint32_t*)g_cmd = g_stream; /* xfer header */
	memset(g_resp, 0, 8);				   /* status + latency */
	dmb();
	v->desc[0].addr = g_cmd_phys;
	v->desc[0].len = 4;
	v->desc[0].flags = VIRTQ_DESC_F_NEXT;
	v->desc[0].next = 1;
	v->desc[1].addr = g_pcm_phys;
	v->desc[1].len = PERIOD_BYTES;
	v->desc[1].flags = VIRTQ_DESC_F_NEXT;
	v->desc[1].next = 2;
	v->desc[2].addr = g_resp_phys;
	v->desc[2].len = 8;
	v->desc[2].flags = VIRTQ_DESC_F_WRITE;
	v->desc[2].next = 0;
	if (vq_submit_wait(2, 1000))
		return 1;
	g_last_status = *(volatile uint32_t*)g_resp; /* status word of the xfer */
	return g_last_status == SND_S_OK ? 0 : 2;
}

static void give_back(const char* why) {
	emit("[sndsrv] ");
	emit(why);
	emit(" -- reloading the in-kernel driver\n");
	if (g_h >= 0) {
		_KeCloseFile(g_h);
		g_h = -1;
	}
	drvctl("load virtsnd");
}

int main(int argc, char* argv[]) {
	/* test hook for the supervisor: "crash-after=N" makes the server fault on
	 * purpose after N periods, to prove that a crash is survivable */
#ifdef __XENEVA_SVC_TEST__
	unsigned crash_after = 0;
	if (argc > 1 && argv && argv[1] && !strncmp(argv[1], "crash-after=", 12))
		for (const char* p = argv[1] + 12; *p >= '0' && *p <= '9'; p++)
			crash_after = crash_after * 10 + (unsigned)(*p - '0');
#else
	(void)argv;
#endif
	logf1("[sndsrv] starting (args=%u)\n", (unsigned)argc);
#ifdef __XENEVA_SVC_TEST__
	if (crash_after)
		logf1("[sndsrv] test hook: will crash after %u periods\n", crash_after);
#endif

	g_ctl = _KeOpenFile((char*)"/dev/hwdev", FILE_OPEN_READ_ONLY);
	if (g_ctl < 0) {
		emit("[sndsrv] no /dev/hwdev (not in the daemons group?)\n");
		return 1;
	}
	drvctl("unload virtsnd");
	if (pci_open(SND_VENDOR, SND_DEVICE, &g_h) != HWDEV_OK) {
		give_back("could not claim virtio-sound");
		return 1;
	}
	emit("[sndsrv] claimed the virtio-sound PCI function\n");

	/* memory decode + bus master */
	cfg_write(0x04, 2, 0x6);

	g_common = map_cap(1, 0);
	unsigned mult = 0;
	g_notify = map_cap(2, &mult);
	g_notify_mult = mult;
	g_devcfg = map_cap(4, 0);
	if (!g_common || !g_notify || !g_devcfg) {
		give_back("missing virtio capability");
		return 1;
	}

	/* ---- virtio 1.0 handshake ---- */
	C8(0x14) = 0; /* reset */
	dsb();
	C8(0x14) = VIRTIO_STATUS_ACK;
	C8(0x14) = C8(0x14) | VIRTIO_STATUS_DRIVER;
	dsb();
	C32(0x00) = 1; /* device_feature_select = 1 */
	dsb();
	unsigned feat_hi = C32(0x04);
	if (!(feat_hi & 1)) { /* VIRTIO_F_VERSION_1 = bit 32 */
		give_back("device lacks VIRTIO_F_VERSION_1");
		return 1;
	}
	C32(0x08) = 0;
	C32(0x0C) = 0; /* no low features */
	C32(0x08) = 1;
	C32(0x0C) = 1; /* VERSION_1 only */
	dsb();
	C8(0x14) = C8(0x14) | VIRTIO_STATUS_FEATURES_OK;
	dsb();
	if (!(C8(0x14) & VIRTIO_STATUS_FEATURES_OK)) {
		give_back("device rejected features");
		return 1;
	}

	/* ---- interrupts: ask the kernel to route the device's MSI-X to us ---- */
	{
		HwdevIrq irq = {0, 0};
		g_irq = (_KeFileIoControl(g_h, HWDEV_IOC_IRQ_SETUP, &irq) == HWDEV_OK);
		logf1(g_irq ? "[sndsrv] interrupt delivery on (spi %u)\n"
					: "[sndsrv] no interrupts, polling (%u)\n",
			  irq.spi);
		C16(0x10) = 0xFFFF; /* no config-change vector */
	}

	/* ---- DMA: queues (4 x 3 pages) + command/response/period buffers ---- */
	HwdevDma mem = {(12 + 4) * 4096ULL, 0, 0};
	if (_KeFileIoControl(g_h, HWDEV_IOC_DMA_ALLOC, &mem) != HWDEV_OK) {
		give_back("dma alloc failed");
		return 1;
	}
	for (unsigned q = 0; q < 4; q++) {
		if (vq_setup(q, (uint8_t*)mem.uaddr + q * 3 * 4096, mem.phys + q * 3 * 4096)) {
			give_back("virtqueue setup failed");
			return 1;
		}
	}
	g_cmd = (uint8_t*)mem.uaddr + 12 * 4096;
	g_cmd_phys = mem.phys + 12 * 4096;
	g_resp = (uint8_t*)mem.uaddr + 13 * 4096;
	g_resp_phys = mem.phys + 13 * 4096;
	g_pcm = (uint8_t*)mem.uaddr + 14 * 4096;
	g_pcm_phys = mem.phys + 14 * 4096;
	C8(0x14) = C8(0x14) | VIRTIO_STATUS_DRIVER_OK;
	dsb();
	logf3("[sndsrv] virtio up: queues=%u status=%x q2size=%u\n", C16(0x12), C8(0x14), g_q[2].size);
	logf1(g_irq ? "[sndsrv] completions by interrupt (queue vectors confirmed %u)\n"
				: "[sndsrv] completions by polling (%u)\n",
		  g_irq ? 1u : 0u);

	/* ---- virtio-snd: find the output stream, configure, start ---- */
	unsigned n_streams = *(volatile uint32_t*)(g_devcfg + 4);
	if (!n_streams || n_streams > 8) {
		give_back("unexpected stream count");
		return 1;
	}
	uint32_t* q = (uint32_t*)g_cmd;
	memset(g_cmd, 0, 16);
	q[0] = SND_R_PCM_INFO;
	q[1] = 0;
	q[2] = n_streams;
	q[3] = sizeof(PcmInfo);
	if (snd_ctrl(16, 4 + sizeof(PcmInfo) * n_streams)) {
		give_back("pcm info failed");
		return 1;
	}
	PcmInfo* info = (PcmInfo*)(g_resp + 4);
	/* virtio-snd spec: the field is a direction ENUM (0 = output, 1 = input),
	 * not a bitmask. Picking the stream with bit 0 set selects the INPUT stream,
	 * and the device answers every playback buffer with BAD_MSG. */
	g_stream = 0xFFFFFFFFu;
	for (unsigned i = 0; i < n_streams; i++) {
		logf3("[sndsrv] stream %u direction=%u channels=%u\n",
			  i,
			  info[i].directions,
			  info[i].channels_max);
		if (info[i].directions == 0 && g_stream == 0xFFFFFFFFu)
			g_stream = i;
	}
	if (g_stream == 0xFFFFFFFFu) {
		give_back("device has no output stream");
		return 1;
	}
	logf3("[sndsrv] output stream %u formats=%x rates=%x\n",
		  g_stream,
		  (unsigned)info[g_stream].formats,
		  (unsigned)info[g_stream].rates);
	if (pcm_set_params() || pcm_simple(SND_R_PCM_PREPARE) || pcm_simple(SND_R_PCM_START)) {
		give_back("could not start the output stream");
		return 1;
	}
	logf3("[sndsrv] output stream %u started (streams=%u period=%u)\n",
		  g_stream,
		  n_streams,
		  PERIOD_BYTES);

	/* ---- ring shared with the kernel sound card ---- */
	HwdevDma ring = {SNDSRV_DATA_OFFSET + RING_DATA, 0, 0};
	if (_KeFileIoControl(g_h, HWDEV_IOC_DMA_ALLOC, &ring) != HWDEV_OK) {
		give_back("ring alloc failed");
		return 1;
	}
	g_ring = (SndRingHdr*)ring.uaddr;
	g_ring_data = (uint8_t*)ring.uaddr + SNDSRV_DATA_OFFSET;
	g_ring->magic = SNDSRV_RING_MAGIC;
	g_ring->size = RING_DATA;
	g_ring->period = PERIOD_BYTES;
	dmb();
	int snd = _KeOpenFile((char*)"/dev/sndsrv", FILE_OPEN_READ_ONLY);
	SndsrvAttach at = {ring.phys, SNDSRV_DATA_OFFSET + RING_DATA};
	int arc = snd >= 0 ? _KeFileIoControl(snd, SNDSRV_IOC_ATTACH, &at) : -99;
	if (arc != SNDSRV_OK) {
		logf1("[sndsrv] attach failed (%d)\n", (unsigned)arc);
		give_back("kernel refused the ring");
		return 1;
	}
	g_ring->running = 1;
	emit("[sndsrv] attached: kernel sound card now feeds this server\n");

	/* ---- main loop: drain the ring one period at a time ---- */
	unsigned periods = 0, bytes = 0, idle = 0, errors = 0, sum = 0, quiet = 0;
	bool reported = true;
	uint8_t buf[PERIOD_BYTES];
	uint32_t was_running = 1;
	for (;;) {
		uint32_t run = g_ring->running;
		if (run != was_running) {
			if (run) {
				pcm_simple(SND_R_PCM_PREPARE);
				pcm_simple(SND_R_PCM_START);
			} else {
				pcm_simple(SND_R_PCM_STOP);
			}
			was_running = run;
		}
		uint32_t head = g_ring->head;
		dmb();
		uint32_t tail = g_ring->tail;
		uint32_t avail = head - tail;
		unsigned take = 0;
		if (avail >= PERIOD_BYTES)
			take = PERIOD_BYTES;
		else if (avail > 0 && ++idle > 10)
			take = avail; /* flush a partial period after a short idle */
		if (take) {
			idle = 0;
			uint32_t off = tail & (RING_DATA - 1);
			unsigned first = take < (RING_DATA - off) ? take : (RING_DATA - off);
			memcpy(buf, g_ring_data + off, first);
			if (first < take)
				memcpy(buf + first, g_ring_data, take - first);
			for (unsigned i = 0; i < take; i++)
				sum += buf[i];                    /* additive checksum of the PCM actually consumed */
			if (tx_period(buf, take)) {
				errors++;
				/* Do not advance tail after a timeout or device error: the
				 * descriptor may still be owned by virtio-sound. Stop using
				 * the shared buffers and let the supervisor hand the device
				 * back instead of reusing an in-flight DMA chain. */
				give_back("virtio-sound transfer failed");
				return 1;
			}
			reported = false;
			quiet = 0;
			dmb();
			g_ring->tail = tail + take;
			periods++;
			bytes += take;
			if ((periods & 63) == 0)
				logf3("[sndsrv] tx periods=%u bytes=%u errors=%u\n", periods, bytes, errors);
#ifdef __XENEVA_SVC_TEST__
			if (crash_after && periods >= crash_after) {
				logf1("[sndsrv] test hook: crashing on purpose after %u periods\n", periods);
				*(volatile int*)0 = 1; /* EL0 data abort: the kernel kills only this process */
			}
#endif
		} else {
			if (!avail)
				idle = 0;
			_KeProcessSleep(2);
			/* a burst has ended after ~1 s of silence: report totals once */
			if (!avail && !reported && ++quiet > 500) {
				char line[200];
				sprintf(line,
						"[sndsrv] burst done: periods=%u bytes=%u sum=%x dropped=%u errors=%u "
						"last_status=%x\n",
						periods,
						bytes,
						sum,
						(unsigned)g_ring->dropped,
						errors,
						g_last_status);
				emit(line);
				sprintf(line,
						"[sndsrv] irq: events=%u waits=%u spurious=%u mode=%s\n",
						g_irq_events,
						g_irq_waits,
						g_irq_spurious,
						g_irq ? "interrupt" : "poll");
				emit(line);
				reported = true;
			}
		}
	}
	return 0;
}
