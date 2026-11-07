/*
 * Process/hwtest/main.cpp -- user-space driver access test for /dev/hwdev.
 *
 * Runs as a MISC_DAEMONS process (init grants that group) and exercises the
 * capability path end to end against the real virtio-sound PCI function:
 *
 *   1. default deny: a PCI function not on the kernel allowlist is refused
 *   2. exclusivity: virtio-sound is refused (BUSY) while its in-kernel driver is loaded
 *   3. the driver is unloaded through /dev/drvctl (Phase 1), then the function opens
 *   4. config space reads return the real ids; forbidden writes are refused
 *   5. BAR sizes are reported; the virtio common-config window is mapped into
 *      this process and read from EL0 (num_queues must be 4 for virtio-snd)
 *   6. out-of-range maps are refused; a DMA buffer is allocated, mapped, used
 *   7. closing the handle releases the claim; the in-kernel driver is reloaded
 *
 * Every result goes to stdout and /dev/kmsg, so it shows in the serial log:
 *     grep '\[hwtest\]' run.log
 * Exit code is the number of failed checks.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <hwdev.h>
#include <drvctl.h>

static int g_ok;
static int g_bad;
static int g_kmsg = -1;

static void emit(const char* line) {
	printf("%s", line);
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

static void report(const char* what, long got, long want) {
	char line[168];
	if (got == want) {
		g_ok++;
		sprintf(line, "[hwtest] PASS %s (%ld)\n", what, got);
	} else {
		g_bad++;
		sprintf(line, "[hwtest] FAIL %s got=%ld want=%ld\n", what, got, want);
	}
	emit(line);
}

/* printf here is 32-bit for %x, so print 64-bit values as high_low */
static void note(const char* what, unsigned long long v) {
	char line[168];
	sprintf(line, "[hwtest] info %s = %x_%x\n", what, (unsigned)(v >> 32), (unsigned)(v & 0xFFFFFFFFu));
	emit(line);
}

static int drvctl(const char* cmd) {
	int fd = _KeOpenFile((char*)"/dev/drvctl", FILE_OPEN_WRITE);
	if (fd < 0)
		return -100;
	AuDriverCtlRequest req;
	memset(&req, 0, sizeof req);
	size_t n = strlen(cmd);
	if (n >= sizeof(req.command))
		n = sizeof(req.command) - 1;
	memcpy(req.command, (void*)cmd, n);
	int rc = _KeFileIoControl(fd, AU_DRVCTL_IOC_EXEC, &req);
	_KeCloseFile(fd);
	return rc;
}

static int pci_open(int ctl, unsigned vendor, unsigned device, int* handle) {
	HwdevPciOpen req;
	memset(&req, 0, sizeof req);
	req.vendor = (uint16_t)vendor;
	req.device = (uint16_t)device;
	req.handle = -1;
	int rc = _KeFileIoControl(ctl, HWDEV_IOC_PCI_OPEN, &req);
	if (handle)
		*handle = req.handle;
	return rc;
}

static int cfg_read(int h, unsigned off, unsigned size, unsigned* val) {
	HwdevCfg c = {off, size, 0};
	int rc = _KeFileIoControl(h, HWDEV_IOC_CFG_READ, &c);
	*val = c.value;
	return rc;
}

static int cfg_write(int h, unsigned off, unsigned size, unsigned val) {
	HwdevCfg c = {off, size, val};
	return _KeFileIoControl(h, HWDEV_IOC_CFG_WRITE, &c);
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	const unsigned VEN = 0x1AF4, SND = 0x1059;
	emit("[hwtest] --- user-space driver access test start ---\n");

	int ctl = _KeOpenFile((char*)"/dev/hwdev", FILE_OPEN_READ_ONLY);
	report("open /dev/hwdev", ctl >= 0, 1);
	if (ctl < 0) {
		emit("[hwtest] SUMMARY aborted: no /dev/hwdev (is this process in the daemons group?)\n");
		return 1;
	}
	report("drvctl unknown driver status", drvctl("unload no-such-driver"), AU_UNLOAD_NOT_FOUND);

	/* 1. default deny: virtio-gpu is real hardware here but not on the allowlist */
	report("non-allowlisted device refused", pci_open(ctl, VEN, 0x1050, NULL), HWDEV_E_PERM);
	/* an id that does not exist is also refused before any PCI probing */
	report("unknown device refused", pci_open(ctl, 0xBEEF, 0xF00D, NULL), HWDEV_E_PERM);

	/* 2. the in-kernel virtio-sound driver still owns the function */
	report("busy while kernel driver loaded", pci_open(ctl, VEN, SND, NULL), HWDEV_E_BUSY);

	/* 3. hand the device over: unload the kernel driver, then claim it */
	report("drvctl unload succeeded", drvctl("unload virtsnd"), AU_UNLOAD_OK);
	int h = -1;
	int rc = pci_open(ctl, VEN, SND, &h);
	if (rc != HWDEV_OK) {
		/* unload is vetoed while an audio client is bound to the card */
		note("pci_open after unload rc (negative = still owned)", (unsigned long long)(long long)rc);
		emit("[hwtest] SKIP device tests: virtio-sound could not be handed over\n");
		report("kernel driver reload", drvctl("load virtsnd"), AU_UNLOAD_OK);
		goto summary;
	}
	report("pci_open after unload", rc, HWDEV_OK);
	report("handle is a new fd", h > 0 && h != ctl, 1);
	report("second open of same function refused", pci_open(ctl, VEN, SND, NULL), HWDEV_E_BUSY);

	{
		/* 4. config space */
		unsigned v = 0;
		report("cfg read id", cfg_read(h, 0x00, 4, &v), HWDEV_OK);
		note("vendor/device dword", v);
		report("vendor id is virtio", (long)(v & 0xFFFF), (long)VEN);
		report("device id is virtio-snd", (long)(v >> 16), (long)SND);
		report("cfg read size 3 refused", cfg_read(h, 0x00, 3, &v), HWDEV_E_INVAL);
		report("cfg write to BAR0 refused", cfg_write(h, 0x10, 4, 0xDEADBEEF), HWDEV_E_INVAL);
		report("cfg write to vendor id refused", cfg_write(h, 0x00, 4, 0), HWDEV_E_INVAL);

		/* enable memory decode through the one writable header register */
		report("enable mem decode", cfg_write(h, 0x04, 2, 0x2), HWDEV_OK);
		report("cfg read command", cfg_read(h, 0x04, 2, &v), HWDEV_OK);
		report("memory decode is on", (long)((v >> 1) & 1), 1);

		/* 5. find the virtio common-config window by walking the capability list */
		unsigned cap = 0, bar = 0xFF, off = 0, len = 0;
		unsigned ptr = 0;
		cfg_read(h, 0x34, 1, &ptr);
		for (int guard = 0; ptr >= 0x40 && ptr < 0xFC && guard < 48; guard++) {
			unsigned id = 0, nxt = 0;
			cfg_read(h, ptr, 1, &id);
			cfg_read(h, ptr + 1, 1, &nxt);
			if (id == 0x09) {              /* vendor-specific: virtio_pci_cap */
				unsigned type = 0;
				cfg_read(h, ptr + 3, 1, &type);
				if (type == 1) {           /* VIRTIO_PCI_CAP_COMMON_CFG */
					cfg_read(h, ptr + 4, 1, &bar);
					cfg_read(h, ptr + 8, 4, &off);
					cfg_read(h, ptr + 12, 4, &len);
					cap = ptr;
					break;
				}
			}
			ptr = nxt;
		}
		report("found virtio common-config capability", cap != 0, 1);
		note("common cfg bar", bar);
		note("common cfg offset", off);
		note("common cfg length", len);

		HwdevBar bi;
		memset(&bi, 0, sizeof bi);
		bi.bar = bar;
		report("bar info", _KeFileIoControl(h, HWDEV_IOC_BAR_INFO, &bi), HWDEV_OK);
		note("bar base", bi.base);
		note("bar size", bi.size);
		report("bar is memory", (long)(bi.flags & HWDEV_BAR_MEM), HWDEV_BAR_MEM);
		report("bar has a size", bi.size > 0, 1);

		/* map the page(s) holding the common config and read it from EL0 */
		unsigned moff = off & ~0xFFFu;
		HwdevMap mp;
		memset(&mp, 0, sizeof mp);
		mp.bar = bar;
		mp.offset = moff;
		mp.length = (off - moff) + len;
		report("map bar window", _KeFileIoControl(h, HWDEV_IOC_MAP_BAR, &mp), HWDEV_OK);
		note("user address", mp.uaddr);
		if (mp.uaddr) {
			volatile uint8_t* cc = (volatile uint8_t*)(mp.uaddr + (off - moff));
			unsigned nq = *(volatile uint16_t*)(cc + 0x12);
			unsigned status = cc[0x14];
			*(volatile uint32_t*)(cc + 0x00) = 0;               /* device_feature_select = 0 */
			unsigned feat0 = *(volatile uint32_t*)(cc + 0x04);
			note("num_queues (read from EL0)", nq);
			note("device_status", status);
			note("device_feature[0]", feat0);
			report("virtio-snd has 4 queues", (long)nq, 4);
		}

		/* out-of-range maps are refused */
		HwdevMap bad = {bar, 0, bi.size + 0x10000, 0x1000, 0};
		report("map past end of bar refused", _KeFileIoControl(h, HWDEV_IOC_MAP_BAR, &bad), HWDEV_E_INVAL);
		HwdevMap badbar = {7, 0, 0, 0x1000, 0};
		report("map invalid bar refused", _KeFileIoControl(h, HWDEV_IOC_MAP_BAR, &badbar), HWDEV_E_INVAL);

		/* 6. DMA buffer: contiguous, zeroed, mapped into this process */
		HwdevDma dma = {8192, 0, 0};
		report("dma alloc", _KeFileIoControl(h, HWDEV_IOC_DMA_ALLOC, &dma), HWDEV_OK);
		note("dma uaddr", dma.uaddr);
		note("dma phys", dma.phys);
		report("dma phys page aligned", dma.phys && !(dma.phys & 0xFFF), 1);
		if (dma.uaddr) {
			volatile uint32_t* p = (volatile uint32_t*)dma.uaddr;
			int zeroed = (p[0] == 0 && p[2047] == 0);
			for (int i = 0; i < 2048; i++)
				p[i] = 0xA5A50000u + (unsigned)i;
			int okp = 1;
			for (int i = 0; i < 2048; i++)
				if (p[i] != 0xA5A50000u + (unsigned)i)
					okp = 0;
			report("dma buffer starts zeroed", zeroed, 1);
			report("dma buffer read/write", okp, 1);
		}
		HwdevDma huge = {64ULL << 20, 0, 0};
		report("oversized dma refused", _KeFileIoControl(h, HWDEV_IOC_DMA_ALLOC, &huge), HWDEV_E_INVAL);

		/* 7. closing the handle unmaps, frees and releases the claim */
		_KeCloseFile(h);
		int h2 = -1;
		report("reopen after close", pci_open(ctl, VEN, SND, &h2), HWDEV_OK);
		if (h2 >= 0)
			_KeCloseFile(h2);
	}

	/* give the device back to the in-kernel driver */
	report("kernel driver reload", drvctl("load virtsnd"), AU_UNLOAD_OK);

summary:
	{
		char line[168];
		sprintf(line, "[hwtest] SUMMARY %d ok, %d failed\n", g_ok, g_bad);
		emit(line);
	}
	_KeCloseFile(ctl);
	if (g_kmsg >= 0)
		_KeCloseFile(g_kmsg);
	return g_bad;
}
