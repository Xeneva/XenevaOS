/*
 * DCL pseudo_fs.h -- drm_drv.c's internal "drm" filesystem, faked.
 *
 * init_pseudo() returns a non-NULL dentry so drm_fs_init_fs_context()
 * succeeds; file_system_type is defined here because mainline keeps it in
 * <linux/fs.h>, which this tree must not edit.  alloc_anon_inode() hands
 * back a zeroed struct inode (never ERR), iput() is a deliberate no-op --
 * struct inode has no i_count on this build and drm allocates a handful of
 * them per device lifetime.  kill_anon_super() only ever runs on unmount,
 * which Xeneva never does for this fs.
 */
#ifndef _LINUX_PSEUDO_FS_H
#define _LINUX_PSEUDO_FS_H

#include <linux/fs.h>
#include <linux/fs_context.h>

struct dentry;
struct module;
struct file_system_type {
	const char *name;
	struct module *owner;
	int (*init_fs_context)(struct fs_context *fc);
	void (*kill_sb)(struct super_block *sb);
};

struct dentry *init_pseudo(struct fs_context *fc, unsigned long magic);
void kill_anon_super(struct super_block *sb);

struct inode *alloc_anon_inode(struct super_block *sb);
void iput(struct inode *inode);

#endif /* _LINUX_PSEUDO_FS_H */
