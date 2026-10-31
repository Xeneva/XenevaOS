#include <linux/module_loader.h>
#include <Drivers/uart.h>
#include <Fs/vfs.h>
#include <Fs/Dev/devfs.h>
#include <Mm/kmalloc.h>
#include <Cred/group.h>
#include <list.h>
#include <string.h>

#include <linux/test_module_bin.h>
static const size_t test_module_size = sizeof(test_module_o);

extern void virtio_rng_detect(void);
extern int virtio_console_detect(void);
extern int virtio_gpu_detect(void);
extern int dcl_vport_selftest(void);
extern int hwrng_selftest(void);
extern int hwrng_read_bytes(void* buf, unsigned int max);

static char dcl_status[512];
static size_t dcl_len = 0;

static void dcl_puts(const char* s) {
	while (*s && dcl_len < sizeof(dcl_status) - 1)
		dcl_status[dcl_len++] = *s++;
	dcl_status[dcl_len] = 0;
}

static void dcl_putdec(long v) {
	char tmp[24];
	int t = 0;
	int neg = (v < 0);
	unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
	if (u == 0)
		tmp[t++] = '0';
	while (u > 0) {
		tmp[t++] = '0' + (u % 10);
		u /= 10;
	}
	if (neg)
		tmp[t++] = '-';
	while (t > 0) {
		char c[2];
		c[0] = tmp[--t];
		c[1] = 0;
		dcl_puts(c);
	}
}

static size_t DclStatusRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	(void)node;
	if (!buffer || !length)
		return 0;
	size_t pos = file->pos;
	if (pos >= dcl_len)
		return 0;
	size_t n = dcl_len - pos;
	if (n > length)
		n = length;
	memcpy(buffer, dcl_status + pos, n);
	file->pos = pos + n;
	return n;
}

static AuVFSNode* DclStatusOpen(AuVFSNode* node, char* path) {
	(void)path;
	node->pos = 0;
	return node;
}

static AuVFSNode*
dcl_devnode_create(AuVFSNode* fs, const char* name, open_callback open, read_callback read) {
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!node)
		return NULL;
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, name);
	node->flags |= FS_FLAG_DEVICE;
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_WORLD);
	node->open = open;
	node->read = read;
	AuDevFSAddFile(fs, "/", node);
	return node;
}

static void dcl_status_register(void) {
	AuVFSNode* fs = AuVFSFind("/dev");
	if (!fs) {
		UARTDebugOut("[dcl]: /dev not found, status node skipped\r\n");
		return;
	}
	AuVFSNode* node = dcl_devnode_create(fs, "dcl", DclStatusOpen, DclStatusRead);
	if (!node)
		return;
	AuVFSContainer* cont = (AuVFSContainer*)fs->device;
	int found = 0;
	if (cont && cont->childs) {
		for (int j = 0; j < (int)cont->childs->pointer; j++) {
			if ((AuVFSNode*)list_get_at(cont->childs, j) == node) {
				found = 1;
				break;
			}
		}
	}
	if (found)
		UARTDebugOut("[dcl]: /dev/dcl registered (root list verified)\r\n");
	else
		UARTDebugOut("[dcl]: WARNING - dcl node missing from devfs root list\r\n");
}

static size_t HwrngRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	(void)node;
	(void)file;
	if (!buffer || !length)
		return 0;
	if (length > 64)
		length = 64;
	int got = hwrng_read_bytes((void*)buffer, length);
	return got > 0 ? (size_t)got : 0;
}

static AuVFSNode* HwrngOpen(AuVFSNode* node, char* path) {
	(void)path;
	node->pos = 0;
	return node;
}

static void hwrng_node_register(void) {
	AuVFSNode* fs = AuVFSFind("/dev");
	if (!fs) {
		UARTDebugOut("[dcl]: /dev not found, hwrng node skipped\r\n");
		return;
	}
	if (dcl_devnode_create(fs, "hwrng", HwrngOpen, HwrngRead))
		UARTDebugOut("[dcl]: /dev/hwrng registered\r\n");
}

static void print_dec(unsigned long v) {
	char tmp[20];
	int t = 0;
	if (v == 0)
		tmp[t++] = '0';
	while (v > 0) {
		tmp[t++] = '0' + (v % 10);
		v /= 10;
	}
	char out[21];
	int o = 0;
	while (t > 0)
		out[o++] = tmp[--t];
	out[o] = 0;
	UARTDebugOut(out);
}

static int modload_embedded_test(void) {
	UARTDebugOut("[modtest]: loading embedded test_module.o (");
	print_dec(test_module_size);
	UARTDebugOut("B)\r\n");

	struct mod_handle mod;
	int rc = mod_load(test_module_o, test_module_size, &mod);
	if (rc != 0) {
		UARTDebugOut("[modtest]: mod_load FAILED\r\n");
		dcl_puts("  test_module  load=");
		dcl_putdec(rc);
		dcl_puts(" FAILED\r\n");
		return 0;
	}

	UARTDebugOut("[modtest]: module loaded, calling init...\r\n");
	int init_rc = mod_call_init(&mod);
	UARTDebugOut("[modtest] init returned ");
	if (init_rc < 0) {
		UARTDebugOut("-");
		print_dec((unsigned long)(-init_rc));
	} else {
		UARTDebugOut(" ");
		print_dec((unsigned long)init_rc);
	}
	UARTDebugOut("\r\n");
	dcl_puts("  test_module  load=0 init=");
	dcl_putdec(init_rc);
	dcl_puts("\r\n");

	UARTDebugOut("[modtest]: calling exit...\r\n");
	mod_call_exit(&mod);

	UARTDebugOut("[modtest]: unloading...\r\n");
	mod_unload(&mod);
	return 1;
}

static void hwrng_roundtrip_test(void) {
	AuVFSNode* hn = AuVFSOpen("/dev/hwrng");
	if (!hn || !hn->read) {
		UARTDebugOut("[dcl]: /dev/hwrng round-trip FAILED (no node)\r\n");
		return;
	}
	uint64_t hb[2][4];
	for (int t = 0; t < 2; t++) {
		memset(hb[t], 0, 32);
		size_t got = hn->read(hn, hn, hb[t], 32);
		UARTDebugOut("[dcl]: hwrng node read#");
		print_dec((unsigned long)(t + 1));
		UARTDebugOut(" -> ");
		print_dec(got);
		UARTDebugOut(" bytes:");
		unsigned char* p = (unsigned char*)hb[t];
		for (size_t i = 0; i < got && i < 32; i++) {
			/* UARTDebugOut has no %02x width support */
			if (p[i] < 0x10)
				UARTDebugOut(" 0%x", p[i]);
			else
				UARTDebugOut(" %x", p[i]);
		}
		UARTDebugOut("\r\n");
	}
	int differ = memcmp(hb[0], hb[1], 32) != 0;
	if (differ)
		UARTDebugOut("[dcl]: /dev/hwrng round-trip OK (samples differ)\r\n");
	else
		UARTDebugOut("[dcl]: /dev/hwrng round-trip FAILED (identical samples)\r\n");
}

/*
 * modload_virtio_rng_test -- the virtio-rng half of the boot gate.
 *
 * This used to hand mod_load() a 10552-byte virtio_rng.ko that had been
 * built in a separate arm64 kernel tree and embedded as an xxd -i array, then
 * call init_module() on it. That path is gone: the driver now compiles from
 * source into the image (Vendored/drivers/char/hw_random/virtio-rng.c, see
 * Vendored/README.md) and registers itself at boot through
 * module_virtio_driver() -> DclRunInitcalls(), so there is nothing to load
 * and no init_module to call.
 *
 * Two things still belong here, in this order:
 *
 *   - virtio_rng_detect() scans PCI for 1AF4:1044. It cannot move into the
 *     initcall, because at initcall time there is no device to find yet;
 *     detect() -> virtio_device_register() is what does the matching, and by
 *     the time it runs the driver is already on the registered list
 *     (init.c calls DclRunInitcalls() before modload_test_run()).
 *   - the self-test and the round-trip read /dev/hwrng, which exists only
 *     after probe -> scan -> hwrng_register, i.e. only once detect() matched.
 *
 * The ELF loader itself is untouched: modload_test() still loads
 * test_module.o through DCL/module_loader.c, which walks the AArch64
 * relocation table and resolves undefined symbols against k_exports[]. What
 * changed is where the driver under test comes from -- a source file this
 * build compiled, instead of a binary another tree produced.
 */
static int modload_virtio_rng_test(void) {
	UARTDebugOut("[modtest]: virtio_rng built-in (Vendored/, not a .ko)\r\n");

	UARTDebugOut("[modtest]: calling virtio_rng_detect...\r\n");
	virtio_rng_detect();

	hwrng_selftest();
	hwrng_roundtrip_test();

	dcl_puts("  virtio_rng   load=0 init=0 name=built-in\r\n");
	UARTDebugOut("[modtest]: virtio_rng resident\r\n");
	return 1;
}

/*
 * The virtio-console half of the same pair, and it is a pair for the same
 * reason: register in DclRunInitcalls(), find the device here.
 *
 * virtio_console_detect() scans 1AF4:1043 the way virtio_rng_detect() scans
 * 1AF4:1044, and dcl_vport_selftest() then opens the /dev/vport* node that
 * probe created and pumps one write/read round through the fops. It is a
 * self-test rather than a bare detect because the failure that matters is
 * not "device absent" -- QEMU may simply not have been given a
 * `-device virtio-serial-pci` -- but "device present, driver registered,
 * probe ran, and the port still does not move a byte". The test says which
 * of the three it is: "host not attached" is the benign no-chardev case,
 * and any other non-zero return is a real failure.
 *
 * Timing is part of that: probe registers the char devices at once, but the
 * /dev/vport* node comes from the host's PORT_ADD control message, which
 * only lands when the control queue is pumped. Nothing has pumped it by the
 * time this runs, so dcl_vport_selftest() waits (and pumps) rather than
 * reporting a driver that was never given the chance to finish.
 *
 * All of its output goes through UARTDebugOut because it runs before
 * userspace has anything to read a /dev node with.
 */
static int modload_virtio_console_test(void) {
	int found;
	int rc;

	UARTDebugOut("[modtest]: virtio_console built-in (Vendored/, not a .ko)\r\n");

	UARTDebugOut("[modtest]: calling virtio_console_detect...\r\n");
	found = virtio_console_detect();

	/*
	 * The self-test waits for the /dev/vport* node, so it is only worth
	 * running when a device exists to make one. With --no-virtio-serial
	 * there is no controller, hence no PORT_ADD, hence no node ever --
	 * and a wait that cannot end in a result is dead boot time rather
	 * than a measurement.
	 */
	if (!found) {
		UARTDebugOut("[modtest]: vport selftest skipped (no device)\r\n");
		dcl_puts("  virtio_console: no device\r\n");
		return 0;
	}

	rc = dcl_vport_selftest();
	UARTDebugOut("[modtest]: vport selftest rc=%d\r\n", rc);

	dcl_puts(rc ? "  virtio_console selftest FAILED\r\n"
				: "  virtio_console selftest ok\r\n");
	return rc < 0 ? 0 : 1;
}

#ifdef __XENEVA_BYPASS_VIRTIO_INPUT__
/*
 * modload_virtio_input_test -- scan for every 1AF4:1052 function and let
 * the vendored virtio-input driver probe each one.  The bridge handler was
 * registered by DclRunInitcalls() before this runs, so input_register_device()
 * attaches it as part of probe and the event queue starts filling.
 */
static int modload_virtio_input_test(void) {
	int found;

	UARTDebugOut("[modtest]: virtio_input built-in (Vendored/drivers/virtio)\r\n");
	found = virtio_input_detect();
	if (found) {
		UARTDebugOut("[modtest]: virtio_input %d device(s) registered\r\n",
					 found);
		dcl_puts("  virtio_input registered\r\n");
		return 1;
	}

	dcl_puts("  virtio_input: no device\r\n");
	return 0;
}
#endif

#ifdef __XENEVA_BYPASS_VIRTGPU__
/*
 * modload_virtio_gpu_test -- the third virtio scan, and the only one whose
 * driver exists on the kernel side for a reason other than self-test:
 * --bypass-virtgpu makes the vendored DRM driver the owner of 1AF4:1050,
 * and this scan is what hands it the device.
 *
 * The driver is already registered when modload_test_run() runs --
 * DclRunInitcalls() called its initcall. detect() creates the virtio
 * device, and the device's registration is what matches that driver and
 * runs probe, which allocates the DRM device, registers the card and brings
 * up the virtio queues, all before this returns. The scan belongs here
 * rather than in the initcall for the same reason rng's and console's do:
 * initcalls register drivers, and the modload pass brings devices up once
 * the drivers are on the list.
 *
 * Compile-time gate, not a runtime "should I": a default image carries the
 * internal virtgpu.dll, which owns 1AF4:1050, and a scan there would race
 * it for the device. Only an image packed by build_and_run_qemu.sh
 * --bypass-virtgpu is this function's to run.
 */
static int modload_virtio_gpu_test(void) {
	UARTDebugOut("[modtest]: virtio_gpu built-in (Vendored/drivers/gpu/drm/virtio)\r\n");

	if (virtio_gpu_detect()) {
		dcl_puts("  virtio_gpu   registered\r\n");
		return 1;
	}

	dcl_puts("  virtio_gpu: no device\r\n");
	return 0;
}
#endif

void modload_test_run(void) {
	dcl_status_register();
	hwrng_node_register();
	dcl_puts("DCL module loader status\r\n");

	/* The toy module is a loader check. virtio-rng still has to come up
	 * so userspace HTTPS can read /dev/hwrng. */
	modload_embedded_test();
	modload_virtio_rng_test();
	/* After rng: both scan the same bus, and rng's scan returns on its
	 * own device rather than walking past it. */
	modload_virtio_console_test();
#ifdef __XENEVA_BYPASS_VIRTIO_INPUT__
	modload_virtio_input_test();
#endif
#ifdef __XENEVA_BYPASS_VIRTGPU__
	modload_virtio_gpu_test();
#endif
	UARTDebugOut("[modtest]: done\r\n");
}
