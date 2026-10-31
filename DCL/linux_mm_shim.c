/**
 * DCL/linux_mm_shim.c -- the runtime behind the drivers/char ABI surface.
 *
 * Implements what vendored mainline sources (DCL/mem.c first) call but Xeneva
 * does not provide natively: the user-access family, the physical-memory
 * window for /dev/mem, lockdown/capability gates, the tty_init() placeholder,
 * and the /dev/random + /dev/urandom + /dev/kmsg fops.
 *
 * Two design points, both deliberate:
 *
 *  - uaccess is a direct copy. Xeneva runs a syscall with the calling
 *    process's page tables live and the devfs bridge already hands raw user
 *    pointers to ->read/->write, so there is no boundary to cross. The
 *    mainline return contract (bytes NOT copied; 0 == success) is preserved
 *    so mainline `if (copy_to_user(...)) return -EFAULT;` branches behave
 *    exactly as upstream. A VMA probe can later sit behind access_ok()
 *    without touching a single caller.
 *
 *  - /dev/random + /dev/urandom read the real hardware RNG
 *    (hwrng_read_bytes -> virtio_rng.ko) instead of vendoring mainline's
 *    ChaCha20 pool. Entropy stays entropy; DCL just does not re-implement
 *    the pool to get it.
 *
 * Timing note: nothing here spins or polls on a deadline, so the adapter
 * stays off the jitter-sensitive paths -- audio periods and vblank remain
 * inside the native cores.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/splice.h>
#include <linux/tty.h>
#include <linux/security.h>
#include <linux/random.h>
#include <linux/io.h>
#include <linux/shmem_fs.h>
#include <linux/pagemap.h>	/* struct page and the page_* decls below */
#include <linux/dma-mapping.h>	/* dma_map_sg()/dma_unmap_sg() below */
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>	/* kmalloc/kfree -- alloc_page()'s block */
#include <Drivers/uart.h>

/* hwrng bridge, provided by DCL/linux_kmod_shim.c */
extern int hwrng_read_bytes(void* buf, unsigned int max);

/* ── user access ───────────────────────────────────────────────────────── */

unsigned long copy_to_user(void __user* to, const void* from, unsigned long n) {
	if (!to || !from)
		return n;
	memcpy(to, from, n);
	return 0;
}

unsigned long copy_from_user(void* to, const void __user* from, unsigned long n) {
	if (!to || !from)
		return n;
	memcpy(to, from, n);
	return 0;
}

/*
 * strncpy_from_user(dst, src, count) -- mainline's NUL-terminated copy out
 * of a user address, added with the virtio-gpu port (the debug-name ioctl
 * bounds a name by its destination buffer). The copy is a plain one for
 * the reason copy_from_user() above says; the loop stops one short of the
 * bound so the terminator always fits, and the count is what comes back --
 * "length copied, terminator not counted", which is what the one caller's
 * success test reads.
 */
long strncpy_from_user(char* dst, const char __user* src, long count) {
	long i;

	if (count <= 0)
		return -EINVAL;

	for (i = 0; i < count - 1 && ((const char*)src)[i]; i++)
		dst[i] = ((const char*)src)[i];
	dst[i] = 0;
	return i;
}

unsigned long clear_user(void __user* to, unsigned long n) {
	if (!to)
		return n;
	memset(to, 0, n);
	return 0;
}

long copy_from_kernel_nofault(void* dst, const void* src, unsigned long size) {
	if (!dst || !src)
		return -EFAULT;
	memcpy(dst, src, size);
	return 0;
}

/* ── physical memory window (/dev/mem) ─────────────────────────────────── */

/*
 * Xeneva's linear map covers all of RAM, so any RAM pfn reads as its P2V.
 * Bounds are intentionally permissive: /dev/mem is a debug facility here,
 * and restricting it belongs to the same node permission check that guards
 * every other devfs node. Non-RAM addresses are rejected by P2V's caller
 * only when a real probe exists; today they read through the same window
 * a native kernel read would.
 */
void* xlate_dev_mem_ptr(phys_addr_t phys) {
	return (void*)(uintptr_t)P2V((uint64_t)phys);
}

void unxlate_dev_mem_ptr(phys_addr_t phys, void* addr) {
	(void)phys;
	(void)addr;
}

int valid_phys_addr_range(phys_addr_t addr, size_t count) {
	(void)addr;
	(void)count;
	return 1;
}

int valid_mmap_phys_addr_range(unsigned long pfn, size_t size) {
	(void)pfn;
	(void)size;
	return 1;
}

int range_is_allowed(unsigned long pgoff, unsigned long size) {
	(void)pgoff;
	(void)size;
	return 1;
}

int phys_mem_access_prot_allowed(struct file* file, unsigned long pfn,
								 unsigned long size, pgprot_t* vma_prot) {
	(void)file;
	(void)pfn;
	(void)size;
	(void)vma_prot;
	return 1;
}

/* ARM64 has no x86 I/O port space -> chr_dev_init skips /dev/port */
int arch_has_dev_port(void) {
	return 0;
}

/* ── gates ─────────────────────────────────────────────────────────────── */

int security_locked_down(int reason) {
	(void)reason;
	return 0;	/* no LSM in Xeneva */
}

/* ── mmap plumbing: declared so mainline's mmap arms link, not yet routed
 *    (devfs nodes expose open/read/write/close only). ─────────────────── */

unsigned long mm_get_unmapped_area(struct file* file, unsigned long addr,
								   unsigned long len, unsigned long pgoff,
								   unsigned long flags) {
	(void)file;
	(void)addr;
	(void)len;
	(void)pgoff;
	(void)flags;
	return (unsigned long)(-ENOSYS);
}

int shmem_zero_setup_desc(struct vm_area_desc* desc) {
	(void)desc;
	return -ENOSYS;
}

unsigned long shmem_get_unmapped_area(struct file* file, unsigned long addr,
									  unsigned long len, unsigned long pgoff,
									  unsigned long flags) {
	(void)file;
	(void)addr;
	(void)len;
	(void)pgoff;
	(void)flags;
	return (unsigned long)(-ENOSYS);
}

/* ── tty_init(): mem.c's last statement ────────────────────────────────── */

int tty_init(void) {
	/*
	 * The serial core and the line discipline table are compiled in now
	 * (8250_port, serial_core, tty_ldisc, tty_ldsem -- stage 3), but the
	 * body of this function is tty_io.c's: it creates /dev/console,
	 * /dev/tty and /dev/ptmx and brings up their cdevs, which is the
	 * file layer that lands at stage 5.  Until then there is no tty to
	 * open, so there is nothing to register either -- n_tty.c (stage 6)
	 * would be the first line discipline to put in the table.
	 */
	UARTDebugOut("[dcl]: tty_init placeholder (tty file layer lands with tty_io.c)\r\n");
	return 0;
}

/* ── /dev/random + /dev/urandom, backed by the hardware RNG ────────────── */

static ssize_t read_random(struct file* file, char* buf, size_t count,
						   long long* ppos) {
	(void)file;
	(void)ppos;
	if (!buf || !count)
		return 0;
	int got = hwrng_read_bytes(buf, (unsigned int)count);
	if (got < 0)
		return -ENODEV;	/* no entropy source bound (hwrng absent) */
	return (ssize_t)got;
}

static ssize_t write_random(struct file* file, const char* buf, size_t count,
							long long* ppos) {
	/* Upstream discards writes into the entropy pool; we have no pool to
	 * stir, but the call must still report a full write so /dev/urandom
	 * users don't see a short count. */
	(void)file;
	(void)buf;
	(void)ppos;
	return (ssize_t)count;
}

const struct file_operations random_fops = {
	.llseek = noop_llseek,
	.read = read_random,
	.write = write_random,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

const struct file_operations urandom_fops = {
	.llseek = noop_llseek,
	.read = read_random,
	.write = write_random,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

void get_random_bytes(void* buf, unsigned long nbytes) {
	if (!buf)
		return;
	int got = hwrng_read_bytes(buf, (unsigned int)nbytes);
	if (got <= 0)
		memset(buf, 0, (size_t)nbytes);
}

/* ── /dev/kmsg: mainline keeps devkmsg in printk.c; DCL logs through the
 *    UART, so reads hit EOF and writes are acknowledged and dropped. ──── */

static ssize_t read_kmsg(struct file* file, char* buf, size_t count,
						 long long* ppos) {
	(void)file;
	(void)buf;
	(void)count;
	(void)ppos;
	return 0;	/* EOF: klog is consumed through Xeneva's own console */
}

static ssize_t write_kmsg(struct file* file, const char* buf, size_t count,
						  long long* ppos) {
	(void)file;
	(void)ppos;
	if (buf && count) {
		size_t n = count < 256 ? count : 256;
		char tmp[257];
		memcpy(tmp, buf, n);
		tmp[n] = 0;
		UARTDebugOut("%s", tmp);
	}
	return (ssize_t)count;
}

const struct file_operations kmsg_fops = {
	.llseek = noop_llseek,
	.read = read_kmsg,
	.write = write_kmsg,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

/* ── iov_iter: single-buffer iterators ─────────────────────────────────── */

/*
 * mem.c keeps write_iter_null/read_iter_zero/... alive in its fops tables,
 * so these must link even though devfs dispatches the plain ->read/->write
 * members (zero_fops and null_fops both carry them). Correct implementations
 * rather than stubs: the moment the bridge routes ->read_iter, they are the
 * ones that will run.
 */
size_t iov_iter_count(const struct iov_iter* i) {
	return i ? i->count : 0;
}

void iov_iter_advance(struct iov_iter* i, size_t bytes) {
	if (!i || bytes > i->count)
		bytes = i ? i->count : 0;
	i->off += bytes;
	i->count -= bytes;
}

size_t iov_iter_zero(size_t bytes, struct iov_iter* i) {
	if (!i || !i->buf)
		return 0;
	if (bytes > i->count)
		bytes = i->count;
	memset((char*)i->buf + i->off, 0, bytes);
	i->off += bytes;
	i->count -= bytes;
	return bytes;
}

/* ── splice: declared so mem.c's fops link, not routed by devfs yet ────── */

ssize_t splice_from_pipe(struct pipe_inode_info* pipe, struct file* out,
						 loff_t* ppos, size_t len, unsigned int flags,
						 splice_pipe_actor actor) {
	(void)pipe;
	(void)out;
	(void)ppos;
	(void)len;
	(void)flags;
	(void)actor;
	return -EOPNOTSUPP;	/* devfs has no pipe/splice path yet */
}

ssize_t copy_splice_read(struct file* in, loff_t* ppos,
						 struct pipe_inode_info* pipe, size_t len,
						 unsigned int flags) {
	(void)in;
	(void)ppos;
	(void)pipe;
	(void)len;
	(void)flags;
	return -EOPNOTSUPP;
}

/* ── struct page: one block that is both descriptor and storage ─────────── */

/*
 * DCL has no buddy array of struct page and no direct-map window, which is
 * what <linux/pagemap.h> says when it models a page as a single kmalloc'd
 * block whose ->data is the address page_address() hands out. Everything
 * below follows from that one choice: allocation is one malloc, a reference
 * is an int in the block's first four bytes, and free is the matching free.
 */

/*
 * alloc_page() -- a struct page, initialised, refcount 1.
 *
 * Zeroed rather than handed over raw: DCL's kmalloc returns whatever the
 * TLSF free list last held, and this block's ->data becomes the destination
 * of a memcpy out of a pipe buffer (virtio_console.c:888) and then the
 * payload of a virtio buffer. Stale bytes there would be sent to the host.
 *
 * The gfp is accepted and dropped for the reason slab.h drops its own -- a
 * plain allocation with no watermark to honour -- and NULL is returned on
 * failure, which is the arm the caller tests (`if (!page) goto`) rather
 * than a degraded path that would have to be invented.
 */
void* alloc_page(gfp_t gfp) {
	struct page* page;

	(void)gfp;
	page = (struct page*)kmalloc(sizeof(struct page));
	if (!page)
		return NULL;

	page->refcount = 1;
	memset(page->data, 0, sizeof(page->data));
	return (void*)page;
}

/*
 * get_page / put_page -- the pair free_buf() walks (virtio_console.c:350-357),
 * releasing each page exactly once on the way out. put_page frees at zero
 * because nothing else will: there is no shrinker and no cache to return the
 * block to, and a block that reaches 0 and stays allocated is a leak with no
 * other owner to blame.
 */
void get_page(struct page* page) {
	if (page)
		page->refcount++;
}

void put_page(struct page* page) {
	if (!page)
		return;
	if (page->refcount > 0 && --page->refcount == 0)
		kfree(page);
}

/*
 * lock_page / unlock_page -- no-ops, and <linux/pagemap.h> is where the
 * reason already lives: the one pair in the driver sits inside pipe_to_sg(),
 * a path no devfs splice call reaches, and making them real would mean a
 * per-page waitqueue for code that cannot run. They are functions here
 * rather than macros so that a later implementation changes only this file.
 */
void lock_page(struct page* page) {
	(void)page;
}

void unlock_page(struct page* page) {
	(void)page;
}

/* ── dma streaming mapping: identity, because there is nothing else ────── */

/*
 * dma_map_sg() / dma_unmap_sg() -- declared in <linux/dma-mapping.h>, which
 * carries the reasoning for the answer in full.
 *
 * The short version: no IOMMU and no bounce buffer, so an sg entry is already
 * contiguous and already addressable, and "map" here is the identity.
 * Returning `nents` unchanged is what makes dma_map_sgtable()'s
 * `sgt->nents = nents` a no-op, and what lets drm_prime_get_contiguous_size()
 * (drm_prime.c:893) walk the list and terminate on the length it was handed.
 *
 * dma_unmap_sg() is empty for the mirror reason: with nothing mapped there is
 * nothing to invalidate, and the CPU never stopped seeing these buffers. The
 * parameters are all mainline's, including the direction, which describes a
 * mapping this does not perform.
 */
int dma_map_sg(struct device* dev, struct scatterlist* sg, int nents,
	       enum dma_data_direction dir)
{
	(void)dev;
	(void)sg;
	(void)dir;
	return nents;
}

void dma_unmap_sg(struct device* dev, struct scatterlist* sg, int nents,
		  enum dma_data_direction dir)
{
	(void)dev;
	(void)sg;
	(void)nents;
	(void)dir;
}

/*
 * dma_max_mapping_size() -- mainline's "largest length the device can map in
 * one segment", declared in <linux/dma-mapping.h>.  drm_prime.c:862
 * (drm_prime_pages_to_sg) uses the answer to cap the segment size it passes
 * to sg_alloc_table_from_pages_segment(), and turns 0 into UINT_MAX itself
 * (`if (max_segment == 0) max_segment = UINT_MAX`).  Zero is also mainline's
 * answer when there are no DMA ops to ask (its !CONFIG_HAS_DMA arm of this
 * function), and with no IOMMU and no bounce buffer there is no segment
 * limit to report: any contiguous run is directly addressable.
 */
size_t dma_max_mapping_size(struct device* dev)
{
	(void)dev;
	return 0;
}

/*
 * dma_coerce_mask_and_coherent() -- mainline sets dev->dma_mask from
 * dev->coherent_dma_mask and then asks the DMA layer to accept the mask
 * (<linux/dma-mapping.h>).  Two things it needs are absent here: struct
 * device (fs.h) is pinned to mainline's 800-byte layout with no named mask
 * fields, and there is no DMA layer that would ever read one.  The only
 * observable part of the call is its verdict -- vkms_create()
 * (vkms_drv.c:187) takes a non-zero return as "DMA support could not be
 * initialized" and unwinds -- so the answer is the success mainline would
 * have produced for a device with no constraints.
 */
int dma_coerce_mask_and_coherent(struct device* dev, u64 mask)
{
	(void)dev;
	(void)mask;
	return 0;
}

/* ── scatterlists over pages: the one constructor drm_prime needs ─────── */

/*
 * sg_alloc_table_from_pages_segment() -- declared in <linux/scatterlist.h>,
 * which carries the full why-comment.  One entry per page for the byte range
 * [offset, offset + size) the caller names, from the native allocator; the
 * matching release is sg_free_table() below.  max_segment and gfp_mask are
 * accepted and dropped for the reasons the header note gives: nothing here
 * squashes runs, and the native allocator takes no flags.
 *
 * The entry count is the pages the range touches, clamped to the caller's
 * array length -- the same bound mainline ends up with after squashing, and
 * the reason the caller's own `nr_pages` is still the authority.
 */
int sg_alloc_table_from_pages_segment(struct sg_table* sgt,
				      struct page** pages,
				      unsigned int n_pages, unsigned int offset,
				      unsigned long size,
				      unsigned int max_segment, gfp_t gfp_mask)
{
	unsigned int nents, i;
	struct scatterlist* sgl;

	(void)max_segment;
	(void)gfp_mask;

	if (!sgt || !pages || !n_pages || !size)
		return -EINVAL;

	nents = (unsigned int)((offset + size + PAGE_SIZE - 1) >> PAGE_SHIFT);
	if (nents > n_pages)
		nents = n_pages;

	sgl = (struct scatterlist*)kcalloc(nents, sizeof(struct scatterlist));
	if (!sgl)
		return -ENOMEM;

	for (i = 0; i < nents; i++) {
		unsigned int pg_off = (i == 0) ? offset : 0;
		unsigned long len = PAGE_SIZE - pg_off;

		if (size < len)
			len = size;

		/* sg_set_page() lives in DCL/linux_kmod_shim.c beside
		 * sg_init_one(); it is what fills ->page as well as ->buf,
		 * which the page-walking writers in drm_prime.c read. */
		sg_set_page(&sgl[i], pages[i], (unsigned int)len, pg_off);
		size -= len;
	}

	sgt->sgl = sgl;
	sgt->nents = nents;
	sgt->orig_nents = nents;
	return 0;
}

/*
 * sg_alloc_table() -- declared in <linux/scatterlist.h>, added with the
 * virtio-gpu port: the command path builds its per-page table with it
 * before walking it, and the vram path a one-entry table. The table is
 * nents zeroed entries off the same heap sg_alloc_table_from_pages_segment()
 * above draws from, and the count is validated like the ones mainline
 * rejects: zero entries is not a table, and a byte length that would not
 * survive the allocator's word is refused before the multiply runs.
 */
int sg_alloc_table(struct sg_table* sgt, unsigned int nents, gfp_t gfp) {
	struct scatterlist* sgl;

	(void)gfp;
	if (!sgt || !nents)
		return -EINVAL;
	if (nents > (unsigned int)(0xffffffffu / sizeof(*sgl)))
		return -EINVAL;

	sgl = kmalloc((unsigned int)(nents * sizeof(*sgl)));
	if (!sgl)
		return -ENOMEM;
	memset(sgl, 0, (unsigned long)nents * sizeof(*sgl));

	sgt->sgl = sgl;
	sgt->nents = nents;
	sgt->orig_nents = nents;
	return 0;
}

/*
 * sg_free_table() -- declared in <linux/scatterlist.h>, where the note over
 * it explains why this stopped being an inline no-op: the constructor above
 * is now a real allocation, and kfree() is the body it needs.  The reset
 * afterwards keeps a freed table from being walked a second time.
 */
void sg_free_table(struct sg_table* sgt)
{
	if (!sgt)
		return;
	kfree(sgt->sgl);
	sgt->sgl = NULL;
	sgt->nents = 0;
	sgt->orig_nents = 0;
}

/* ── initcall runner ───────────────────────────────────────────────────── */

/*
 * linux/init.h stamps fs_initcall(fn) into __dcl_initcall_<fn>; there is no
 * initcall section in the PE image, so DCL names the ones it runs. Order
 * matters: chr_dev_init() publishes the /dev nodes the rest of boot expects.
 */
extern int (*const __dcl_initcall_chr_dev_init)(void);
extern int (*const __dcl_initcall_virtio_rng_driver_init)(void);
extern int (*const __dcl_initcall_virtio_console_init)(void);
extern int (*const __dcl_initcall_drm_core_init)(void);
extern int (*const __dcl_initcall_vkms_init)(void);
extern int (*const __dcl_initcall_bochs_pci_driver_init)(void);
extern int (*const __dcl_initcall_virtio_gpu_driver_init)(void);
#ifdef __XENEVA_BYPASS_VIRTIO_INPUT__
extern int (*const __dcl_initcall_virtio_input_driver_init)(void);
void dcl_input_bridge_init(void);
#endif

/*
 * init_user_ns -- the object tty_ioctl.c:843 takes the address of
 * (`checkpoint_restore_ns_capable(&init_user_ns)`).  Its type is declared in
 * <linux/capability.h> beside the predicate that consumes it; it is defined
 * here because it is a kernel object rather than a tty one, and this file is
 * where DCL keeps the rest of them.
 *
 * It is never dereferenced -- see the note in capability.h -- so what is
 * defined is an address and a placeholder member, not mainline's sixty fields
 * of uid/gid mapping.  It exists so that the expression at that call site has
 * something to name: without it the file would compile and the link would fail
 * on a symbol nobody would think to look for in a tty port.
 */
struct user_namespace init_user_ns = { 0 };

/*
 * Run one DCL_INITCALL() pointer and report the outcome in the shape the
 * DRM and virtio blocks below use: missing pointer, failed return, or the
 * success line.
 */
static void dcl_run_initcall(int (*fn)(void), const char* name,
			     const char* ok, const char* missing)
{
	int rc;

	if (!fn) {
		UARTDebugOut("[dcl]: %s initcall not linked\r\n", missing);
		return;
	}
	rc = fn();
	if (rc)
		UARTDebugOut("[dcl]: %s failed: %d\r\n", name, rc);
	else
		UARTDebugOut("[dcl]: %s\r\n", ok);
}

void DclRunInitcalls(void) {
	if (__dcl_initcall_chr_dev_init) {
		int rc = __dcl_initcall_chr_dev_init();
		if (rc)
			UARTDebugOut("[dcl]: chr_dev_init failed: %d\r\n", rc);
		else
			UARTDebugOut("[dcl]: chr_dev_init ok (mem devices registered)\r\n");
	}

	/*
	 * The vendored drivers. module_virtio_driver() in <linux/virtio.h>
	 * expands to an init function plus a DCL_INITCALL() pointer, and this
	 * function is where those pointers get named -- nothing discovers
	 * them, which is the whole reason the list is explicit.
	 *
	 * Order against chr_dev_init is arbitrary (the two are independent),
	 * but both must run before modload_test_run() calls
	 * virtio_rng_detect(), since detect() matches against the registered
	 * driver list and would otherwise find nothing to bind.
	 *
	 * This line is the half of bringing up a driver that does not show up
	 * in the build: omit it and virtio-rng.c compiles, links, and then
	 * silently never registers -- no error, no warning, and
	 * virtio_rng_detect() quietly reports no match. It is exactly the
	 * symptom the prebuilt .ko used to produce when its init_module()
	 * was not called, so it is worth having written down once.
	 */
	if (__dcl_initcall_virtio_rng_driver_init) {
		int rc = __dcl_initcall_virtio_rng_driver_init();
		if (rc)
			UARTDebugOut("[dcl]: virtio_rng register failed: %d\r\n", rc);
		else
			UARTDebugOut("[dcl]: virtio_rng driver registered\r\n");
	}

	/*
	 * Same for virtio_console, and for the same reason stated once above:
	 * compiled, linked, and never registered is indistinguishable from
	 * compiled, linked, and registered-but-unbound, and both look like
	 * "probe never called" from outside. This one runs last because it is
	 * the only one of the three whose probe makes device nodes of its own
	 * (/dev/vport*), and it must be on the driver list before
	 * virtio_console_detect() scans the bus in modload_test_run().
	 *
	 * A failure here is not cosmetic: virtio_console_init() registers a
	 * class and two drivers, and an error means the console port QEMU was
	 * told to create has nobody to bind it -- the vport self-test below
	 * would report "host not attached", which is also what it says when
	 * the device was never there, so the two cases are indistinguishable
	 * from the test's output alone.
	 */
	if (__dcl_initcall_virtio_console_init) {
		int rc = __dcl_initcall_virtio_console_init();
		if (rc)
			UARTDebugOut("[dcl]: virtio_console register failed: %d\r\n", rc);
		else
			UARTDebugOut("[dcl]: virtio_console driver registered\r\n");
	} else {
		UARTDebugOut("[dcl]: virtio_console initcall not linked\r\n");
	}

	/*
	 * drm core, then vkms -- module_init() in each vendored file stamps
	 * the DCL_INITCALL() pointer, and this is where the two get named.
	 * Order between them is not arbitrary: drm_core_init() registers
	 * major 226 and the minor idrs that vkms_init() allocates into when
	 * it creates the device, registers its connector, and calls
	 * drm_dev_register() -- which is what publishes /dev/dri/card0.
	 */
	dcl_run_initcall(__dcl_initcall_drm_core_init, "drm_core_init",
			 "drm core initialized", "drm core");

	dcl_run_initcall(__dcl_initcall_vkms_init, "vkms_init",
			 "vkms registered", "vkms");

	/*
	 * bochs -- the second scanout device, and the tree's first vendored
	 * PCI driver.  module_driver() in <linux/module.h> plus
	 * drm_module_pci_driver_if_modeset() in <drm/drm_module.h> expand to
	 * an initcall that registers bochs_pci_driver with the DCL PCI
	 * fabric (DCL/pci_shim.c), and the fabric is what walks ECAM,
	 * matches the id_table against 1234:1111 and calls the driver's
	 * probe.
	 *
	 * Last on purpose, and for a reason the driver itself cannot know:
	 * a DRM device takes the next free minor when it registers, so vkms
	 * has to bind first for it to stay card0 -- the order that existed
	 * before bochs arrived, kept so every path that named card0 keeps
	 * its meaning.  bochs then lands on card1, and the self-tests look
	 * for it there.
	 *
	 * On a machine without the device (every board this kernel really
	 * targets -- bochs-display exists in QEMU), the scan finds nothing,
	 * no probe runs, and the initcall reports exactly what the others
	 * report when their devices are absent.  The fabric logs how many
	 * devices it saw either way.
	 */
	dcl_run_initcall(__dcl_initcall_bochs_pci_driver_init, "bochs register",
			 "bochs pci driver registered", "bochs");

	/*
	 * virtio-gpu -- the third registration, and the first whose probe ends
	 * in a DRM card rather than a character device. Nothing is claimed
	 * here: the vendored virtio_gpu_driver_init()
	 * (Vendored/drivers/gpu/drm/virtio/virtgpu_drv.c:182) only puts the
	 * driver on the virtio list. A device appears when virtio_gpu_detect()
	 * runs, and the only caller of that is modload_test_run() under
	 * __XENEVA_BYPASS_VIRTGPU__ (see DCL/modload_test.c). A default build
	 * thus registers this driver as a bystander, which is the point: the
	 * internal virtgpu.dll owns 1AF4:1050 unless the image was packed
	 * without it.
	 */
	dcl_run_initcall(__dcl_initcall_virtio_gpu_driver_init,
			 "virtio_gpu register", "virtio_gpu driver registered",
			 "virtio_gpu");

#ifdef __XENEVA_BYPASS_VIRTIO_INPUT__
	/*
	 * The vendored input path.  Register the handler before the driver so
	 * input_register_device() finds it on the list; the devices themselves
	 * arrive later from modload_test_run()'s bus scan.
	 */
	dcl_input_bridge_init();
	dcl_run_initcall(__dcl_initcall_virtio_input_driver_init,
			 "virtio_input register", "virtio_input driver registered",
			 "virtio_input");
#endif
}
