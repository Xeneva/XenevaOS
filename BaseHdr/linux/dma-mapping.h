#ifndef __LINUX_DMA_MAPPING_H
#define __LINUX_DMA_MAPPING_H

/*
 * DCL <linux/dma-mapping.h> -- the DMA-buffer fence markers, and nothing else.
 *
 * mainline's header is 27 KB: the dma_alloc_coherent family, the dmam_alloc
 * family, dma_map_sg, dma_sync_single_*, the attrs and direction enums, the
 * DMA_MASK checks. None of it has a caller in this tree, and all of it would
 * need a DMA layer behind it -- which Xeneva does not have. What a ported
 * source reaches for today is a pair of macros that fence a buffer inside a
 * struct, so that is what this is. If a driver needs real allocation, that is
 * the moment to decide whether the linear map is the answer and to write the
 * rest of the header then.
 *
 * The two macros are mainline's, including the quirk that matters:
 *
 *     #define __dma_from_device_group_begin(GROUP) \
 *         __cacheline_group_begin(GROUP) ____dma_from_device_aligned
 *
 * NO trailing semicolon (the caller's `;` terminates the member), and GROUP
 * may be empty -- virtio-rng.c:34 and :38 write `__dma_from_device_group_begin();`
 * with nothing inside the parens. See the note in <linux/cache.h>.
 *
 * ARCH_HAS_DMA_MINALIGN is not defined for this target, so
 * ____dma_from_device_aligned is the empty spelling mainline falls back to
 * too -- no extra alignment. That is not an oversight: virtio-rng.c guards
 * its buffer with `#if SMP_CACHE_BYTES < 32` and sizes it itself, so the
 * cacheline (64) already governs the layout. ARCH_DMA_MINALIGN itself is
 * defined now, in <asm/cache.h>, for drm_managed.c's struct drmres -- but it
 * is ARCH_HAS_DMA_MINALIGN that the #ifdef below tests, and that one stays
 * undefined, so the layout here does not move.
 */

#include <linux/cache.h>	/* __cacheline_group_begin/end, SMP_CACHE_BYTES */
#include <linux/kernel.h>	/* gfp_t, dma_addr_t, size_t */
/* mainline's first include: enum dma_data_direction, which this header's
 * dma_sync_* prototypes and everything downstream (dma-buf.h) name. */
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>	/* struct scatterlist, struct sg_table -- dma_map_sg* */

struct device;

/*
 * DMA_BIT_MASK(n) -- mainline defines it here, and vkms_drv.c:188
 * (vkms_create) is this tree's only use: dma_coerce_mask_and_coherent(dev,
 * DMA_BIT_MASK(64)).  Without the macro the argument compiles as an implicit
 * call to a function named DMA_BIT_MASK returning int -- no compile error,
 * because -Wno-implicit-function-declaration is on -- and the mask would be
 * a 32-bit value from a function that does not exist.  The #ifndef is
 * belt-and-braces for a second definition arriving with some other port; the
 * spelling is mainline's.
 */
#ifndef DMA_BIT_MASK
#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#endif

/*
 * dma_alloc_coherent() / dma_free_coherent() -- the one pair the fence macros
 * above were standing in front of.
 *
 * mainline's header declares a whole family (the dmam_ allocators, dma_map_sg,
 * the direction enum, the mask checks) and this comment used to say none of it
 * had a caller. virtio_console.c:439 is a caller:
 *
 *     buf->buf = dma_alloc_coherent(buf->dev, buf_size, &buf->dma, GFP_KERNEL);
 *
 * -- the rproc-serial arm of alloc_buf(), the arm DCL folds away (see
 * CONFIG_REMOTEPROC in <linux/autoconf.h>). It has to *compile*, and it has to
 * link if the compiler ever declines to fold it, so the two declarations are
 * here and the bodies are in DCL/linux_mm_shim.c.
 *
 * What they do: dma_alloc_coherent() is a plain kmalloc with the address
 * handed back as the dma_addr_t as well. That is sound rather than convenient
 * -- Xeneva's virtio path takes a *virtual* address and translates it at kick
 * time (see <linux/scatterlist.h> on why buf is a linear-map pointer), so the
 * only thing this tree ever does with buf->dma is hand it straight back to
 * dma_free_coherent() at :373, and a token that round-trips is a token that
 * works. There is no streaming mapping here to need a bus address, and no
 * IOMMU to need an address the CPU cannot reach.
 *
 * The parameter types are mainline's, so a caller written against mainline
 * compiles without a cast: `struct device *` (incomplete is fine -- nothing
 * here dereferences it), size_t, `dma_addr_t *`, gfp_t.
 */
void* dma_alloc_coherent(struct device* dev, size_t size,
			 dma_addr_t* dma_handle, gfp_t gfp);
void dma_free_coherent(struct device* dev, size_t size, void* vaddr,
		       dma_addr_t handle);

#ifdef ARCH_HAS_DMA_MINALIGN
#define ____dma_from_device_aligned __aligned(ARCH_DMA_MINALIGN)
#else
#define ____dma_from_device_aligned
#endif

/* Mark the start of a DMA buffer. */
#define __dma_from_device_group_begin(GROUP)	\
	__cacheline_group_begin(GROUP) ____dma_from_device_aligned

/* Mark the end of a DMA buffer. */
#define __dma_from_device_group_end(GROUP)	\
	__cacheline_group_end(GROUP) ____dma_from_device_aligned

/*
 * DMA_ATTR_* -- the streaming-mapping attribute bits, of which
 * DMA_ATTR_SKIP_CPU_SYNC is the only one this tree uses (drm_prime.c:684 and
 * :710, in the prime export path). Defined as mainline's values rather than
 * renumbered, so a later port that carries two of them does not have to
 * reconcile two schemes; the rest are carried for the same reason, and none
 * of them is read anywhere.
 *
 * mainline: include/linux/dma-mapping.h.
 */
#define DMA_ATTR_WRITE_BARRIER		(1UL << 0)
#define DMA_ATTR_WEAK_ORDERING		(1UL << 1)
#define DMA_ATTR_WRITE_COMBINE		(1UL << 2)
#define DMA_ATTR_NON_CONSISTENT		(1UL << 3)
#define DMA_ATTR_NO_KERNEL_MAPPING	(1UL << 4)
#define DMA_ATTR_SKIP_CPU_SYNC		(1UL << 5)
#define DMA_ATTR_FORCE_CONTIGUOUS	(1UL << 6)
#define DMA_ATTR_ALLOC_SINGLE_PAGES	(1UL << 7)
#define DMA_ATTR_NO_WARN		(1UL << 8)
#define DMA_ATTR_MERGEABLE		(1UL << 9)
#define DMA_ATTR_CPU_MAPPABLE		(1UL << 10)
#define DMA_ATTR_FORCE_DMA32		(1UL << 11)
#define DMA_ATTR_ALLOC_PAGES		(1UL << 12)

/*
 * dma_map_sg() -- map `nents` sg entries for the device and report how many
 * DMA segments they became.
 *
 * The answer is `nents` unchanged, which is the identity mapping: with no
 * IOMMU and no bounce buffers, an entry is already contiguous and already
 * addressable, so nothing coalesces. That is the same reasoning
 * dma_alloc_coherent() above uses for handing the virtual address back as the
 * dma_addr_t -- and drm_need_swiotlb() in drm_cache.c answering false is the
 * other half of it. The @attrs word is mainline's and, at this mapping
 * granularity, has nothing to act on: DMA_ATTR_SKIP_CPU_SYNC in particular
 * only defers a sync that never happens here.
 *
 * Declared here, defined in DCL/linux_mm_shim.c beside the two allocators.
 */
int dma_map_sg(struct device* dev, struct scatterlist* sg, int nents,
	       enum dma_data_direction dir);
void dma_unmap_sg(struct device* dev, struct scatterlist* sg, int nents,
		  enum dma_data_direction dir);

/*
 * dma_map_sgtable() -- mainline's wrapper, and mainline's body verbatim: map
 * the list, and on success rewrite nents to the count that survived. The
 * -EINVAL arm is mainline's for the "mapped to nothing" case, which cannot
 * happen against the dma_map_sg() above but is kept so a real mapper could
 * be dropped in without the contract changing.
 *
 * Inline rather than a shim because the whole of it is the wrapper -- there is
 * no state of its own, and mainline keeps it in the header too.
 */
static inline int dma_map_sgtable(struct device* dev, struct sg_table* sgt,
				  enum dma_data_direction dir,
				  unsigned long attrs)
{
	int nents;

	(void)attrs;	/* see the note above: nothing here reads it */

	nents = dma_map_sg(dev, sgt->sgl, sgt->nents, dir);
	if (nents <= 0)
		return nents < 0 ? nents : -EINVAL;

	sgt->nents = (unsigned int)nents;
	return 0;
}

/*
 * dma_unmap_sgtable() -- mainline's wrapper, and mainline's body verbatim
 * (include/linux/dma-mapping.h): unmap the table's *original* entries.
 * drm_prime.c:710 (drm_gem_unmap_dma_buf) is the only caller.  Inline for the
 * same reason dma_map_sgtable() above is -- the whole of it is the call --
 * and `attrs` has nothing left to act on because dma_unmap_sg() below
 * performs no unmapping (see the note over dma_map_sg).
 *
 * orig_nents rather than nents is mainline's choice and matters to the
 * contract even here: dma_map_sgtable() rewrites nents to the post-map
 * count, while unmapping must cover what was handed in.
 */
static inline void dma_unmap_sgtable(struct device* dev, struct sg_table* sgt,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	(void)attrs;
	dma_unmap_sg(dev, sgt->sgl, (int)sgt->orig_nents, dir);
}

/*
 * dma_coerce_mask_and_coherent() / dma_max_mapping_size() -- the two
 * remaining DMA operations a vendored caller names as symbols.  Both are
 * mainline declarations in this header; the callers reach it directly
 * (vkms_drv.c:187, drm_prime.c:862), and both bodies are in
 * DCL/linux_mm_shim.c beside dma_map_sg() above.  The reasons they cannot be
 * inlines are there: one would dereference dev->dma_mask fields that fs.h's
 * pinned struct device does not name, the other has a value to answer with.
 */
int dma_coerce_mask_and_coherent(struct device* dev, u64 mask);
size_t dma_max_mapping_size(struct device* dev);

/*
 * dma_set_max_seg_size() -- mainline's cap on one DMA segment, read by
 * virtio_gpu_probe() (Vendored/drivers/gpu/drm/virtio/virtgpu_drv.c:102:
 * `dma_set_max_seg_size(dev->dev, dma_max_mapping_size(dev->dev) ?: UINT_MAX)`).
 * There is no DMA layer here for a segment limit to bind -- dma_map_sg()
 * above is the identity and the virtio queues carry linear-map buffers --
 * and the caller does not read the result, so the whole of the contract that
 * matters is "accept the call". 0 is success, mainline's own answer for a
 * limit it stored. Inline because there is no state to keep.
 */
static inline int dma_set_max_seg_size(struct device* dev, unsigned int size)
{
	(void)dev;
	(void)size;
	return 0;
}

/*
 * dma_map_resource()/dma_unmap_resource()/dma_mapping_error() -- the
 * resource mapping trio, added with the virtio-gpu port. One caller:
 * virtgpu_vram.c maps a host-visible blob's physical range before hanging
 * it on an sg element and checks the result with dma_mapping_error(). That
 * path is unreachable on a device with no host-visible region, and the
 * bodies are the identity this port's DMA story is throughout (dma_map_sg
 * above; the queues carry linear-map buffers): the address maps to itself,
 * the unmap has nothing to tear down, and 0 answers "not an error".
 *
 * u64 for mainline's phys_addr_t: the name is not spelled anywhere in this
 * tree and u64 is what it aliases on arm64.
 */
static inline dma_addr_t dma_map_resource(struct device* dev, u64 phys,
					  size_t size,
					  enum dma_data_direction dir,
					  unsigned long attrs)
{
	(void)dev;
	(void)size;
	(void)dir;
	(void)attrs;
	return (dma_addr_t)phys;
}

static inline void dma_unmap_resource(struct device* dev, dma_addr_t addr,
				      size_t size,
				      enum dma_data_direction dir,
				      unsigned long attrs)
{
	(void)dev;
	(void)addr;
	(void)size;
	(void)dir;
	(void)attrs;
}

static inline bool dma_mapping_error(struct device* dev, dma_addr_t addr)
{
	(void)dev;
	(void)addr;
	return false;
}

/*
 * dma_sync_sgtable_for_device() -- mainline's sync-a-whole-table entry
 * point, added with the virtio-gpu port. DCL has no caches to sync (the
 * queues carry linear-map buffers and the device side is QEMU's
 * emulation), and every caller reaches it behind the driver's use_dma_api
 * test, which is false without VIRTIO_F_ACCESS_PLATFORM -- the only kind
 * of virtio-gpu device this tree scans. A no-op rather than a forward to a
 * dma_sync_sg_for_device() that does not exist here.
 */
static inline void dma_sync_sgtable_for_device(struct device* dev,
					       struct sg_table* sgt,
					       enum dma_data_direction dir)
{
	(void)dev;
	(void)sgt;
	(void)dir;
}

#endif /* __LINUX_DMA_MAPPING_H */
