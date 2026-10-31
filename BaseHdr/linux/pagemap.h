#ifndef __LINUX_PAGEMAP_H__
#define __LINUX_PAGEMAP_H__

/*
 * DCL <linux/pagemap.h> -- the eight page primitives virtio_console.c uses.
 *
 * mainline's is 50 KB and its first include is <linux/mm.h>, which is the
 * whole memory-management subsystem (rmap, page tables, compaction, the NUMA
 * balancing state reached through gfp.h).  The driver's entire contact with
 * the page model is:
 *
 *     alloc_page(GFP_KERNEL)   :875   splice fallback buffer
 *     page_address(page)       :888   destination of the copy
 *     kmap_local_page / kunmap :887   source window over pipe_buffer.page
 *     get_page / put_page      :868   refcount, and free_buf()'s release loop
 *     lock_page / unlock_page  :869   taken around the steal
 *     sg_page / sg_set_page    :354   <linux/scatterlist.h>, which is DCL's own
 *     PAGE_SIZE / PAGE_MASK    :884   <Mm/vmmngr.h>, Xeneva's native geometry
 *
 * so this header defines exactly that and nothing else.
 *
 * The page *contents* live inside struct page itself.  DCL has no buddy-to-
 * struct-page array and no virtual-address lookup, and splice is not routed
 * by the devfs bridge yet (see <linux/splice.h>), so the honest shape for a
 * page here is one kmalloc'd block that is both the descriptor and the
 * storage.  alloc_page() therefore returns a block whose ->data is what
 * page_address() hands out, which is an ordinary kernel heap pointer -- the
 * same kind of pointer that sg_init_one() already feeds to the virtio queue,
 * so the DMA translation in DCL/virtio_shim.c needs no new case for it.
 */

#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/mm.h>		/* DCL's hub header: uaccess (copy_*_user),
				 * register_chrdev, min/ARRAY_SIZE, PAGE_SIZE */
#include <Mm/vmmngr.h>		/* PAGE_SHIFT, PAGE_SIZE, PAGE_MASK */

struct page {
	unsigned int refcount;
	char data[PAGE_SIZE];
};

/*
 * page_address(page) -- the data block of a struct page.
 *
 * It used to be spelled `((page)->data)`, which is right for every call that
 * passes a `struct page *` and wrong the moment anything else does: a member
 * reference through a `void *` argument is a hard error (one `->` on a void
 * base), and virtio_console.c:887 passes `buf->page` from <linux/splice.h>,
 * where the field is `void *` because DCL's pipe_buffer has no page array
 * behind it. Adding the offset instead of dereferencing answers both forms
 * with one expression, and it answers them the same way -- __builtin_offsetof
 * needs no <stddef.h> and gives the exact displacement the `->` did.
 *
 * The argument is still read as a struct page pointer, which is what it is:
 * the alternative (treating it as a raw buffer) would silently shift a real
 * page by the refcount's four bytes.
 */
/*
 * The parameter is named `p`, not `page`: a macro argument replaces every
 * token that spells it, including the one in `struct page` two lines down,
 * and `__builtin_offsetof(struct p, data)` is a diagnostic about an expected
 * comma at an expression that was never meant to hold one.
 */
#define page_address(p)								\
	((void*)((char*)(void*)(p) + __builtin_offsetof(struct page, data)))
#define lowmem_page_address(page) page_address(page)

/*
 * kmap_local_page / kunmap_local -- mainline maps the page into the caller's
 * address space for the duration of the statement.  DCL's kernel is a single
 * flat higher-half mapping, so a page's address is already its address: the
 * map is the identity and the unmap is a statement that has to stay, because
 * the driver writes it and it has to parse.
 */
#define kmap_local_page(page)	page_address(page)
#define kunmap_local(addr)	((void)(addr))

void* alloc_page(gfp_t gfp);

/*
 * Refcounts are real: alloc_page() starts at 1, get_page() adds one,
 * put_page() drops one and frees at zero, which is the contract free_buf()
 * relies on at :350-357 (it walks sgpages and releases each page exactly
 * once).
 */
void get_page(struct page* page);
void put_page(struct page* page);

/*
 * lock_page / unlock_page -- no-ops, deliberately.  The only pair in the
 * driver is :868-869, inside pipe_to_sg(), which runs only when splice is
 * actually routed, and the devfs bridge does not route it yet.  Making them
 * real would mean a per-page waitqueue for a path that cannot be entered;
 * a no-op here is the same class of decision as device_find_child() ->
 * NULL and pm_runtime_enabled() -> 0.  If splice is ever wired up, this is
 * the one line in this file that has to change.
 */
void lock_page(struct page* page);
void unlock_page(struct page* page);

#endif /* __LINUX_PAGEMAP_H__ */
