/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PATH_H
#define _LINUX_PATH_H

struct dentry;
struct vfsmount;

struct path {
	struct vfsmount *mnt;
	struct dentry *dentry;
};

extern void path_get(const struct path *);
extern void path_put(const struct path *);

/*
 * d_path(path, buf, buflen) -- declared here rather than in mainline's home,
 * <linux/dcache.h>, and the relocation is a consequence of this tree's header
 * set rather than a preference.
 *
 * The only caller is lib/seq_buf.c:322, whose include list is bug.h, err.h,
 * export.h, hex.h, minmax.h, printk.h, seq_buf.h, seq_file.h, sprintf.h,
 * string.h, types.h and uaccess.h -- none of which is dcache.h. <linux/seq_buf.h>
 * includes *this* header (for seq_buf_path()'s parameter), so a declaration
 * here is the one that reaches that caller. Mainline needs no such care: its
 * dcache.h is reached from mm.h, which is in every translation unit.
 *
 * A declaration and not a definition: the body is in DCL/linux_cdev_shim.c,
 * beside the other fs-layer fops helpers, because it needs a struct file and
 * this header deliberately forward-declares rather than defines dentries.
 *
 * Declared in DCL/linux_cdev_shim.c too, where the definition is, so the two
 * cannot drift apart -- an identical redeclaration of a function is legal in
 * C and this is the one place it is done on purpose.
 */
char *d_path(const struct path *path, char *buf, int buflen);

static inline int path_equal(const struct path *path1, const struct path *path2)
{
	return path1->mnt == path2->mnt && path1->dentry == path2->dentry;
}

/*
 * Cleanup macro for use with __free(path_put). Avoids dereference and
 * copying @path unlike DEFINE_FREE(). path_put() will handle the empty
 * path correct just ensure @path is initialized:
 *
 * struct path path __free(path_put) = {};
 */
#define __free_path_put path_put

#endif  /* _LINUX_PATH_H */
