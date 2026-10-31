#ifndef __LINUX_SHMEM_FS_H__
#define __LINUX_SHMEM_FS_H__

#include <linux/kernel.h>	/* gfp_t, pgoff_t, loff_t */
#include <linux/mm.h>		/* vma_flags_t -- shmem_file_setup()'s flag word */

/*
 * DCL <linux/shmem_fs.h> -- the two shmem entry points mem.c's /dev/zero mmap
 * arms call. Xeneva has no tmpfs-backed shmem object yet and devfs does not
 * route ->mmap, so the shims answer "unsupported"; they are declared here so
 * the vendored source compiles unchanged. Bodies: DCL/linux_mm_shim.c.
 */

#include <linux/mm.h>		/* struct vm_area_desc */

int shmem_zero_setup_desc(struct vm_area_desc* desc);

/*
 * struct address_space -- mainline keeps this in <linux/fs.h>, which this
 * tree must not edit.  What the drm core does to a mapping is read or set
 * its gfp mask and flip the unevictable bit (drm_gem.c:691-716,
 * drm_gem_shmem_helper.c:88), so those fields and the helpers over them are
 * the whole of it; the shmem stubs ignore both.
 */
struct address_space {
	unsigned long flags;
	gfp_t gfp_mask;
};

static inline void mapping_set_unevictable(struct address_space *mapping)
{
	mapping->flags |= 1UL;
}

static inline void mapping_clear_unevictable(struct address_space *mapping)
{
	mapping->flags &= ~1UL;
}

static inline gfp_t mapping_gfp_mask(const struct address_space *mapping)
{
	return mapping->gfp_mask;
}

static inline void mapping_set_gfp_mask(struct address_space *mapping,
					gfp_t mask)
{
	mapping->gfp_mask = mask;
}

static inline gfp_t mapping_gfp_constraint(const struct address_space *mapping,
					   gfp_t constraint)
{
	return mapping->gfp_mask & constraint;
}

struct vfsmount;
struct folio;
struct page;

/*
 * The shmem entry points drm_gem.c and drm_gem_shmem_helper.c call.  They
 * are declared so the vendored source compiles unchanged; the bodies
 * (DCL/drm_stubs.c) answer ERR_PTR(-ENOSYS), which drm_gem_object_init()
 * and the read paths both turn into a checked failure.  Stage 2 replaces
 * them with a real shmem-backed GEM allocator.
 */
struct file *shmem_file_setup(const char *name, loff_t size,
			      vma_flags_t vm_flags);
struct file *shmem_file_setup_with_mnt(struct vfsmount *mnt, const char *name,
				       loff_t size, vma_flags_t vm_flags);
struct folio *shmem_read_folio_gfp(struct address_space *mapping, pgoff_t index,
				   gfp_t gfp);
struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
					 pgoff_t index, gfp_t gfp);

unsigned long shmem_get_unmapped_area(struct file* file, unsigned long addr,
									  unsigned long len, unsigned long pgoff,
									  unsigned long flags);

#endif /* __LINUX_SHMEM_FS_H__ */
