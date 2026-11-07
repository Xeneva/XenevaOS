/*
 * Process/inputsrv/main.cpp -- user-space virtio-input driver (keyboard + tablet).
 *
 * Takes the two virtio-input PCI functions away from the kernel's built-in
 * drivers and drives them from EL0:
 *
 *   1. release the built-in drivers (/dev/drvctl "unload virtio-kbd" / "virtio-tablet"),
 *      then claim each PCI function through /dev/hwdev (the two share a PCI id, so
 *      they are told apart by instance and identified by the name the device reports)
 *   2. per device: map the BAR, walk the virtio capabilities, run the virtio 1.0
 *      handshake, post receive buffers on the event queue, take its MSI-X interrupt
 *   3. one thread per device waits for the interrupt, drains the event queue, decodes
 *      each event exactly as the in-kernel driver did (scancode mapping, button
 *      state, absolute positions) and injects it into /dev/inputsrv, which feeds the
 *      kernel's input layer
 *
 * Runs as a MISC_DAEMONS process under svcmgr. If setup fails after the built-in
 * drivers were released it loads them back, so input is never lost.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <sys/_ketime.h>
#include <hwdev.h>
#include <inputsrv.h>

#define INPUT_VENDOR   0x1AF4
#define INPUT_DEVICE   0x1052
#define MAX_DEV        2

#define VIRTIO_STATUS_ACK         1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTQ_DESC_F_WRITE        2

struct VqDesc  { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct VqAvail { uint16_t flags; uint16_t idx; uint16_t ring[1]; };
struct VqUsed  { uint16_t flags; uint16_t idx; struct { uint32_t id; uint32_t len; } ring[1]; };
struct InEvent { uint16_t type; uint16_t code; uint32_t value; };     /* virtio_input_event */

enum { KIND_NONE = 0, KIND_KEYBOARD = 1, KIND_TABLET = 2 };

struct Dev {
	int instance;
	int h;                          /* hwdev handle */
	int kind;
	uint8_t* bar[6];
	volatile uint8_t* common;
	volatile uint8_t* notify;
	volatile uint8_t* devcfg;
	uint32_t notify_mult;
	uint16_t qsize;
	VqDesc* desc;
	VqAvail* avail;
	VqUsed* used;
	uint16_t last_used;
	InEvent* events;
	volatile uint16_t* qnotify;
	/* tablet decode state */
	uint32_t maxx, maxy;
	int32_t x, y;
	int left, right, middle, scroll_up, scroll_down;
	/* statistics */
	unsigned irqs, events_seen, injected, rejected;
};

static Dev g_dev[MAX_DEV];
static int g_ctl = -1, g_inject = -1;
static int g_kmsg = -1;
static unsigned g_crash_after;           /* test hook, 0 = off */

static const uint8_t ext_key_map[256] = {
	[0x63] = 0x37, [0x66] = 0x47, [0x67] = 0x48, [0x68] = 0x49, [0x6c] = 0x50,
	[0x69] = 0x4B, [0x6a] = 0x4D, [0x6b] = 0x4F, [0x6d] = 0x51, [0x7d] = 0x5b,
};

static inline void dmb(void) { __asm__ volatile("dmb ish" ::: "memory"); }
static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }

static void emit(const char* line) {
	printf("%s", line);
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

static void logf2(const char* fmt, unsigned a, unsigned b) {
	char line[200];
	sprintf(line, fmt, a, b);
	emit(line);
}

static int drvctl(const char* cmd) {
	int fd = _KeOpenFile((char*)"/dev/drvctl", FILE_OPEN_WRITE);
	if (fd < 0)
		return -1;
	int n = (int)_KeWriteFile(fd, (void*)cmd, strlen(cmd));
	_KeCloseFile(fd);
	return n;
}

static unsigned cfg_read(Dev* d, unsigned off, unsigned size) {
	HwdevCfg c = {off, size, 0};
	_KeFileIoControl(d->h, HWDEV_IOC_CFG_READ, &c);
	return c.value;
}

static int cfg_write(Dev* d, unsigned off, unsigned size, unsigned val) {
	HwdevCfg c = {off, size, val};
	return _KeFileIoControl(d->h, HWDEV_IOC_CFG_WRITE, &c);
}

static uint8_t* map_bar(Dev* d, unsigned bar) {
	if (bar >= 6)
		return 0;
	if (d->bar[bar])
		return d->bar[bar];
	HwdevBar bi;
	memset(&bi, 0, sizeof bi);
	bi.bar = bar;
	if (_KeFileIoControl(d->h, HWDEV_IOC_BAR_INFO, &bi) != HWDEV_OK || !bi.size)
		return 0;
	HwdevMap mp;
	memset(&mp, 0, sizeof mp);
	mp.bar = bar;
	mp.length = bi.size;
	if (_KeFileIoControl(d->h, HWDEV_IOC_MAP_BAR, &mp) != HWDEV_OK)
		return 0;
	d->bar[bar] = (uint8_t*)mp.uaddr;
	return d->bar[bar];
}

static volatile uint8_t* map_cap(Dev* d, unsigned type, unsigned* extra) {
	unsigned ptr = cfg_read(d, 0x34, 1);
	for (int guard = 0; ptr >= 0x40 && ptr < 0xFC && guard < 48; guard++) {
		unsigned id = cfg_read(d, ptr, 1), nxt = cfg_read(d, ptr + 1, 1);
		if (id == 0x09 && cfg_read(d, ptr + 3, 1) == type) {
			unsigned bar = cfg_read(d, ptr + 4, 1), off = cfg_read(d, ptr + 8, 4);
			if (extra)
				*extra = (type == 2) ? cfg_read(d, ptr + 16, 4) : 0;
			uint8_t* base = map_bar(d, bar);
			return base ? (volatile uint8_t*)(base + off) : 0;
		}
		ptr = nxt;
	}
	return 0;
}

#define C8(d, o)   (*(volatile uint8_t*)((d)->common + (o)))
#define C16(d, o)  (*(volatile uint16_t*)((d)->common + (o)))
#define C32(d, o)  (*(volatile uint32_t*)((d)->common + (o)))

/* virtio-input device configuration: select(0) subsel(1) size(2) data(8..) */
static unsigned vi_select(Dev* d, unsigned sel, unsigned sub) {
	*(volatile uint8_t*)(d->devcfg + 0) = (uint8_t)sel;
	*(volatile uint8_t*)(d->devcfg + 1) = (uint8_t)sub;
	dsb();
	return *(volatile uint8_t*)(d->devcfg + 2);          /* size of the selected data */
}

static bool identify(Dev* d) {
	unsigned n = vi_select(d, 1, 0);                     /* VIRTIO_INPUT_CFG_ID_NAME */
	char name[64];
	memset(name, 0, sizeof name);
	if (n > 63)
		n = 63;
	for (unsigned i = 0; i < n; i++)
		name[i] = (char)*(volatile uint8_t*)(d->devcfg + 8 + i);
	char line[120];
	sprintf(line, "[inputsrv] instance %d is '%s'\n", d->instance, name);
	emit(line);
	if (strstr(name, "Keyboard") || strstr(name, "keyboard")) {
		d->kind = KIND_KEYBOARD;
	} else if (strstr(name, "Tablet") || strstr(name, "tablet")) {
		d->kind = KIND_TABLET;
		vi_select(d, 0x12, 0);                           /* VIRTIO_INPUT_CFG_ABS_INFO, ABS_X */
		d->maxx = *(volatile uint32_t*)(d->devcfg + 8 + 4);
		vi_select(d, 0x12, 1);                           /* ABS_Y */
		d->maxy = *(volatile uint32_t*)(d->devcfg + 8 + 4);
		logf2("[inputsrv] tablet range %u x %u\n", d->maxx, d->maxy);
	}
	vi_select(d, 0, 0);
	return d->kind != KIND_NONE;
}

static bool device_up(Dev* d) {
	unsigned mult = 0;
	cfg_write(d, 0x04, 2, 0x6);                          /* memory decode + bus master */
	d->common = map_cap(d, 1, 0);
	d->notify = map_cap(d, 2, &mult);
	d->devcfg = map_cap(d, 4, 0);
	d->notify_mult = mult;
	if (!d->common || !d->notify || !d->devcfg)
		return false;
	if (!identify(d))
		return false;

	/* virtio 1.0 handshake */
	C8(d, 0x14) = 0;
	dsb();
	C8(d, 0x14) = VIRTIO_STATUS_ACK;
	C8(d, 0x14) = C8(d, 0x14) | VIRTIO_STATUS_DRIVER;
	dsb();
	C32(d, 0x00) = 1;
	dsb();
	if (!(C32(d, 0x04) & 1))                              /* VIRTIO_F_VERSION_1 */
		return false;
	C32(d, 0x08) = 0; C32(d, 0x0C) = 0;
	C32(d, 0x08) = 1; C32(d, 0x0C) = 1;
	dsb();
	C8(d, 0x14) = C8(d, 0x14) | VIRTIO_STATUS_FEATURES_OK;
	dsb();
	if (!(C8(d, 0x14) & VIRTIO_STATUS_FEATURES_OK))
		return false;

	/* interrupt */
	HwdevIrq irq = {0, 0};
	if (_KeFileIoControl(d->h, HWDEV_IOC_IRQ_SETUP, &irq) != HWDEV_OK)
		return false;
	C16(d, 0x10) = 0xFFFF;                                /* no config-change vector */

	/* event queue 0: three pages for the rings + one page of receive buffers */
	HwdevDma mem = {4 * 4096ULL, 0, 0};
	if (_KeFileIoControl(d->h, HWDEV_IOC_DMA_ALLOC, &mem) != HWDEV_OK)
		return false;
	C16(d, 0x16) = 0;
	dsb();
	d->qsize = C16(d, 0x18);
	if (!d->qsize || d->qsize > 256)
		return false;
	uint8_t* m = (uint8_t*)mem.uaddr;
	d->desc = (VqDesc*)m;
	d->avail = (VqAvail*)(m + 4096);
	d->used = (VqUsed*)(m + 8192);
	d->events = (InEvent*)(m + 12288);
	uint64_t pd = mem.phys, pa = mem.phys + 4096, pu = mem.phys + 8192, pe = mem.phys + 12288;
	C32(d, 0x20) = (uint32_t)pd; C32(d, 0x24) = (uint32_t)(pd >> 32);
	C32(d, 0x28) = (uint32_t)pa; C32(d, 0x2C) = (uint32_t)(pa >> 32);
	C32(d, 0x30) = (uint32_t)pu; C32(d, 0x34) = (uint32_t)(pu >> 32);
	C16(d, 0x1A) = 0;                                     /* MSI-X vector 0 */
	dsb();
	if (C16(d, 0x1A) != 0)
		return false;
	C16(d, 0x1C) = 1;                                     /* queue_enable */
	dsb();
	d->qnotify = (volatile uint16_t*)(d->notify + (uint32_t)C16(d, 0x1E) * d->notify_mult);

	for (unsigned i = 0; i < d->qsize && i * sizeof(InEvent) < 4096; i++) {
		d->desc[i].addr = pe + i * sizeof(InEvent);
		d->desc[i].len = sizeof(InEvent);
		d->desc[i].flags = VIRTQ_DESC_F_WRITE;
		d->desc[i].next = 0;
		d->avail->ring[i] = (uint16_t)i;
	}
	dmb();
	d->avail->idx = d->qsize;
	d->last_used = 0;
	C8(d, 0x14) = C8(d, 0x14) | VIRTIO_STATUS_DRIVER_OK;
	dsb();
	*d->qnotify = 0;                                      /* tell the device buffers are ready */
	dsb();
	return true;
}

static void inject(Dev* d, InputsrvEvent* ev) {
	int rc = _KeFileIoControl(g_inject, INPUTSRV_IOC_INJECT, ev);
	if (rc == INPUTSRV_OK)
		d->injected++;
	else
		d->rejected++;
	if (g_crash_after && d->injected >= g_crash_after) {
		logf2("[inputsrv] test hook: crashing on purpose after %u events (instance %u)\n", d->injected, (unsigned)d->instance);
		*(volatile int*)0 = 1;
	}
}

static void handle_key(Dev* d, const InEvent* e) {
	if (e->type != 1)
		return;
	uint8_t scancode;
	if (e->code == 28 || e->code == 96)                   /* KEY_ENTER, KEY_KPENTER */
		scancode = 0x1c;
	else if (e->code < 0x49)
		scancode = (uint8_t)e->code;
	else if (e->code < 256 && ext_key_map[e->code])
		scancode = ext_key_map[e->code];
	else
		return;
	if (e->value == 0)
		scancode |= 0x80;                                 /* release */
	InputsrvEvent ev;
	memset(&ev, 0, sizeof ev);
	ev.type = INPUTSRV_KEY;
	ev.code = scancode;
	inject(d, &ev);
}

static void handle_tablet(Dev* d, const InEvent* e) {
	if (e->type == 3) {                                   /* EV_ABS */
		if (e->code == 0)
			d->x = (int32_t)e->value;
		else if (e->code == 1)
			d->y = (int32_t)e->value;
	} else if (e->type == 1) {                            /* EV_KEY */
		if (e->code == 0x110) d->left = (int)e->value;
		else if (e->code == 0x111) d->right = (int)e->value;
		else if (e->code == 0x112) d->middle = (int)e->value;
		else if (e->code == 0x150) d->scroll_down = 1;
		else if (e->code == 0x151) d->scroll_up = 1;
	} else if (e->type == 2) {                            /* EV_REL: QEMU wheel-axis */
		if (e->code == 8 && e->value) {
			if (e->value < 0x80000000u)
				d->scroll_up = 1;
			else
				d->scroll_down = 1;
		}
	} else if (e->type == 0) {                            /* EV_SYN: one complete pointer update */
		InputsrvEvent ev;
		memset(&ev, 0, sizeof ev);
		ev.type = INPUTSRV_ABS;
		ev.x = d->x;
		ev.y = d->y;
		ev.maxx = d->maxx;
		ev.maxy = d->maxy;
		ev.buttons = (uint8_t)((d->left ? INPUTSRV_BTN_LEFT : 0) | (d->right ? INPUTSRV_BTN_RIGHT : 0) |
							   (d->middle ? INPUTSRV_BTN_MIDDLE : 0) |
							   (d->scroll_down ? INPUTSRV_BTN_SCROLL_DOWN : 0) |
							   (d->scroll_up ? INPUTSRV_BTN_SCROLL_UP : 0));
		inject(d, &ev);
		d->scroll_down = d->scroll_up = 0;
	}
}

/* drain the event queue, then give the buffers back to the device */
static unsigned drain(Dev* d) {
	unsigned n = 0;
	dmb();
	while (d->last_used != d->used->idx) {
		uint32_t id = d->used->ring[d->last_used % d->qsize].id;
		if (id < d->qsize) {
			InEvent e = d->events[id];
			d->events_seen++;
			if (d->kind == KIND_KEYBOARD)
				handle_key(d, &e);
			else
				handle_tablet(d, &e);
			d->avail->ring[d->avail->idx % d->qsize] = (uint16_t)id;
			dmb();
			d->avail->idx = (uint16_t)(d->avail->idx + 1);
		}
		d->last_used++;
		n++;
	}
	if (n) {
		dsb();
		*d->qnotify = 0;
	}
	return n;
}

static void device_loop(Dev* d) {
	for (;;) {
		HwdevIrqWait w = {50, 0};
		_KeFileIoControl(d->h, HWDEV_IOC_IRQ_WAIT, &w);
		HwdevIrqAck ack = {0, 0, 0};
		_KeFileIoControl(d->h, HWDEV_IOC_IRQ_ACK, &ack);
		d->irqs += ack.pending;
		drain(d);
	}
}

static void thread_0() { device_loop(&g_dev[0]); }
static void thread_1() { device_loop(&g_dev[1]); }

static void give_back(const char* why) {
	char line[160];
	sprintf(line, "[inputsrv] %s -- giving the devices back to the kernel drivers\n", why);
	emit(line);
	for (int i = 0; i < MAX_DEV; i++)
		if (g_dev[i].h > 0)
			_KeCloseFile(g_dev[i].h);
	drvctl("load virtio-kbd");
	drvctl("load virtio-tablet");
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
		sprintf(line, "[inputsrv] starting (args=%d%s)\n", argc, g_crash_after ? ", crash hook on" : "");
		emit(line);
	}

	g_ctl = _KeOpenFile((char*)"/dev/hwdev", FILE_OPEN_READ_ONLY);
	g_inject = _KeOpenFile((char*)"/dev/inputsrv", FILE_OPEN_READ_ONLY);
	if (g_ctl < 0 || g_inject < 0) {
		emit("[inputsrv] no /dev/hwdev or /dev/inputsrv (not in the daemons group?)\n");
		return 1;
	}

	/* the built-in drivers hold the devices: release them (a no-op if already gone) */
	drvctl("unload virtio-kbd");
	drvctl("unload virtio-tablet");

	int found = 0;
	for (int inst = 0; inst < MAX_DEV; inst++) {
		Dev* d = &g_dev[found];
		memset(d, 0, sizeof *d);
		d->instance = inst;
		HwdevPciOpen req;
		memset(&req, 0, sizeof req);
		req.vendor = INPUT_VENDOR;
		req.device = INPUT_DEVICE;
		req.instance = (uint32_t)inst;
		req.handle = -1;
		int rc = _KeFileIoControl(g_ctl, HWDEV_IOC_PCI_OPEN, &req);
		if (rc != HWDEV_OK) {
			logf2("[inputsrv] instance %u: pci_open failed (%u)\n", (unsigned)inst, (unsigned)rc);
			continue;
		}
		d->h = req.handle;
		if (!device_up(d)) {
			logf2("[inputsrv] instance %u: device setup failed\n", (unsigned)inst, 0);
			_KeCloseFile(d->h);
			d->h = 0;
			continue;
		}
		{
			char line[100];
			sprintf(line, "[inputsrv] instance %d up as %s\n", inst, d->kind == KIND_KEYBOARD ? "keyboard" : "tablet");
			emit(line);
		}
		found++;
	}
	if (!found) {
		give_back("no input device could be taken over");
		return 1;
	}
	emit("[inputsrv] input is now served from user space\n");

	_KeCreateThread(thread_0, (char*)"inpkbd");
	if (found > 1)
		_KeCreateThread(thread_1, (char*)"inptab");

	/* the main thread reports progress, so a serial log shows the path working */
	unsigned last_total = 0;
	for (;;) {
		_KeProcessSleep(200);
		unsigned total = 0, irqs = 0, rej = 0;
		for (int i = 0; i < found; i++) {
			total += g_dev[i].injected;
			irqs += g_dev[i].irqs;
			rej += g_dev[i].rejected;
		}
		if (total != last_total) {
			char line[200];
			sprintf(line, "[inputsrv] injected=%u irqs=%u rejected=%u (kbd=%u tablet=%u)\n", total, irqs, rej,
					g_dev[0].kind == KIND_KEYBOARD ? g_dev[0].injected : g_dev[1].injected,
					g_dev[0].kind == KIND_TABLET ? g_dev[0].injected : g_dev[1].injected);
			emit(line);
			last_total = total;
		}
	}
	return 0;
}
