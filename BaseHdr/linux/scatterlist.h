#ifndef __LINUX_SCATTERLIST_H__
#define __LINUX_SCATTERLIST_H__

/*
 * DCL <linux/scatterlist.h> -- struct scatterlist and sg_init_one().
 *
 * ### Deliberately not mainline's layout
 *
 * mainline's struct scatterlist is
 *
 *     unsigned long page_link;   // page pointer + chain/terminate bits
 *     unsigned int  offset;
 *     unsigned int  length;
 *     dma_addr_t    dma_address;
 *
 * -- a *page-based* list, whose whole job is to walk a compound page in
 * physical segments and to carry a DMA address that the IOMMU/streaming
 * mapper filled in. Xeneva has neither: a buffer handed to a virtio queue is
 * a single linear-map virtual address (AuVirtioPCISetupQueue and buf_phys()
 * in virtio_shim.c turn it into a ring address directly), and there is no
 * DMA API to populate dma_address through. So the struct here carries exactly
 * what the one consumer reads:
 *
 *     struct scatterlist { void* buf; unsigned int len; };
 *
 * That is the shape DCL/linux_kmod_shim.c's sg_init_one() fills in, and the
 * shape virtio_shim.c's virtqueue_add_inbuf/sgs walk -- both of which are
 * written against this definition, not against upstream's. Keeping upstream's
 * fields here would leave four members that every write has to remember to
 * leave zero, for a mapping layer that does not exist.
 *
 * The consequence worth writing down: a driver that reaches into the sg for a
 * page (`sg_virt()` on a compound page, DMA mapping helpers) will not build.
 * Add those when something needs them, with the linear-map address doing the
 * job of `page_link`.
 *
 * This struct used to live in <linux/virtio.h>, which included it in every
 * file that talked to a virtqueue. It is here because that is mainline's home
 * for it, so a ported source includes the header mainline tells it to; virtio.h
 * now includes this file instead of redeclaring the type. Exactly one
 * definition either way -- two would be two different `struct scatterlist`s
 * with no way to pass one to the other.
 */

#include <linux/kernel.h>	/* size_t */

/*
 * One contiguous region, as the shim sees it. buf is a *virtual* address in
 * the linear map; the shim converts to a bus address at kick time.
 *
 * `page` is the one member that is not read by the queue machinery -- it is
 * the bookkeeping a *releasing* driver walks. virtio_console.c:350-357 does
 *
 *     for (i = 0; i < buf->sgpages; i++) {
 *         struct page *page = sg_page(&buf->sg[i]);
 *         if (!page)
 *             break;
 *         put_page(page);
 *     }
 *
 * and sg_set_page() at :872/:891 is what would have put the pointer there.
 * Without a slot for it sg_page() has nothing to return but NULL, the `break`
 * fires on the first entry, and the pages behind it are never released: a
 * leak that no test in this tree would report, because the only path that
 * fills sgpages > 0 is the splice one (see <linux/splice.h> on why that is
 * not reachable here). Carrying the pointer costs 8 bytes an entry and makes
 * the release loop do what it reads as doing.
 *
 * buf stays the first member: every existing walk (virtio_shim.c,
 * linux_kmod_shim.c's sg_init_one) reads ->buf and ->len by name, and putting
 * a pointer in front of them would move those offsets for no gain.
 */
struct scatterlist {
	void* buf;
	/*
	 * One length, two spellings. DCL's writers (sg_init_one,
	 * sg_set_page, the shmem tables) name it `len`; mainline's readers
	 * name it `length` -- virtgpu_object.c:195 copies sg->length into a
	 * virtio_gpu_mem_entry -- and vendored sources are not edited to a
	 * local spelling. A union keeps the two names on one storage, so a
	 * write through either is visible through the other.
	 */
	union {
		unsigned int len;
		unsigned int length;
	};
	struct page* page;
	/*
	 * mainline's two DMA fields, carried in full even though nothing
	 * fills them: there is no DMA mapping layer in this kernel, so
	 * dma_address is never written and always reads 0. drm_prime.c:889
	 * still walks it (`sg_dma_address(sgt->sgl)`) and drm_prime.c:893
	 * walks sg_dma_len(), so the *reads* have to be well-formed even
	 * where the writes never happen -- which is the same situation as
	 * iomem_resource in <linux/ioport.h>: an empty but correctly shaped
	 * answer, rather than a missing member.
	 *
	 * mainline makes these conditional on CONFIG_NEED_SG_DMA_LENGTH; here
	 * they are unconditional, so sg_dma_len() below has one arm instead
	 * of two.
	 */
	dma_addr_t dma_address;
	unsigned int dma_length;
};

/*
 * struct sg_table -- the *list* of scatterlists, and the reason this header
 * had to grow it is a C scoping rule rather than a missing field.
 *
 * drm_cache.h:41 declares `void drm_clflush_sg(struct sg_table *st);` and
 * drm_cache.c:127 defines the same function. Both spell the parameter
 * identically, and clang still reports "conflicting types for
 * 'drm_clflush_sg'" -- because nothing named `struct sg_table` at file scope
 * before those lines. C scopes a struct tag that is *first* mentioned inside a
 * function prototype's parameter list to that declaration alone, so the two
 * lines name two unrelated incomplete types and the signatures genuinely do
 * differ. clang warns about the rule first ("declaration of 'struct sg_table'
 * will not be visible outside of this function", -Wvisibility) and errors
 * after, and reading only the error sends you hunting for a difference
 * between two spellings that are byte-identical.
 *
 * mainline does not hit this because mainline's <linux/scatterlist.h>
 * *defines* struct sg_table, so the tag is at file scope before any drm
 * header is parsed. A forward declaration already exists here in drm_drv.h:67,
 * but drm_cache.h does not include it -- and moving the decl there would fix
 * only this consumer, leaving the next header that names the type to
 * rediscover the same rule.
 *
 * All three of mainline's fields, whole, though nothing in this tree fills one
 * yet: `sgl` is the array, `nents` the entries that were mapped, and
 * `orig_nents` the entries that were handed in. A two-field version would be
 * worse than carrying all three -- see the note on struct vm_area_struct in
 * <linux/mm.h> for why.
 */
struct sg_table {
	struct scatterlist* sgl;
	unsigned int nents;
	unsigned int orig_nents;
};

/*
 * sg_init_one() -- point a single sg entry at a whole buffer.
 *
 * Declared here, defined in DCL/linux_kmod_shim.c (also exported to loaded
 * modules through KernelAA64/kernel_exports.c, which is how the old prebuilt
 * virtio_rng.ko reached it). It now also clears ->page: an sg entry built by
 * sg_init_one() names no page, and sg_page() on it answers NULL, which is
 * what a caller walking a mixed list to release it needs to see for the
 * entries that came from the buffer path rather than the page path.
 */
extern void sg_init_one(struct scatterlist* sg, const void* buf,
			unsigned int len);

/*
 * sg_page() / sg_set_page() / sg_init_table() -- declarations only, with the
 * bodies in DCL/linux_kmod_shim.c beside sg_init_one().
 *
 * They cannot be static inlines here without dragging <linux/pagemap.h> in:
 * sg_set_page() has to turn a struct page* into the address buf should point
 * at, and page_address() is defined there. mainline gets this for free
 * because its scatterlist.h and pagemap.h are the same subsystem; in DCL
 * pagemap.h sits on top of mm.h and this header is included by
 * <linux/virtio.h>, which nearly every driver pulls in -- so the dependency
 * goes the other way and the three become calls.
 *
 * The signatures are mainline's, including sg_set_page()'s `offset` (bytes
 * into the page) which lands in buf as page_address(page) + offset, and
 * sg_init_table()'s count, which clears buf/len/page across `n` entries so a
 * partially filled list never hands the queue a stale pointer.
 */
extern struct page* sg_page(const struct scatterlist* sg);
extern void sg_set_page(struct scatterlist* sg, struct page* page,
			unsigned int len, unsigned int offset);
extern void sg_init_table(struct scatterlist* sgl, unsigned int n);

/*
 * sg_virt() -- the linear-map virtual address of an sg entry.
 *
 * mainline computes it from the page; here buf *is* the virtual address, so
 * the helper is a field read. Provided because it is the accessor ported code
 * reaches for, and returning nothing but `buf` is correct rather than merely
 * convenient.
 */
static inline void* sg_virt(const struct scatterlist* sg)
{
	return (void*)sg->buf;
}

static inline unsigned int sg_len(const struct scatterlist* sg)
{
	return sg->len;
}

/*
 * struct sg_page_iter and the for_each_sg*_page family -- the page-granular
 * walk over an sg list, needed by drm_prime.c:1059 (a local
 * `struct sg_page_iter page_iter`, so the type has to be complete, not merely
 * forward-declared) and by its for_each_sgtable_page() loop at :1061.
 *
 * mainline's five fields are all here. The sixth, __sg_page_nents, is not
 * mainline's and exists because of a difference this header documents above:
 * mainline's scatterlist carries chain and terminate *bits* in page_link, so
 * __sg_page_iter_next() finds the end of the list with sg_is_last(). DCL's
 * scatterlist is {buf, len, page} with no bits to spare and no terminator,
 * so the bound has to be carried alongside the walk instead. It is added
 * rather than substituted: every mainline field keeps its name and meaning,
 * and one more records what sg_is_last() would have.
 *
 * __sg_page_index stays 0 for every entry, because an sg entry here maps one
 * buffer and nth_page() would have nothing to index into. It is kept so that
 * sg_page_iter_page() can be spelled mainline's way.
 */
struct sg_page_iter {
	struct scatterlist* sg;
	unsigned int __sg_page_accum;
	unsigned int __sg_page_index;
	unsigned int entry;
	unsigned int offset;
	unsigned int __sg_page_nents;
};

static inline struct page* sg_page_iter_page(struct sg_page_iter* pgi)
{
	return sg_page(pgi->sg);
}

/*
 * Both static inline rather than shim functions: neither touches shared
 * state, and the header already answers the other accessors that way
 * (sg_virt(), sg_len() above). mainline declares them extern because
 * mainline's bodies walk compound pages through nth_page(); there is no
 * compound page here to walk.
 */
static inline void __sg_page_iter_start(struct sg_page_iter* pgi,
					struct scatterlist* sglist,
					unsigned int nents, unsigned int pgoff)
{
	pgi->sg = sglist;
	pgi->entry = 0;
	pgi->offset = pgoff;
	pgi->__sg_page_accum = 0;
	pgi->__sg_page_index = 0;
	pgi->__sg_page_nents = nents;
}

static inline int __sg_page_iter_next(struct sg_page_iter* pgi)
{
	if (pgi->entry >= pgi->__sg_page_nents)
		return 0;
	pgi->sg += 1;
	pgi->entry += 1;
	return 1;
}

#define for_each_sg_page(sglist, pgi, nents, pgoff)			\
	for (__sg_page_iter_start(pgi, sglist, nents, pgoff);		\
	     __sg_page_iter_next(pgi);)

#define for_each_sgtable_page(sgt, pgi, pgoff)				\
	for_each_sg_page((sgt)->sgl, pgi, (sgt)->orig_nents, pgoff)

/*
 * The DMA-address walk, and the accessors behind it -- drm_prime.c:889 and
 * :893 (sg_dma_address, sg_dma_len) and :894 (for_each_sgtable_dma_sg).
 *
 * Both accessors are field reads off the two members added to struct
 * scatterlist above. sg_dma_len() has one arm because dma_length is
 * unconditional here; mainline's second arm reads ->length when
 * CONFIG_NEED_SG_DMA_LENGTH is off, and there is no config to be off.
 */
#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)		((sg)->dma_length)

/*
 * The table walk keeps mainline's spelling -- for_each_sgtable_dma_sg(sgt,
 * sg, i) -- and threads the caller's variable through to the entry walk
 * instead of naming a fixed `sg` inside it: virtgpu_prime.c:174 walks with
 * `sl`, and a macro that hardcodes the name cannot serve that caller. The
 * bound is explicit because DCL's entries carry no chain/terminator bit (see
 * struct scatterlist).
 */
#define for_each_sg_dma(sglist, sg, nents, i)				\
	for ((i) = 0; (i) < (nents) && ((sg) = &(sglist)[(i)]); (i)++)

#define for_each_sgtable_dma_sg(sgt, sg, i)				\
	for_each_sg_dma((sgt)->sgl, sg, (sgt)->nents, i)

/*
 * for_each_sg() -- mainline's plain entry walk, added with the virtio-gpu
 * port. The caller is virtgpu_object.c:193, filling a resource's attach
 * mem-entry list: with VIRTIO_F_ACCESS_PLATFORM never offered, the addresses
 * come from sg_phys() and every entry counts (orig_nents, not the
 * dma-degraded nents).
 *
 * The spellings are mainline's, same trade as for_each_sg_dma() above: the
 * loop variable is the hardcoded `sg`, and the bound is explicit because
 * DCL's entries carry no chain/terminator bit (see struct scatterlist).
 */
#define for_each_sg(sglist, sg, nents, i)				\
	for ((i) = 0; (i) < (nents) && ((sg) = &(sglist)[(i)]); (i)++)

#define for_each_sgtable_sg(sgt, sg, i)					\
	for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)

/*
 * struct sg_dma_page_iter and its walk -- the address-granular counterpart,
 * asked for by drm_prime.c:1085 (a local `struct sg_dma_page_iter dma_iter`,
 * so the type has to be complete) and its for_each_sgtable_dma_page() loop
 * and sg_page_iter_dma_address() call at :1088.
 *
 * Two spellings there are worth recording, because both look wrong and are
 * not. First, sg_page_iter_dma_address() is named for the *page* iter but is
 * called with a `struct sg_dma_page_iter *`; it takes that type, which is
 * the only spelling under which mainline's own call site compiles. Second,
 * for_each_sgtable_dma_page() hands `&(pgi)->base` to the page walk rather
 * than driving the outer struct -- the dma iteration *is* the page iteration
 * with a dma address read alongside it, so there is one advance step and not
 * two to keep in sync.
 *
 * mainline: include/linux/scatterlist.h.
 *
 * sg_page_iter_dma_len() is the one shape that differs: mainline caps it at
 * PAGE_SIZE, because mainline's iterator yields one page per step and the
 * remainder of a larger entry belongs to the next step. Here
 * __sg_page_iter_next() above advances one whole sg entry at a time, so
 * there is no next step to hand the remainder to and the cap would truncate
 * the answer to a value no caller could use to cover the entry. The rest --
 * sg_dma_len minus the offset -- is the length that actually stands.
 */
struct sg_dma_page_iter {
	struct sg_page_iter base;
	dma_addr_t dma_address;
	unsigned int __sg_dma_page_accum;
};

static inline dma_addr_t
sg_page_iter_dma_address(struct sg_dma_page_iter* pgi)
{
	return sg_dma_address(pgi->base.sg) + pgi->base.offset;
}

static inline unsigned int
sg_page_iter_dma_len(struct sg_dma_page_iter* pgi)
{
	return sg_dma_len(pgi->base.sg) - pgi->base.offset;
}

#define for_each_sgtable_dma_page(sgt, pgi, pgoff)			\
	for_each_sg_page((sgt)->sgl, &(pgi)->base, (sgt)->nents, pgoff)

/*
 * sg_alloc_table_from_pages_segment() -- build an sg_table over an array of
 * pages, mainline's lib/scatterlist.c helper.  The one caller is
 * drm_prime_pages_to_sg() (drm_prime.c:865), the generic
 * drm_gem_object_funcs.get_sg_table implementation: it has already allocated
 * the struct sg_table and hands over the page array plus the byte range the
 * buffer covers.  The body is in DCL/linux_mm_shim.c and allocates one entry
 * per page, which is the granularity DCL's iterators above walk
 * (__sg_page_iter_next() advances by entry) rather than mainline's squashing
 * of physically contiguous runs.
 *
 * `offset` is the byte offset into the first page, `size` the valid byte
 * count after it, `max_segment` mainline's cap on a squashed run; that cap
 * has nothing to apply to here and is accepted and ignored (as the call
 * site's UINT_MAX-derived value already implies).  `gfp_mask` likewise: the
 * array comes from the native allocator, which takes no flags.
 */
int sg_alloc_table_from_pages_segment(struct sg_table* sgt,
				      struct page** pages, unsigned int n_pages,
				      unsigned int offset, unsigned long size,
				      unsigned int max_segment, gfp_t gfp_mask);

/*
 * sg_free_table() -- release the scatterlist array behind a table.
 *
 * This was a static-inline no-op as long as every table in the tree was
 * filled in place: sg_init_table() above allocates nothing.  The allocator
 * above is the first producer whose array is on the heap, so its release has
 * to be real; the two callers are both in drm_prime.c (:686 on the map
 * failure path, :711 on unmap), and both tables got there through a
 * get_sg_table() helper, i.e. through that allocator.  A caller holding its
 * own array still has sg_init_table() and no release to call.
 *
 * Declared here, defined in DCL/linux_mm_shim.c: the body is kfree(), and
 * this header deliberately does not include <linux/slab.h> to name it.
 * mainline declares the same function in this header.
 */
/*
 * sg_alloc_table() -- allocate a table of `nents` zeroed entries, the
 * entry point sg_init_table() (whose callers fill in place) is the static
 * form of. Added with the virtio-gpu port: the command path sizes its
 * per-page table from it and the vram path its one-entry mapping. Body in
 * DCL/linux_mm_shim.c beside sg_free_table().
 */
int sg_alloc_table(struct sg_table* sgt, unsigned int nents, gfp_t gfp);

void sg_free_table(struct sg_table* sgt);

/*
 * sg_phys() -- the physical address an entry covers. mainline spells it
 * page_to_phys(sg_page(sg)) + sg->offset; a DCL entry names its buffer
 * directly (struct scatterlist above). The entries that reach this are the
 * shmem tables' linear-map aliases, so the answer is the direct map's own
 * inverse: V2P() of ->buf, page offset included. Body in
 * DCL/linux_kmod_shim.c beside sg_init_one().
 *
 * The caller is the non-DMA-API arm of virtgpu_object.c:194's mem-entry
 * fill; mainline declares this in the same header.
 */
dma_addr_t sg_phys(const struct scatterlist* sg);

#endif /* __LINUX_SCATTERLIST_H__ */
