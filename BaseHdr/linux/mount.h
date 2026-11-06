/*
 * DCL mount.h -- the pseudo-mount surface drm_drv.c uses for its internal
 * inode pool (drm_fs_inode_new/free), plus the unmap helper.
 *
 * simple_pin_fs() serves one shared fake vfsmount (there is only the one
 * drm pseudo-fs on Xeneva); alloc_anon_inode()/iput() are provided by
 * linux/pseudo_fs.h's side of the split, and the unmap helper below is the
 * one that must not type-check its operand.
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

#include <linux/fs.h>

/*
 * Two mainline headers, neither of which mainline's own mount.h carries. They
 * come in here because this file is where drm_drv.c already reaches the
 * mainline fs.h surface that DCL's pinned <linux/fs.h> cannot hold --
 * simple_pin_fs(), simple_release_fs() and unmap_mapping_range() below are all
 * mainline fs.h functions, and the pinned fs.h is an ABI blob of struct
 * layouts with no room to declare them.
 *
 * drm_drv.c names this header directly (drm_drv.c:36) and that is the only
 * route into it in the whole drm set: drm_device.h includes it too, but
 * behind `#ifdef CONFIG_TRANSPARENT_HUGEPAGE` (drm_device.h:5), which is off.
 * So a symbol put here is visible to drm_drv.c and to nobody else, which is
 * why the two below are here and O_CLOEXEC is not.
 *
 * <linux/kernel.h> comes first and is not a mainline mount.h include either --
 * it is here for what this file now defines. noop_llseek() returns loff_t and
 * kdev_t.h's helpers take dev_t, and both types live in kernel.h; fs.h carries
 * neither (it defines the file and inode *layouts* and nothing else), so
 * without this include the header depends on its includer having pulled
 * kernel.h already, which is true of mm.h and untrue of drm_drv.c.
 *
 *   <linux/kdev_t.h>  MINORBITS, for DRM_EXTENDED_MINOR_LIMIT (drm_drv.c:141).
 *                     mainline reaches it because fs.h includes kdev_t.h; the
 *                     pinned fs.h defines MKDEV/MAJOR/MINOR with the bit count
 *                     spelled inline and never names the macro.
 *   <linux/sizes.h>   SZ_8, for the bits-to-bytes pitch rounding in
 *                     drm_dumb_buffers.c:180.
 *
 * noop_llseek() comes from further down this file rather than <linux/mm.h>,
 * where it used to be: mm.h includes mount.h (for the VFS block below), so
 * including mm.h back would be a cycle.
 */
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/sizes.h>

struct file_system_type;

struct vfsmount {
	struct super_block *mnt_sb;
};

int simple_pin_fs(struct file_system_type *type, struct vfsmount **mnt, int *count);
void simple_release_fs(struct vfsmount **mnt, int *count);

/* ── VFS helpers mainline pulls through its own fs.h ───────────────────── */

/*
 * These five lived in <linux/mm.h>, which is where drivers/char ports reach
 * them, and moved here for the reason the three includes above give: mainline
 * has them in fs.h, DCL's fs.h is pinned and cannot, and mount.h is the file
 * that has taken that surface over. mm.h includes this header, so a port that
 * reaches them through mm.h as before is unaffected.
 */
#define file_inode(f)   ((struct inode*)((f)->f_inode))
#define iminor(inode)   (MINOR((inode)->i_rdev))
#define inode_lock(i)   ((void)(i))
#define inode_unlock(i) ((void)(i))

/*
 * noop_llseek() is mainline's "seeking does nothing and the position moves
 * anyway", which is what a stream-like node wants. Returning @offset (rather
 * than the current position) is what mainline's does for a regular file, and
 * every caller here is a character device whose f_pos is never read back.
 */
static inline loff_t noop_llseek(struct file* f, loff_t offset, int whence) {
	(void)f;
	(void)whence;
	return offset;
}

/*
 * unmap_mapping_range() is a macro so drm_drv.c:500's whole call --
 * holebegin and holelen included -- disappears: the mapping operand there
 * is `dev->anon_inode->i_mapping`, a member the pinned <linux/fs.h> struct
 * inode does not carry, so the expansion must not touch any of the four
 * arguments.  The anon inode has no mappings to unmap.
 */
#define unmap_mapping_range(mapping, holebegin, holelen, even_cows)	\
	do { } while (0)

#define mntget(m)		(m)
#define mntput(m)		do { (void)(m); } while (0)

#endif /* _LINUX_MOUNT_H */
