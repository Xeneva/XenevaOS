#ifndef __LINUX_IOPORT_H__
#define __LINUX_IOPORT_H__

/*
 * DCL <linux/ioport.h> -- I/O window bookkeeping.
 *
 * Xeneva has no I/O region allocator: there is no ioport_resource tree, no
 * sparse iomem map, and no enforcement of exclusive claims. The serial code
 * still asks for regions (8250_pci.c:2904 request_mem_region, :2917
 * request_region, plus serial_core.c's resource bookkeeping), and the answer
 * to a claim has to be truthful: mainline treats a NULL return as "someone
 * else owns this window" and unwinds.
 *
 * So the claim is recorded in a small table with overlap detection and the
 * window is only refused if it genuinely collides -- a real check, just over
 * an eight-entry list instead of a resource tree. Reporting success blindly
 * would let two drivers walk over the same registers, which on a UART means
 * two writers corrupting each other's LCR; reporting failure blindly would
 * leave every port unbound. Bodies in DCL/linux_irq_shim.c.
 *
 * __iomem is already defined (empty) in <linux/kernel.h>.
 */

#include <linux/kernel.h>	/* resource_size_t, u32 */

/*
 * The three tree links, in mainline's order, plus the four fields this file
 * already had. They were added for drm_cache.c:204, which walks the I/O
 * memory map as a *tree* -- `for (tmp = iomem_resource.child; tmp;
 * tmp = tmp->sibling)` -- to find the highest mapped address and decide
 * whether a device with a narrower DMA mask would need bounce buffers.
 *
 * A struct with only start/end/name/flags would compile at the `child` read
 * and fail at the `sibling` one, and the two errors point at different tokens
 * in one expression, which is the shape a missing member has when the first
 * dereference happens to find something.
 *
 * The tree is empty -- iomem_resource is defined in a shim with a NULL child
 * -- because Xeneva maps nothing through request_mem_region() and has no
 * SWIOTLB to bounce against. So the loop does not run, max_iomem keeps its
 * initial 0, and drm_need_swiotlb() answers false, which is the correct
 * answer for this kernel rather than a stubbed one.
 */
struct resource {
	resource_size_t start;
	resource_size_t end;
	const char* name;
	unsigned long flags;
	struct resource* parent;
	struct resource* sibling;
	struct resource* child;
};

#define IORESOURCE_IO   0x00000100
#define IORESOURCE_MEM  0x00000200
#define IORESOURCE_IRQ  0x00000400
#define IORESOURCE_DMA  0x00000800
#define IORESOURCE_BUSY 0x80000000

/*
 * The root of the I/O memory tree, and an empty one -- see the note on struct
 * resource above for who reads it and why empty is the right answer here.
 * Defined in DCL/linux_irq_shim.c beside request_mem_region().
 */
extern struct resource iomem_resource;

/* Return the claim, or NULL if the window is already taken (mainline's rule). */
struct resource* request_mem_region(resource_size_t start, resource_size_t n,
					const char* name);
struct resource* request_region(resource_size_t start, resource_size_t n,
				const char* name);

void release_mem_region(resource_size_t start, resource_size_t n);
void release_region(resource_size_t start, resource_size_t n);

#endif /* __LINUX_IOPORT_H__ */
