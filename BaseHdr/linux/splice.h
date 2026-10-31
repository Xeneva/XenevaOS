#ifndef __LINUX_SPLICE_H__
#define __LINUX_SPLICE_H__

/*
 * DCL <linux/splice.h> -- enough of the splice graph for mem.c's
 * null/zero/full fops (.splice_write -> splice_from_pipe, .splice_read ->
 * copy_splice_read). Xeneva's devfs bridge does not route splice yet, so
 * these are ABI-shaped definitions that keep the fops initializers legal;
 * the declarations live here so the vendored source compiles unchanged.
 */

#include <stddef.h>
#include <linux/kernel.h>	/* loff_t, ssize_t, gfp_t -- splice_from_pipe()'s
				 * own parameter types. They used to come in
				 * sideways through whatever the includer had
				 * pulled first (the driver reaches splice.h from
				 * cdev.h -> kobject.h -> kernel.h), which is not
				 * an include order a header may rely on: this
				 * file now compiles on its own as
				 * DCL/linux_splice_shim.c proved by failing to. */
#include <linux/fs.h>		/* struct pipe_inode_info (fwd), struct file */

struct pipe_buffer {
	void* page;
	unsigned int offset, len;
	const void* ops;
	unsigned int flags;
	unsigned long private;
};

struct splice_desc {
	size_t len;			/* mem.c's pipe_to_null returns sd->len */
	unsigned int total_len;
	unsigned int flags;
	unsigned int pos;
	struct pipe_inode_info* pipe;
	/*
	 * mainline names this `union { void *data; void (*cleanup)(...); } u;`
	 * and the ported splice actor reads it as sd->u.data -- virtio_console.c:912
	 * initialises `.u.data = &sgl` and pipe_to_sg() at :861 casts it back.
	 * The union has only the one arm DCL can name: nothing here has a
	 * cleanup callback to hang off the other, and a member that no source
	 * can reach is not worth carrying.
	 *
	 * Nothing in this tree reads the old flat `sd->data` spelling (checked
	 * across Vendored/ and DCL/ before the rename), so this is a rename and
	 * not a widening.
	 */
	union {
		void* data;
	} u;
	struct file* file;
};

/*
 * The pipe side of splice. mainline has these as static inlines over
 * pipe->head/pipe->tail (linux/pipe_fs_i.h); DCL's struct pipe_inode_info is
 * a forward declaration only -- see the include at the top -- so they are
 * calls, bodies in DCL/linux_splice_shim.c.
 *
 * All four are unreachable today: Xeneva's devfs bridge does not route
 * splice, so no port's .splice_write is ever entered (the header's own note
 * above). They exist so the vendored source compiles, and their bodies say
 * the same thing the rest of the splice layer says -- a list that is empty,
 * a lock nobody holds, a buffer nobody took.
 */
void pipe_lock(struct pipe_inode_info* pipe);
void pipe_unlock(struct pipe_inode_info* pipe);
unsigned int pipe_is_empty(struct pipe_inode_info* pipe);
unsigned int pipe_buf_usage(struct pipe_inode_info* pipe);

/*
 * pipe_buf_try_steal() -- take ownership of a buffer's page so the caller can
 * move it without copying. DCL's returns false always, which sends the one
 * caller (virtio_console.c:866) down its copy arm: alloc_page(), memcpy,
 * sg_set_page(). Returning true instead would claim ownership of a page the
 * pipe still points at, and there is no pipe here with a page to lose -- but
 * false is the answer that is true even if splice is wired up one day.
 */
bool pipe_buf_try_steal(struct pipe_inode_info* pipe,
			struct pipe_buffer* buf);

/*
 * splice_pipe_actor -- the callback __splice_from_pipe() drives, one buffer
 * at a time. Declared before its first use: __splice_from_pipe() below takes
 * one as a parameter, and a parameter of undeclared function-pointer type
 * would be a different type from the typedef that follows it.
 */
typedef int (*splice_pipe_actor)(struct pipe_inode_info* pipe,
								 struct pipe_buffer* buf,
								 struct splice_desc* sd);

/*
 * __splice_from_pipe() -- push each buffered page through `actor`.
 *
 * mainline's returns the actor's accumulated byte count and is the engine
 * behind splice_from_pipe(), which this header declares just after. The DCL
 * body is the loop's *shape* without a loop behind it, for the same reason
 * every other entry point here is stubbed: no pipe ever has content to
 * iterate, because devfs does not route splice.
 */
long __splice_from_pipe(struct pipe_inode_info* pipe,
			struct splice_desc* sd, splice_pipe_actor actor);

ssize_t splice_from_pipe(struct pipe_inode_info* pipe, struct file* out,
						 loff_t* ppos, size_t len, unsigned int flags,
						 splice_pipe_actor actor);

ssize_t copy_splice_read(struct file* in, loff_t* ppos,
						 struct pipe_inode_info* pipe, size_t len,
						 unsigned int flags);

#endif /* __LINUX_SPLICE_H__ */
