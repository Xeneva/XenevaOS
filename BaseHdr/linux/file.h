#ifndef __DCL_STUB_BaseHdr_linux_file_h__
#define __DCL_STUB_BaseHdr_linux_file_h__

/*
 * DCL <linux/file.h> -- the struct file helpers that used to live only in
 * this scaffolding.
 *
 * mkh.sh created the original so clang would stop reporting a missing include
 * and start reporting what was *used* from it, which is exactly what
 * happened: virtio_console.c's port_fops_open() calls nonseekable_open(), and
 * the include that resolves it upstream is <linux/fs.h> -- the one header DCL
 * does not edit. So what this header owes is spelled here, in mainline's own
 * home for it: mainline splits fs/open.c's file helpers out to a header
 * rather than making every caller reach through fs.h, and DCL has one more
 * reason than mainline does to want that split.
 *
 * <linux/cdev.h> includes this file, which is how a character-device driver
 * reaches it: port_fops_open() is a .cdev open, and a driver should not have
 * to name a second header for a helper its first one already owes it.
 * It is safe to include anywhere -- it pulls <linux/fs.h>, <linux/fcntl.h>,
 * <linux/capability.h>, <linux/pid.h> and nothing else, and specifically not
 * <linux/kobject.h>, which is why the bodies live in
 * DCL/linux_cdev_shim.c rather than somewhere that already includes cdev.h.
 * None of the four can reach <linux/sched.h>, which is the property that
 * matters here: see the note at the bottom of fcntl.h.
 */

#include <linux/fs.h>

/*
 * <linux/fcntl.h> for the O_* flags, and it is the same situation as
 * nonseekable_open() below: mainline's <linux/fs.h> includes fcntl.h, and
 * DCL's fs.h is pinned so it includes nothing.
 *
 * This header rather than any other is where they land, because mainline's
 * file.h is where get_unused_fd_flags() is declared and drm_lease.c:532
 * calls it with `get_unused_fd_flags(cl->flags & (O_CLOEXEC | O_NONBLOCK))`
 * -- one line needing the function and its flag vocabulary from the same
 * place. drm_lease.c:494 uses the two flags alone, for the same reason.
 *
 * It is safe here, and not only because fcntl.h has stopped including
 * <linux/types.h> (see the note at the bottom of fcntl.h for why that
 * mattered to <Fs/vfs.h>): this header is included by linux_cdev_shim.c at
 * line 9, before <Fs/vfs.h> at line 11, and the flags are now a leaf -- two
 * headers of #defines between here and nothing.
 */
#include <linux/fcntl.h>

/*
 * <linux/pid.h>, for the same reason fcntl.h above is here: mainline's
 * <linux/fs.h> includes it (fs.h:20), and a drm source reaches it through
 * that chain without naming it.
 *
 * The consumer is drm_file.c:1030's `pid_task(pid, PIDTYPE_TGID)`, which
 * needs the enum constant and the function. None of the nine headers
 * drm_file.c does name pulls pid.h in -- module.h, file.h, pci.h, slab.h and
 * anon_inodes.h are all checked against mainline's own copies.
 *
 * Safe to reach from here for the reason fcntl.h is: pid.h includes nothing
 * at all, so it cannot reach <linux/sched.h> and its `current` macro.
 *
 * capable() is deliberately *not* here, though mainline's fs.h carries
 * capability.h as well. drm_ioctl.c needs capable() and does not include
 * this header, whereas it and both other consumers do include
 * <linux/pci.h>; so capability.h is carried once, there, rather than twice
 * from two different justifications.
 */
#include <linux/pid.h>

/*
 * FMODE_LSEEK / FMODE_PREAD / FMODE_PWRITE -- the bits nonseekable_open()
 * clears. mainline's values (include/linux/fs.h:112-116, 1<<2, 1<<3, 1<<4);
 * they are named by nothing else in this tree, so there was no second reading
 * of the word to reconcile against -- mem.c ORs a device's whole fmode into
 * the same field without naming a bit.
 */
#define FMODE_LSEEK	0x00000004
#define FMODE_PREAD	0x00000008
#define FMODE_PWRITE	0x00000010

/*
 * nonseekable_open() -- clear the seek bits on a file that has no .llseek.
 * mainline: fs/open.c:1560, declared in <linux/fs.h>, and its body is the
 * mask and a 0. Returning 0 is the whole of its contract (mainline has no
 * failure arm), and port_fops_open() ignores the value anyway. The inode
 * parameter is mainline's and unused there too.
 *
 * Body in DCL/linux_cdev_shim.c.
 */
int nonseekable_open(struct inode* inode, struct file* filp);

/*
 * get_file() -- take a reference.  mainline has this in <linux/fs.h> (the
 * header this file exists to stand beside without editing); it has to be
 * here because <linux/dma-buf.h> -- which includes this header at line 17 --
 * spells get_dma_buf() as an inline that calls it, so the declaration must
 * already be in scope for that parse.  DCL files carry no refcount; the
 * reference is the pointer, and nothing in the tree ever drops it.
 */
static inline void get_file(struct file* f)
{
	(void)f;
}

/*
 * fput() -- drop one reference to a struct file. mainline's body is
 * fs/file_table.c's delayed-teardown machinery; DCL carries no refcount for
 * it to decrement -- the pointer is the reference, the same trade get_file()
 * above makes -- so the body in DCL/linux_cdev_shim.c is the (void)file.
 * Declared here because mainline declares it here (include/linux/file.h:18),
 * and both call sites (drm_gem.c:1139, drm_atomic_uapi.c:1576) reach this
 * header.
 */
void fput(struct file* file);

#include <linux/cleanup.h>	/* __cleanup -- the fd_destructor attribute below */

/*
 * struct fd and the CLASS(fd, ...) plumbing -- mainline's fdget() result
 * type, carried here because drm_syncobj.c:710 uses it, and this header is
 * where mainline puts it (include/linux/file.h).
 *
 * There is no file-descriptor table in this kernel: no __fdget(), no fget(),
 * and nothing that maps an unsigned int to a struct file. So the constructor
 * answers an empty fd for every input, which is not a stub pretending to
 * work -- it is the correct answer, and drm_syncobj.c is written for it:
 * :710 `if (fd_empty(f)) return -EINVAL;` is the first thing that function
 * does after the declaration, so an fd-based syncobj import returns -EINVAL
 * and touches nothing behind it.
 *
 * fd_destructor is empty for the same reason fd_constructor returns empty:
 * there is no reference to drop, because there was never one to take. It is
 * spelled by name rather than built with DEFINE_FREE(), because CLASS()'s
 * expansion looks for `name##_destructor` specifically -- see the note on
 * CLASS() in <linux/cleanup.h>.
 *
 * The mainline struct also carries `flags` (FMODE_PATH and friends), kept
 * here so a later fdget() could be dropped in without the type changing.
 */
struct fd {
	struct file* file;
	unsigned int flags;
};

#define fd_empty(f)	((f).file == NULL)
#define fd_file(f)	((f).file)
#define fd_valid(f)	(fd_file(f) != NULL)

#define fd_t		struct fd

/*
 * Takes a void*, not a struct fd: the compiler calls a cleanup function with
 * the *address* of the variable it is attached to, and passing the struct by
 * value produced "'cleanup' function 'fd_destructor' parameter has type
 * 'struct fd' which is incompatible with type 'struct fd *'". mainline's
 * DEFINE_FREE() spells the same thing the same way -- `_type _T = *(_type *)p`.
 */
static inline void fd_destructor(void* p)
{
	(void)p;
}

static inline struct fd fd_constructor(unsigned int fd)
{
	(void)fd;
	return (struct fd){ 0 };
}

/*
 * ── the fd table: mainline's file.h owns it, this kernel does not ───────
 *
 * An fd is an index into current->files; get_unused_fd_flags(),
 * fd_install() and put_unused_fd() are the three moves mainline's VFS makes
 * a struct file with. DCL has neither a files_struct nor an fget() that
 * could turn an index back into a file (see the struct fd note above), so
 * the bodies in DCL/linux_cdev_shim.c are the honest answers: the allocation
 * answers -ENOSYS -- no table, not a full one -- and the two void calls have
 * nothing to install into or give back. That is also what makes them safe to
 * leave empty: every caller (drm_lease.c:532, drm_syncobj.c:669,
 * drm_atomic_uapi.c:1400, drm_prime.c:516) tests `fd < 0` and unwinds before
 * its install/put arm.
 *
 * file_clone_open() is the same missing VFS one step earlier: mainline keeps
 * it in <linux/fs.h> (:2527) as a dentry_open() of the source file, and there
 * is no dentry tree here to open through. Its one caller (drm_lease.c:551)
 * checks the ERR_PTR the body in linux_cdev_shim.c returns.
 */
int get_unused_fd_flags(unsigned flags);
void put_unused_fd(unsigned int fd);
void fd_install(unsigned int fd, struct file* file);
struct file* file_clone_open(struct file* file);

#endif /* __DCL_STUB_BaseHdr_linux_file_h__ */
