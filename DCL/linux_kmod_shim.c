#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>		/* V2P() -- sg_phys() below */
#include <linux/virtio.h>
#include <linux/pagemap.h>	/* page_address() -- sg_set_page() is the one
				 * sg helper that has to turn a struct page* into
				 * a virtual address, which scatterlist.h says is
				 * why these three are calls and not inlines */
#include <linux/hw_random.h>
#include <Drivers/uart.h>

int __register_virtio_driver(struct virtio_driver* drv, void* owner) {
	(void)owner;
	UARTDebugOut("[dcl]: __register_virtio_driver('");
	if (drv && drv->driver.name)
		UARTDebugOut((const char*)drv->driver.name);
	UARTDebugOut("')\r\n");
	int rc = register_virtio_driver(drv);
	UARTDebugOut("[dcl]: register_virtio_driver -> %d\r\n", rc);
	return rc;
}

void virtio_reset_device(struct virtio_device* dev) {
	(void)dev;
	UARTDebugOut("[dcl]: virtio_reset_device (stub)\r\n");
}

#define DCL_GFP_ZERO 0x100u

void* kmalloc_caches[8][32];

void* __kmalloc_cache_noprof(void* cache, unsigned long flags, unsigned long size) {
	(void)cache;
	unsigned int sz = (unsigned int)size;
	void* p = kmalloc(sz);
	if (p && (flags & DCL_GFP_ZERO))
		memset(p, 0, sz);
	return p;
}

#define DCL_HWRNG_MAX 8
static void* _hwrng_table[DCL_HWRNG_MAX];
static int _hwrng_count = 0;

int hwrng_register(struct hwrng* rng) {
	if (!rng)
		return -1;
	if (_hwrng_count >= DCL_HWRNG_MAX)
		return -1;
	_hwrng_table[_hwrng_count++] = rng;
	UARTDebugOut("[dcl]: hwrng_register (");
	{
		const char* nm = *(const char**)rng;
		if (nm)
			UARTDebugOut(nm);
	}
	UARTDebugOut(")\r\n");
	return 0;
}

void hwrng_unregister(struct hwrng* rng) {
	for (int i = 0; i < _hwrng_count; i++) {
		if (_hwrng_table[i] == rng) {
			_hwrng_table[i] = _hwrng_table[_hwrng_count - 1];
			_hwrng_count--;
			return;
		}
	}
}

int hwrng_selftest(void) {
	if (_hwrng_count <= 0) {
		UARTDebugOut("[dcl]: hwrng read test: no rng registered\r\n");
		return -1;
	}
	struct hwrng* rng = (struct hwrng*)_hwrng_table[0];
	UARTDebugOut("[dcl]: hwrng read test, name=");
	if (rng->name)
		UARTDebugOut(rng->name);
	UARTDebugOut("\r\n");
	if (!rng->read) {
		UARTDebugOut("[dcl]: driver has no ->read\r\n");
		return -2;
	}
	unsigned char buf[32];
	int got = rng->read(rng, buf, 32, 1);
	UARTDebugOut("[dcl]: virtio_read -> ");
	UARTDebugOut("%d", got);
	if (got > 0) {
		UARTDebugOut(" bytes:");
		int n = got > 32 ? 32 : got;
		for (int i = 0; i < n; i++) {
			if (buf[i] < 0x10)
				UARTDebugOut(" 0%x", buf[i]);
			else
				UARTDebugOut(" %x", buf[i]);
		}
	}
	UARTDebugOut("\r\n");
	return got;
}

int hwrng_read_bytes(void* buf, unsigned int max) {
	if (!buf || !max)
		return -1;
	if (_hwrng_count <= 0)
		return -2;
	struct hwrng* rng = (struct hwrng*)_hwrng_table[0];
	if (!rng->read)
		return -3;
	return (int)rng->read(rng, buf, max, 1);
}

#define ERESTARTSYS_ 512

extern int AuSchedYieldMs(uint64_t ms);

void complete(struct completion* x) {
	if (!x)
		return;
	*(unsigned int*)x = 1;
}

int wait_for_completion_killable(struct completion* x) {
	unsigned int spins = 0;
	if (!x)
		return -ERESTARTSYS_;
	while (!*(unsigned int*)x) {
		virtio_poll_vqs();
		if (*(unsigned int*)x)
			break;
		/* This wait runs from a read syscall, which entered with IRQs
		 * masked. Spinning here stops the timer, so the desktop does not
		 * move until the spin gives up. Sleep and let the compositor run.
		 * A few polls above still catch a reply already in the ring. */
		if (AuSchedYieldMs(1) != 0) {
			if (++spins > 64)
				return -ERESTARTSYS_;
			continue;
		}
		if (++spins > 100)
			return -ERESTARTSYS_;
	}
	*(unsigned int*)x = 0;
	return 0;
}

void __init_swait_queue_head(void* q, const char* name, void* key) {
	(void)q;
	(void)name;
	(void)key;
}

int ida_alloc_range(struct ida* ida, unsigned int min, unsigned int max, gfp_t gfp) {
	(void)gfp;
	unsigned int id = ida->next_id;
	if (id < min)
		id = min;
	if (id > max)
		return -28;
	ida->next_id = id + 1;
	return (int)id;
}

void ida_free(struct ida* ida, unsigned int id) {
	(void)ida;
	(void)id;
}

void sg_init_one(struct scatterlist* sg, const void* buf, unsigned int len) {
	if (!sg)
		return;
	sg->buf = (void*)buf;
	sg->len = len;
	/*
	 * And no page. <linux/scatterlist.h> says this line is part of the
	 * contract sg_page() answers: an entry built by sg_init_one() names a
	 * buffer, not a page, so sg_page() on it must say NULL -- which is
	 * what free_buf()'s release loop (virtio_console.c:350-357) tests
	 * before it breaks. Leaving whatever the last user of this slot put
	 * there would have it put_page() on a pointer that is no longer
	 * anyone's page.
	 */
	sg->page = NULL;
}

/*
 * sg_phys() -- the physical address an entry covers. The entries that reach
 * it are the shmem helper's: one per object, naming its whole contiguous run
 * through the linear-map alias (DCL/drm_shmem_shim.c's dcl_shmem_make_sgt --
 * "->buf carries the linear-map alias so sg_phys() can undo the P2V"). V2P()
 * is that mapping's own inverse, so undoing it is the whole translation,
 * page offset included.
 *
 * It also has to be arithmetic rather than a page-table walk. The first cut
 * walked (AuGetPhysicalAddress()); the direct map is installed as 1 GiB
 * block descriptors (BootAA64/paging.cpp: XEPagingInstallPhysicalDirectMap),
 * and a walker that tests bit 0 without PTE_TABLE follows one of those blocks
 * as if it were a child table and dereferences whatever physical memory lies
 * at the address it decodes. That is exactly how this function first ran:
 * the virtio-gpu object path (the console bridge's first resource) had the
 * walk read a block's target as an L2 table and fault on a garbage L3
 * pointer. vmm_get_leaf() already carries the block rule; this translation
 * simply never saw a block-mapped address until the shmem tables did.
 */
dma_addr_t sg_phys(const struct scatterlist* sg)
{
	if (!sg || !sg->buf)
		return 0;

	return (dma_addr_t)V2P((uint64_t)(uintptr_t)sg->buf);
}

/*
 * sg_page / sg_set_page / sg_init_table -- the page-shaped half of the sg
 * API, declared in <linux/scatterlist.h> with a note about why the bodies
 * are here rather than in that header: sg_set_page() needs page_address(),
 * which lives in <linux/pagemap.h>, and pulling that into scatterlist.h
 * would pull it into <linux/virtio.h> and therefore into every driver.
 *
 * In DCL's struct scatterlist the page is bookkeeping (see the struct's own
 * note) and buf is the address the queue machinery walks -- so sg_set_page()
 * fills both at once: buf as page_address(page) + offset, mainline's own
 * sg_virt() arithmetic done up front because there is no mapping step to do
 * it later. A NULL page is not an error to report; sg_init_one() never has
 * one, and a caller that reaches for sg_page() on such an entry is in the
 * `break` arm of a release loop rather than in a path that can be fixed by
 * returning a lie.
 */
struct page* sg_page(const struct scatterlist* sg) {
	return sg ? sg->page : NULL;
}

void sg_set_page(struct scatterlist* sg, struct page* page, unsigned int len,
				 unsigned int offset) {
	if (!sg)
		return;
	sg->page = page;
	sg->buf = page ? (char*)page_address(page) + offset : NULL;
	sg->len = len;
}

void sg_init_table(struct scatterlist* sgl, unsigned int n) {
	if (!sgl)
		return;
	/*
	 * Clear every field across all n entries, not just the first: the
	 * caller has asked for a table and is entitled to read any slot it
	 * has not filled, and a stale buf from a previous use would be
	 * handed to the queue as though someone had put it there. mainline
	 * also terminates a chain bit DCL's struct has no room for -- a
	 * linear array of regions walked by index does not need one.
	 */
	for (unsigned int i = 0; i < n; i++) {
		sgl[i].buf = NULL;
		sgl[i].len = 0;
		sgl[i].page = NULL;
	}
}
