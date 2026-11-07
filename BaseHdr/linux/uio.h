#ifndef __LINUX_UIO_H__
#define __LINUX_UIO_H__

/*
 * DCL <linux/uio.h> -- iterator + kiocb bits mem.c's read_iter/write_iter
 * arms touch (read_iter_zero peeks at iocb->ki_flags, write_iter_null walks
 * the iov_iter).
 *
 * mainline defines struct kiocb in include/linux/fs.h; DCL's fs.h is a
 * frozen ABI pin file (it only forward-declares kiocb), so the layout this
 * target compiles against lives here instead -- same rule: never edit the
 * pinned file, add beside it.
 */

#include <stddef.h>
#include <stdint.h>
#include <linux/fs.h>

#define IOCB_NOWAIT 0x00000001

struct kiocb {
	struct file* ki_filp;
	long long ki_pos;
	void* private;
	int ki_flags;
	unsigned int ki_hint;
	unsigned int ki_ioprio;
};

/*
 * DCL's iov_iter: a single buffer. mainline's is a tagged union over
 * iovec/bvec/kvec/xarray iterators; none of the drivers/char code mem.c
 * brings up inspects the fields -- it only calls iov_iter_count() /
 * iov_iter_advance() / iov_iter_zero() -- so the union collapses to
 * {buf, off, count} here. Implementations live in DCL/linux_mm_shim.c.
 */
#define ITER_DEST 0
#define ITER_SOURCE 1

struct iov_iter {
	void* buf;
	size_t off;
	size_t count;
	int dir;
};

static inline struct iov_iter iov_iter_init(int dir, void* buf, size_t len) {
	struct iov_iter i;
	i.buf = buf;
	i.off = 0;
	i.count = len;
	i.dir = dir;
	return i;
}

size_t iov_iter_count(const struct iov_iter* i);
void iov_iter_advance(struct iov_iter* i, size_t bytes);
size_t iov_iter_zero(size_t bytes, struct iov_iter* i);

#endif /* __LINUX_UIO_H__ */
