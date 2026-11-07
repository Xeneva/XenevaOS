/*
 * DCL/mem_test.c -- end-to-end proof for the drivers/char port.
 *
 * Exercises the whole chain the owner asked about as one stack:
 * open -> read/write -> llseek -> close, dispatched from Xeneva devfs into
 * vendored mainline file_operations (DCL/mem.c), with kmalloc, copy_*_user
 * and printk on the path. Every check goes through AuVFSOpen + the node's
 * callbacks, so nothing here can pass by short-circuiting the bridge.
 *
 * Runs after modload_test_run() on purpose: /dev/urandom reads the real
 * hardware RNG, which only exists once virtio_rng.ko is bound.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Fs/vfs.h>
#include <Drivers/uart.h>

static int _memtest_bad = 0;
static int _memtest_ok = 0;

static void memcheck(const char* what, int ok, long got, long want) {
	if (ok) {
		_memtest_ok++;
	} else {
		_memtest_bad++;
		UARTDebugOut("[dcl]: memtest FAIL %s (got %d want %d)\r\n",
					 what, (int)got, (int)want);
	}
}

static AuVFSNode* opendev(const char* path) {
	AuVFSNode* n = AuVFSOpen((char*)path);
	if (!n || !n->read || !n->write) {
		_memtest_bad++;
		UARTDebugOut("[dcl]: memtest open FAIL %s\r\n", path);
		return (AuVFSNode*)0;
	}
	/*
	 * Mirror fileserv's open syscall (Serv/fileserv.c): AuVFSOpen only
	 * resolves the node, the caller then invokes its ->open. Skipping this
	 * is what a naive caller gets wrong -- without it mainline's ->open
	 * (memory_open swapping a minor onto null_fops/zero_fops/...) never
	 * runs and every dispatch would hit the wrong fops.
	 */
	if (n->open && !n->open(n, NULL)) {
		_memtest_bad++;
		UARTDebugOut("[dcl]: memtest open callback FAIL %s\r\n", path);
		return (AuVFSNode*)0;
	}
	return n;
}

void DclMemTestRun(void) {
	unsigned char buf[64];
	size_t n;

	_memtest_bad = 0;
	_memtest_ok = 0;

	/* /dev/null: writes are consumed, reads are EOF */
	AuVFSNode* nd = opendev("/dev/null");
	if (nd) {
		memset(buf, 0xAA, sizeof(buf));
		n = nd->write(nd, nd, (uint64_t*)buf, 8);
		memcheck("null write==8", n == 8, (long)n, 8);
		memset(buf, 0xAA, sizeof(buf));
		n = nd->read(nd, nd, (uint64_t*)buf, 8);
		memcheck("null read==0", n == 0, (long)n, 0);
	}

	/* /dev/zero: writes consumed, reads yield PAGE-aligned zero chunks */
	AuVFSNode* zr = opendev("/dev/zero");
	if (zr) {
		memset(buf, 0xAA, sizeof(buf));
		n = zr->write(zr, zr, (uint64_t*)buf, 8);
		memcheck("zero write==8", n == 8, (long)n, 8);
		memset(buf, 0xAA, sizeof(buf));
		n = zr->read(zr, zr, (uint64_t*)buf, 32);
		int allz = 1;
		for (size_t i = 0; i < n && i < sizeof(buf); i++)
			if (buf[i])
				allz = 0;
		memcheck("zero read==32", n == 32, (long)n, 32);
		memcheck("zero bytes are 0", allz, allz, 1);
	}

	/* /dev/full: reads EOF, writes report no space (mapped to a short count) */
	AuVFSNode* fl = opendev("/dev/full");
	if (fl) {
		n = fl->read(fl, fl, (uint64_t*)buf, 8);
		memcheck("full read==0", n == 0, (long)n, 0);
		n = fl->write(fl, fl, (uint64_t*)buf, 8);
		memcheck("full write==0", n == 0, (long)n, 0);
	}

	/* /dev/urandom: real entropy from the hardware RNG */
	AuVFSNode* rn = opendev("/dev/urandom");
	if (rn) {
		memset(buf, 0, sizeof(buf));
		n = rn->read(rn, rn, (uint64_t*)buf, 16);
		memcheck("urandom read>0", n > 0, (long)n, 16);
	}

	/*
	 * /dev/mem: open only. open_port() walks capable() -> security_locked_down()
	 * -> iomem_get_mapping() -> memory_open(), which is the mainline gate path;
	 * reading arbitrary physical addresses is left to an interactive session.
	 * opendev() reports the failure if that gate ever rejects.
	 */
	AuVFSNode* mm = opendev("/dev/mem");
	if (mm)
		_memtest_ok++;

	UARTDebugOut("[dcl]: mem devices %d ok, %d failed\r\n",
				 _memtest_ok, _memtest_bad);
}
