/*
 * DCL/drm_shmem_shim.c -- the drm_gem_shmem helper bodies the vendored
 * virtio-gpu driver links against.
 *
 * mainline's helper backs a GEM object with shmemfs pages reached through
 * the page cache, pins them on demand, and describes them with the sg
 * machinery. Xeneva has no shmem filesystem and no page cache. What it has
 * is the backing DCL/drm_stubs.c built for the dumb-buffer path in stage 2:
 * shmem_file_setup() allocates one physically contiguous buddy range,
 * zeroes it, and parks its base in filp->private_data, and
 * drm_gem_object_init() keeps that file as obj->filp.
 *
 * These bodies are that same backing wearing mainline's API. The object is
 * whatever drm_gem_shmem_init() makes of a driver-supplied struct --
 * virtio-gpu subclasses it (virtio_gpu_create_object() is its
 * gem_create_object hook, and the first-member chain is what lets
 * drm_gem_shmem_free() release the whole wrapper) -- the "pages" are a
 * one-entry sg_table whose buffer is the linear-map alias of the range, and
 * every question the page cache would answer is answered here with
 * P2V/P2V-inverse arithmetic.
 *
 * What is deliberately absent is the machinery for a backing that can move:
 * purge, madvise, partial eviction, swap-in. A contiguous buddy allocation
 * is resident from creation to free; there is nothing to evict, and a
 * shrinker that cannot shrink is a promise in the wrong direction. The
 * declarations left without bodies in drm_gem_shmem_helper.h are exactly
 * that set, so the linker is the check that no caller in this tree needs
 * one.
 *
 * Only virtio-gpu pulls this file in. bochs and vkms take the two
 * DRM_GEM_SHMEM_DRIVER_OPS symbols from DCL/drm_stubs.c
 * (drm_gem_shmem_dumb_create, drm_gem_shmem_prime_import_no_map), which
 * build the dcl_gem_dumb object those drivers' dumb buffers have always
 * used.
 */
#include <stddef.h>
#include <stdint.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/iosys-map.h>
#include <linux/list.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#include <Mm/pmmngr.h>		/* P2V */
#include <Mm/vmmngr.h>		/* PAGE_SIZE */

/*
 * dcl_shmem_phys() -- the physical base of an object's range. The fake
 * "shmem file" is the only carrier: shmem_file_setup() wrote the buddy base
 * into private_data, and the object has owned the file since
 * drm_gem_object_init().
 */
static uint64_t dcl_shmem_phys(const struct drm_gem_shmem_object* shmem)
{
	const struct file* filp = shmem->base.filp;

	return filp ? (uint64_t)(uintptr_t)filp->private_data : 0;
}

/*
 * MAP_DUMB needs to tell a shmem object from vkms/bochs's dcl_gem_dumb
 * object without including a driver's private funcs table (virtio's is
 * static).  Keep the objects this file initialised on a short list; it is
 * also where the process mapping address lives, since the vendored shmem
 * object has nowhere DCL may put one.
 */
struct dcl_shmem_entry {
	struct drm_gem_shmem_object* shmem;
	uint64_t uva;
	struct dcl_shmem_entry* next;
};

static struct dcl_shmem_entry* dcl_shmem_entries;

static struct dcl_shmem_entry*
dcl_shmem_find(const struct drm_gem_shmem_object* shmem) {
	struct dcl_shmem_entry* e;

	for (e = dcl_shmem_entries; e; e = e->next)
		if (e->shmem == shmem)
			return e;
	return NULL;
}

bool dcl_gem_is_shmem(struct drm_gem_object* obj) {
	return dcl_shmem_find(to_drm_gem_shmem_obj(obj)) != NULL;
}

/* drm_stubs.c: map one contiguous backing range into the caller's process. */
extern long dcl_drm_map_user_pages(uint64_t phys, size_t size, uint64_t* uva);

long dcl_shmem_map_user(struct drm_gem_object* obj, uint64_t* uva) {
	struct drm_gem_shmem_object* shmem = to_drm_gem_shmem_obj(obj);
	struct dcl_shmem_entry* e = dcl_shmem_find(shmem);
	uint64_t phys;
	int ret;

	if (!e)
		return -ENOENT;
	if (e->uva) {
		*uva = e->uva;
		return 0;
	}

	phys = dcl_shmem_phys(shmem);
	if (!phys)
		return -ENOMEM;
	ret = (int)dcl_drm_map_user_pages(phys, (size_t)shmem->base.size,
					  &e->uva);
	if (ret)
		return ret;
	*uva = e->uva;
	return 0;
}

static void dcl_shmem_unregister(struct drm_gem_shmem_object* shmem) {
	struct dcl_shmem_entry** pp = &dcl_shmem_entries;

	while (*pp) {
		struct dcl_shmem_entry* e = *pp;

		if (e->shmem == shmem) {
			*pp = e->next;
			if (e->uva)
				AuFreePages(e->uva, false,
					    (size_t)shmem->base.size);
			kfree(e);
			return;
		}
		pp = &e->next;
	}
}

/*
 * dcl_shmem_make_sgt() -- a fresh one-entry table over the whole range.
 *
 * mainline's tables are per-page walks of the page cache; one contiguous
 * allocation is one physically contiguous run, and one entry spanning the
 * buffer is the accurate description, not a simplification. The consumer
 * (virtio_gpu_object_shmem_init()) has to name a resource's whole backing in
 * a virtio_gpu_mem_entry list, and one entry whose address and length cover
 * the resource is the shape QEMU's device accepts and the shape the
 * sg_phys() arm reads back.
 *
 * ->buf carries the linear-map alias so sg_phys() can undo the P2V;
 * dma_address/dma_length carry the same run for the DMA-API arm, which this
 * port never selects but whose walkers exist and must read well-formed
 * values. ->page stays NULL for the reason sg_init_one()'s entries do (see
 * <linux/scatterlist.h>): the entry names a buffer, not a page.
 */
static struct sg_table* dcl_shmem_make_sgt(const struct drm_gem_shmem_object* shmem)
{
	struct sg_table* sgt;
	struct scatterlist* sgl;
	uint64_t phys = dcl_shmem_phys(shmem);
	uint64_t size = (uint64_t)shmem->base.size;

	if (!phys)
		return ERR_PTR(-ENOMEM);

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	sgl = kcalloc(1, sizeof(*sgl));
	if (!sgl) {
		kfree(sgt);
		return ERR_PTR(-ENOMEM);
	}

	sgl->buf = (void*)(uintptr_t)P2V(phys);
	sgl->len = (unsigned int)size;
	sgl->page = NULL;
	sgl->dma_address = phys;
	sgl->dma_length = (unsigned int)size;

	sgt->sgl = sgl;
	sgt->nents = 1;
	sgt->orig_nents = 1;
	return sgt;
}

/*
 * drm_gem_shmem_init() -- allocate the object's backing. The one step with
 * a DCL counterpart is drm_gem_object_init(): it calls shmem_file_setup(),
 * which is where the contiguous range comes from. mainline's init also
 * associates the file's page cache with the object; there are no pages in a
 * cache here, so there is nothing to associate.
 */
int drm_gem_shmem_init(struct drm_device* dev,
		       struct drm_gem_shmem_object* shmem, size_t size)
{
	struct dcl_shmem_entry* e;
	int ret;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	ret = drm_gem_object_init(dev, &shmem->base, size);
	if (ret) {
		kfree(e);
		return ret;
	}

	e->shmem = shmem;
	e->next = dcl_shmem_entries;
	dcl_shmem_entries = e;
	return 0;
}

/*
 * drm_gem_shmem_create() -- mainline's create, over the backing above.
 *
 * The driver-supplied arm is the one virtio-gpu takes: its gem_create_object
 * hook allocates a wrapper whose first member chain starts with this struct,
 * sets the object funcs, and hands the base back. The fallback arm and the
 * init failure path both kfree(shmem) directly -- correct for a wrapper too,
 * because the chain that lets &bo->base be freeable as one allocation is the
 * same chain that makes shmem the allocation's address.
 */
struct drm_gem_shmem_object* drm_gem_shmem_create(struct drm_device* dev,
						  size_t size)
{
	struct drm_gem_shmem_object* shmem;
	int ret;

	size = (size + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);

	if (dev->driver->gem_create_object) {
		struct drm_gem_object* obj;

		obj = dev->driver->gem_create_object(dev, size);
		if (IS_ERR(obj))
			return ERR_CAST(obj);
		shmem = to_drm_gem_shmem_obj(obj);
	} else {
		shmem = kzalloc(sizeof(*shmem), GFP_KERNEL);
		if (!shmem)
			return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&shmem->madv_list);

	ret = drm_gem_shmem_init(dev, shmem, size);
	if (ret) {
		kfree(shmem);
		return ERR_PTR(ret);
	}

	return shmem;
}

/*
 * drm_gem_shmem_free() -- release everything the object owns.
 *
 * fput() is a no-op on this port (no open-file table; see
 * DCL/drm_stubs.c's note), so the frames and the fake file are released
 * here, in the same order and by the same calls dcl_gem_dumb_free() uses.
 * The sgt goes first: it points into the range being released.
 *
 * No lock is taken and no refcount is asserted. mainline guards this with
 * the object's dma_resv and WARNs on a live vmap; DCL's vmap is stateless
 * (the linear map is always there) and the callers already own the last
 * reference when they get here -- the reference the funcs free handler
 * received.
 */
void drm_gem_shmem_free(struct drm_gem_shmem_object* shmem)
{
	struct drm_gem_object* obj = &shmem->base;
	struct file* filp = obj->filp;
	uint64_t phys = dcl_shmem_phys(shmem);

	if (shmem->sgt) {
		sg_free_table(shmem->sgt);
		kfree(shmem->sgt);
		shmem->sgt = NULL;
	}

	dcl_shmem_unregister(shmem);

	if (phys)
		AuPmmngrReleasePages(phys);

	drm_gem_object_release(obj);
	kfree(filp);
	kfree(shmem);
}

/*
 * drm_gem_shmem_print_info() -- the debugfs one-liner. mainline reports the
 * page-cache accounting; one contiguous range has a base, a size and a pin
 * count, so those are what it reports.
 */
void drm_gem_shmem_print_info(const struct drm_gem_shmem_object* shmem,
			      struct drm_printer* p, unsigned int indent)
{
	drm_printf(p, "%*sshmem: %u pages, pins %d\n", indent, "",
		   (unsigned int)(shmem->base.size / PAGE_SIZE),
		   refcount_read(&shmem->pages_pin_count));
}

/*
 * pin_locked / unpin_locked -- mainline's pair keeps a shrinker from
 * evicting pages while a device maps the object. There is no shrinker and
 * no eviction here: the allocation stays resident until free. The counter
 * still moves, because it is the struct's account of the state and
 * drm_gem_shmem_is_purgeable() reads it; that the balance is all an
 * observer can see is the point.
 */
int drm_gem_shmem_pin_locked(struct drm_gem_shmem_object* shmem)
{
	refcount_inc(&shmem->pages_pin_count);
	return 0;
}

void drm_gem_shmem_unpin_locked(struct drm_gem_shmem_object* shmem)
{
	refcount_dec(&shmem->pages_pin_count);
}

/*
 * vmap_locked / vunmap_locked -- mainline maps the page array into the
 * kernel's iomem window and counts the window's users. The linear map of a
 * contiguous buddy allocation is already the single mapping such a caller
 * wants (stage 2's note in DCL/drm_stubs.c says the same about
 * drm_gem_fb_vmap()), so vmap hands back P2V(base) and vunmap clears the
 * caller's descriptor. Both are stateless, hence no vmap_use_count: there is
 * no window to open or close.
 */
int drm_gem_shmem_vmap_locked(struct drm_gem_shmem_object* shmem,
			      struct iosys_map* map)
{
	uint64_t phys = dcl_shmem_phys(shmem);

	if (!phys)
		return -ENOMEM;

	iosys_map_set_vaddr(map, (void*)(uintptr_t)P2V(phys));
	return 0;
}

void drm_gem_shmem_vunmap_locked(struct drm_gem_shmem_object* shmem,
				 struct iosys_map* map)
{
	(void)shmem;
	iosys_map_clear(map);
}

/*
 * drm_gem_shmem_mmap() -- mainline faults the shmem file's pages into the
 * vma. DCL has no file-backed mmap to fault into: the devfs bridge routes
 * none (see the note opening <linux/mm.h>'s vma section), so the call
 * answers -ENOSYS instead of pretending a pfn remap happened. Nothing in
 * this tree can reach it -- it is only reached from drm_gem_mmap_obj(),
 * which runs from an mmap() the bridge never routes. Userspace reaches GEM
 * buffers through MAP_DUMB's hook in DCL/drm_stubs.c, which asks
 * dcl_shmem_map_user() below to map this object's backing directly.
 */
int drm_gem_shmem_mmap(struct drm_gem_shmem_object* shmem,
		       struct vm_area_struct* vma)
{
	(void)shmem;
	(void)vma;
	return -ENOSYS;
}

/*
 * drm_gem_shmem_get_sg_table() -- a caller-owned table, freshly built.
 * mainline's semantic is the same: the caller frees it, and nothing is
 * cached on the object.
 */
struct sg_table* drm_gem_shmem_get_sg_table(struct drm_gem_shmem_object* shmem)
{
	return dcl_shmem_make_sgt(shmem);
}

/*
 * drm_gem_shmem_get_pages_sgt() -- the cached table, mainline's accessor for
 * "the object's pages as sg". First caller builds it; failures are not
 * cached, so a later attempt can retry (mainline also caches only success).
 * The caller is virtio_gpu_object_shmem_init() at resource-attach time.
 */
struct sg_table* drm_gem_shmem_get_pages_sgt(struct drm_gem_shmem_object* shmem)
{
	struct sg_table* sgt;

	if (shmem->sgt)
		return shmem->sgt;

	sgt = dcl_shmem_make_sgt(shmem);
	if (IS_ERR(sgt))
		return sgt;

	shmem->sgt = sgt;
	return sgt;
}

/*
 * drm_gem_shmem_vm_ops -- the object the shmem funcs table points at
 * (virtgpu_object.c:138). mainline's carries fault/open/close/access
 * handlers; DCL's <linux/mm.h> reduces the type to the two fields the rest
 * of the core compiles against, and no vma is ever constructed on this port
 * (the devfs bridge has no ->mmap routing), so there is no mapping for a
 * handler to serve. An all-zero table is the accurate description of a
 * mapping path that is not wired; the symbol exists because the vendored
 * funcs table requires one to be named.
 */
const struct vm_operations_struct drm_gem_shmem_vm_ops = {
	.access = 0,
	.close = 0,
};
