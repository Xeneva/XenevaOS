/**
 * DCL/linux_splice_shim.c -- the pipe half of the splice ABI.
 *
 * <linux/splice.h> names this file as the home of these six, and they are
 * together because the reason they are all no-ops is one fact about DCL's
 * boot rather than six decisions: Xeneva's devfs bridge does not route splice,
 * so no port's .splice_write is ever entered and nothing can hold, take or
 * release a pipe buffer.
 *
 * That is worth stating plainly rather than burying, because two of them look
 * like they could be wrong for longer than that:
 *
 *   pipe_buf_try_steal -> false  sends virtio_console.c:866 down its copy arm
 *                              (alloc_page + memcpy + sg_set_page), which is
 *                              the arm that stays correct if splice is ever
 *                              wired up -- stealing would move a page the pipe
 *                              still points at.
 *   pipe_is_empty     -> 1      is the true answer for a pipe nobody can fill.
 *
 * splice_from_pipe() and copy_splice_read(), the two entry points a
 * file_operations table actually names, are in DCL/linux_mm_shim.c with the
 * rest of mem.c's fops backing; this file is the layer below them.
 */

#include <stddef.h>
#include <linux/splice.h>

/*
 * A pipe nobody can contend for. mainline's pair take pipe->mutex; DCL's
 * struct pipe_inode_info is a forward declaration only (see the include at
 * the top of splice.h), so there is no mutex to take even if there were a
 * holder to contend with.
 */
void pipe_lock(struct pipe_inode_info* pipe) {
	(void)pipe;
}

void pipe_unlock(struct pipe_inode_info* pipe) {
	(void)pipe;
}

/*
 * pipe_is_empty / pipe_buf_usage -- how many buffers are queued and how many
 * pages they hold. Both are 0/empty for the same reason, and a caller
 * looping on "while not empty" therefore terminates on its first test rather
 * than on a count it can never observe change.
 */
unsigned int pipe_is_empty(struct pipe_inode_info* pipe) {
	(void)pipe;
	return 1;
}

unsigned int pipe_buf_usage(struct pipe_inode_info* pipe) {
	(void)pipe;
	return 0;
}

/*
 * pipe_buf_try_steal() -- take ownership of a buffer's page so the caller
 * can move it without copying. false always; see this file's header comment
 * for why false is the answer that outlives the one that cannot be reached.
 */
bool pipe_buf_try_steal(struct pipe_inode_info* pipe,
						struct pipe_buffer* buf) {
	(void)pipe;
	(void)buf;
	return false;
}

/*
 * __splice_from_pipe() -- push each buffered page through `actor` and return
 * the bytes it accumulated.
 *
 * The loop has no body to run: struct pipe_inode_info is incomplete here, so
 * there are no head/tail indices to walk, and even if there were the list
 * would be empty (pipe_is_empty() above). What this returns is therefore 0
 * -- "the actor was driven over no buffers" -- rather than an error, because
 * an error would be read by splice_from_pipe()'s caller as a failure of a
 * splice that DCL declines to support, which that caller already gets from
 * splice_from_pipe() itself.
 */
long __splice_from_pipe(struct pipe_inode_info* pipe,
						struct splice_desc* sd, splice_pipe_actor actor) {
	(void)pipe;
	(void)sd;
	(void)actor;
	return 0;
}
