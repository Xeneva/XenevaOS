#ifndef __LINUX_MM_H__
#define __LINUX_MM_H__

/*
 * DCL <linux/mm.h> -- the hub header for ports that live under drivers/char.
 *
 * A mainline drivers/char/*.c includes mm.h and then leans on its own
 * transitive chain for printk, VFS helpers, scheduler hooks, errno values
 * and the mmap descriptor type. Xeneva's DCL headers do not have that chain,
 * so mm.h collects it here rather than editing the vendored source. Items
 * mainline actually declares in include/linux/fs.h or include/linux/major.h
 * are marked as such.
 */

#include <linux/autoconf.h>
#include <linux/kernel.h>	/* types, errno, GFP_KERNEL, __user */
#include <linux/compiler.h>	/* likely/unlikely */
#include <linux/fs.h>		/* file/inode/cdev/device ABI pins */
#include <linux/mount.h>	/* the VFS helpers mainline has in fs.h -- *			 * file_inode, iminor, inode_lock/unlock,
				 * noop_llseek -- which DCL's pinned fs.h
				 * cannot carry. See the note there. */
#include <linux/mm_types.h>	/* struct folio + page_folio() helpers --
				 * drm_gem.c/drm_gem_shmem_helper.c batch
				 * folios and mainline mm.h provides these */
#include <linux/printk.h>	/* printk() -- mem.c never includes this directly */
#include <linux/init.h>
#include <linux/uaccess.h>
#include <Mm/vmmngr.h>\t\t/* PAGE_SIZE/PAGE_SHIFT: Xeneva's native page geometry */

/*
 * vma_flags_t / mk_vma_flags() / VMA_NORESERVE_BIT -- the packed VMA flag
 * word v7.2 hands to shmem_file_setup().  One caller in this tree:
 * drm_gem_object_init() (drm_gem.c:189) builds the word and passes it on.
 * Xeneva has no VMAs to carry flags, and the shmem shims take the word but
 * do not read it, so the type exists for that expression's sake and the bit
 * is a position rather than a policy.
 */
typedef unsigned long vma_flags_t;

#define VMA_NORESERVE_BIT 0

static inline vma_flags_t mk_vma_flags(unsigned int bit)
{
	return (vma_flags_t)1 << bit;
}

/* ── general helpers (mainline: include/linux/{kernel,overflow}.h) ─────── */

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef min_t
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif
#ifndef fallthrough
#define fallthrough __attribute__((fallthrough))
#endif

/* errno values kernel.h does not carry yet */
#ifndef EFAULT
#define EFAULT		14
#endif
#ifndef EFBIG
#define EFBIG		27
#endif
#ifndef ENXIO
#define ENXIO		6
#endif
#ifndef ERESTARTSYS
#define ERESTARTSYS	512
#endif

/* ── scheduler surface (mainline: linux/sched/{signal.h}) ────────────────
 * Xeneva's DCL ports are polled and run to completion inside the syscall;
 * no preemption point and no signal delivery to unwind out of a copy. */

#ifndef need_resched
#define need_resched() 0
#endif
#ifndef cond_resched
#define cond_resched() ((void)0)
#endif
#ifndef signal_pending
#define signal_pending(p) 0
#endif
#ifndef current
#define current ((void*)0)
#endif

/*
 * The VFS helpers that used to open this section -- file_inode(), iminor(),
 * inode_lock()/inode_unlock() and noop_llseek() -- have moved to
 * <linux/mount.h>, included at the top of this file so ports reaching them
 * through mm.h keep working. mainline keeps all five in its fs.h, DCL's fs.h
 * is pinned and cannot carry them, and mount.h has taken that fs.h surface
 * over (it already had simple_pin_fs() and unmap_mapping_range() for the same
 * reason). The drm set reaches them from mount.h directly, which is what let
 * them leave here; nothing in mm.h's own body uses them.
 */

/* mainline include/linux/major.h */
#define MEM_MAJOR 1

/* lseek origins (mainline: uapi asm-generic/fcntl.h) */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/*
 * mainline include/linux/fs.h: register_chrdev() is a static inline that
 * forwards to __register_chrdev(), which the cdev bridge implements.
 */
extern int __register_chrdev(unsigned int major, unsigned int baseminor,
							 unsigned int count, const char* name,
							 const void* fops);
extern void __unregister_chrdev(unsigned int major, unsigned int baseminor,
								unsigned int count, const char* name);

static inline int register_chrdev(unsigned int major, const char* name,
								  const struct file_operations* fops) {
	return __register_chrdev(major, 0, 256, name, (const void*)fops);
}

static inline void unregister_chrdev(unsigned int major, const char* name) {
	__unregister_chrdev(major, 0, 256, name);
}

/* used by open_port(); DCL keeps no iomem mapping object */
static inline void* iomem_get_mapping(void) { return (void*)0; }

/* mainline include/linux/fs.h; fmode/fop flag bits mem.c ORs into struct file */
#define FMODE_NOWAIT		0x00080000
#define FOP_UNSIGNED_OFFSET	0x0001

/* ── mmap descriptor surface (mainline: linux/mm.h + vma.h) ──────────────
 * Xeneva's devfs bridge has no ->mmap routing yet, so these types exist so
 * the mainline mmap arms compile; the shims below report "unsupported"
 * instead of pretending a pfn remap happened. */

typedef uint64_t pgprot_t;

struct vm_operations_struct {
	void* access;	/* generic_access_phys only under CONFIG_HAVE_IOREMAP_PROT */
	void* close;
	/*
	 * DCL delta: mainline's open() callback, added with the virtio-gpu
	 * port -- virtgpu_vram.c:30 names .open = drm_gem_vm_open. No VMA is
	 * ever constructed on this port, so nothing calls it; the field exists
	 * so the vendored initialiser compiles, the same reason the two above
	 * are void pointers rather than typed function pointers.
	 */
	void* open;
};

/* ── struct vm_area_struct ───────────────────────────────────────────────
 *
 * mainline keeps this in <linux/mm_types.h>. DCL's cannot: the struct's
 * vm_page_prot member is a pgprot_t, pgprot_t is defined at the top of this
 * file, and mm_types.h is included *by* it -- the definition would have to
 * run the other way. So it lives here, next to the type it needs.
 *
 * It is the whole struct and not just the members drm reads, because a
 * partial struct is indistinguishable from a complete one until someone adds
 * a member: the compiler would accept a write to a field that is not there
 * and place it at whatever offset it had computed, which is the failure mode
 * an ABI pin exists to prevent.
 *
 * The members are mainline's names and mainline's order for the ones
 * drm_gem.c touches -- vm_start, vm_end, vm_flags, vm_page_prot, vm_pgoff,
 * vm_private_data, vm_ops -- plus vm_file, which mainline carries in the same
 * place and which nothing here reads yet. This is a layout nothing persists
 * and nothing outside this kernel sees: Xeneva's devfs bridge has no ->mmap
 * routing (see the note opening this section), so no vma is ever constructed.
 * The struct exists so that drm_gem.c's mmap half compiles, and drm_gem.c
 * only ever reads these members back.
 */
typedef vma_flags_t vm_flags_t;

struct vm_area_struct {
	unsigned long vm_start;
	unsigned long vm_end;
	vm_flags_t vm_flags;
	pgprot_t vm_page_prot;
	unsigned long vm_pgoff;
	void *vm_private_data;
	struct file *vm_file;
	const struct vm_operations_struct *vm_ops;
};

/*
 * The four VM_* bits drm names, at drm_gem.c:1247 and :1254.
 *
 * They are mainline's long-standing numbers, and they are spelled as numbers
 * rather than as mainline v7.2 spells them -- v7.2 moved the entire VM_* set
 * onto a VMA_*_BIT enum whose numbering is generated by a DECLARE_VMA_BIT()
 * list in that header, and reproducing the generated order here would be a
 * claim about a bit layout that nothing in this kernel can read back. What
 * matters is that the four are distinct, that they do not collide with the
 * VMA_NORESERVE_BIT and VMA_SHARED_BIT already named above, and that the
 * flag word they are OR'd into is the same word vm_flags_set() writes and
 * vm_get_page_prot() reads -- all three of which are here.
 */
#define VM_PFNMAP	0x00000400	/* the whole range is one page table */
/*
 * DCL delta: mainline's flag, added with the virtio-gpu port --
 * virtgpu_vram.c:58 sets it on a host-visible blob mapping. Same value as
 * linux-7.2's include/linux/mm.h; nothing in DCL reads it, and no VMA is
 * built on this port (see the note above struct vm_operations_struct).
 */
#define VM_MIXEDMAP	0x00000100
#define VM_IO		0x00004000	/* mapped I/O or similar */
#define VM_DONTEXPAND	0x00080000	/* mremap may not grow this */
#define VM_DONTDUMP	0x02000000	/* leave out of the core dump */

/*
 * vm_flags_set() -- OR flags into a vma. mainline's also asserts the mmap
 * lock is held and goes through a private atomic copy of the flag word;
 * neither has a referent here, and the assertion would fire on every call
 * since nothing takes a lock that nothing releases.
 */
static inline void vm_flags_set(struct vm_area_struct *vma, vm_flags_t flags)
{
	vma->vm_flags |= flags;
}

/*
 * vma_pages() -- the length of a mapping in pages, from its bounds.
 * drm_gem.c:1380 passes the result of this to drm_gem_object_lookup_at_offset()
 * as the extent to search. Undefined, it was an implicit int-returning
 * function, which is a warning -- the reason it is defined rather than left
 * implicit is that the argument it feeds is a u64 offset limit, and a 32-bit
 * return would silently truncate a mapping larger than 4 GB of pages.
 */
static inline unsigned long vma_pages(const struct vm_area_struct *vma)
{
	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

/*
 * PAGE_ALIGNED(addr) -- mainline's "is this address page-aligned" (v7.2
 * mm.h), added with the virtio-gpu port: the command path warns on the
 * answer before walking a buffer page by page, and with the macro
 * undeclared that line compiled as an implicit call. uintptr_t, not
 * unsigned long: addresses here hold the top of the 64-bit space and long
 * is 32 bits on this LLP64 target -- the same width note the allocation
 * casts carry.
 */
#define PAGE_ALIGNED(addr) (((uintptr_t)(addr) & (PAGE_SIZE - 1)) == 0)

/*
 * is_vmalloc_addr() / vmalloc_to_page() -- mainline declares both in this
 * header (v7.2 mm.h:1849 and :1859), and that is how the virtio-gpu port's
 * command path reaches them: the translation unit's include list carries no
 * <linux/vmalloc.h>, only the DRM headers that land here. There is no
 * vmalloc address range in this tree (see <linux/vmalloc.h>), so no pointer
 * can be inside of one and there is no page to translate; both answers are
 * exactly the ones mainline gives for a kmalloc'd argument, which is what
 * every caller here passes. Added with the virtio-gpu port, whose vbuf
 * assembly tests a buffer before walking it page by page and translates it
 * when the test says vmalloc.
 */
struct page;

static inline bool is_vmalloc_addr(const void* addr)
{
	(void)addr;
	return false;
}

static inline struct page* vmalloc_to_page(const void* addr)
{
	(void)addr;
	return NULL;
}

/*
 * vm_get_page_prot() -- turn VMA flags into a page-table protection.
 *
 * There are no page tables to translate into, and the answer is never
 * observed: drm_gem.c:1255-1256 pipes it through pgprot_writecombine() and
 * pgprot_decrypted(), both of which below return their argument unchanged,
 * into vma->vm_page_prot -- a field nothing reads. Returning 0 is "no
 * attributes", which is what these flags mean before an arch adds one.
 *
 * A static inline rather than the exported function mainline has in
 * mm/pgprot.c, so no link symbol is added for a body that is one line.
 */
static inline pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	(void)vm_flags;
	return 0;
}

/*
 * pgprot_writecombine() / pgprot_decrypted() -- the two transformations
 * drm_gem.c:1255-1256 applies in turn. mainline's are arch hooks: arm64's
 * writecombine clears the cacheability bits to a write-combining encoding,
 * and the decrypted pair is AMD SEV. Xeneva has neither a cache attribute
 * encoding to clear nor an encrypted memory to decrypt, so both are the
 * identity -- which is why vm_get_page_prot()'s answer can be 0 with nothing
 * lost.
 *
 * mainline's home for pgprot_writecombine() is <linux/pgtable.h> and
 * arm64's asm/pgtable.h; there is no such header here, and pgprot_t lives in
 * this file, so this is where they go.
 */
static inline pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return prot;
}

static inline pgprot_t pgprot_decrypted(pgprot_t prot)
{
	return prot;
}

/*
 * pgprot_noncached() -- arm64's "map uncached" attribute, the sibling of
 * pgprot_writecombine() above and the same identity for the same reason.
 * Added with the virtio-gpu port: the vram mmap path selects it for blobs
 * whose host mapping is uncached, and that path is only reachable on a
 * device with a host-visible region -- none of the ones this tree scans.
 */
static inline pgprot_t pgprot_noncached(pgprot_t prot)
{
	return prot;
}

/*
 * io_remap_pfn_range() -- mainline's remap of a pfn range into a VMA
 * (arm64 declares it; the generic form is a #define to remap_pfn_range).
 * One caller, the same unreachable vram mmap path as pgprot_noncached()
 * above. There is no pfn remap on this port -- the devfs bridge builds no
 * VMAs for drivers (see the vma section note) -- so the honest answer is
 * mainline's "cannot remap" failure, which the one caller already has a
 * branch for.
 */
static inline int io_remap_pfn_range(struct vm_area_struct *vma,
				     unsigned long addr, unsigned long pfn,
				     unsigned long size, pgprot_t prot)
{
	(void)vma;
	(void)addr;
	(void)pfn;
	(void)size;
	(void)prot;
	return -ENOSYS;
}

struct vm_area_desc {
	struct file* file;
	unsigned long vm_flags;
	unsigned long pgoff;
	unsigned long len;
	pgprot_t page_prot;
	const struct vm_operations_struct* vm_ops;
	struct {
		int error_override;
	} action;
};

#define VMA_SHARED_BIT 3

static inline size_t vma_desc_size(const struct vm_area_desc* desc) {
	return (size_t)desc->len;
}
static inline int vma_desc_test(const struct vm_area_desc* desc, int bit) {
	(void)desc;
	(void)bit;
	return 0;
}
static inline void vma_desc_set_anonymous(struct vm_area_desc* desc) {
	(void)desc;
}
static inline void mmap_action_remap_full(struct vm_area_desc* desc, unsigned long pgoff) {
	(void)desc;
	(void)pgoff;
}

int valid_phys_addr_range(phys_addr_t addr, size_t count);
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size);
int range_is_allowed(unsigned long pgoff, unsigned long size);
int phys_mem_access_prot_allowed(struct file* file, unsigned long pfn,
								 unsigned long size, pgprot_t* vma_prot);
unsigned long mm_get_unmapped_area(struct file* file, unsigned long addr,
								   unsigned long len, unsigned long pgoff,
								   unsigned long flags);

/* /dev/kmsg -- mainline declares kmsg_fops in include/linux/printk.h */
extern const struct file_operations kmsg_fops;

#endif /* __LINUX_MM_H__ */
