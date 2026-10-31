#ifndef __LINUX_POLL_H__
#define __LINUX_POLL_H__

/*
 * DCL <linux/poll.h> -- the epoll bits tty_poll() and n_tty poll through.
 *
 * The masks are mainline's values so a byte handed to userspace means the
 * same thing it would on Linux.
 *
 * poll_wait() is a deliberate no-op, and it is worth being explicit about
 * why: it exists so that the *next* wait (a state change or a timeout) wakes
 * a blocked select(). DCL has no sleeping scheduler to block in, so a
 * blocking poll would never wake through this path -- it re-reads state on
 * the caller's own timeout instead, which is exactly what Xeneva's select
 * does (it takes a timeout argument). The consequence is honest and local:
 * select() on a tty with no data waits out its timeout rather than returning
 * early when a byte arrives mid-wait. Real wake-up registration arrives with
 * stage 5's wait-queue work.
 *
 * Bodies in DCL/linux_irq_shim.c.
 */

struct file;
struct wait_queue_head;
typedef struct poll_table_struct poll_table;

#define EPOLLIN    0x00000001
#define EPOLLPRI   0x00000002
#define EPOLLOUT   0x00000004
#define EPOLLERR   0x00000008
#define EPOLLHUP   0x00000010
#define EPOLLNVAL  0x00000020
#define EPOLLRDNORM 0x00000040
#define EPOLLWRNORM 0x00000100
#define EPOLLRDHUP 0x00002000

#define POLLIN     0x0001
#define POLLPRI    0x0002
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

/*
 * POLL_OUT / POLL_IN -- the *signal band* half of kill_fasync(), not the
 * poll(2) event. virtio_console.c:1319 does
 *
 *     kill_fasync(&port->fasync, SIGIO, POLL_OUT);
 *
 * where mainline takes POLL_OUT from <linux/poll.h> (it is the same word as
 * POLLOUT, 0x0004) and SIGIO from <linux/signal.h>, reached through
 * <linux/fs.h>. DCL's fs.h is pinned and carries neither, and there is no
 * <linux/signal.h> in this tree -- so both names live here, beside the
 * kill_fasync() declaration, because this header is where the file's
 * notification surface already sits.
 *
 * SIGIO is 29, the generic-ABI value arm64 uses (asm-generic/signal.h:30);
 * SIGRTMIN-style numbering is not in play because nothing raises it. DCL
 * delivers no signals (signal_pending() is 0, kernel.h), so both names are
 * ABI shape: kill_fasync() is a no-op and the band is never read.
 */
#define POLL_IN     0x0001
#define POLL_OUT    0x0004
#define SIGIO       29

#define DEFAULT_POLLMASK (EPOLLIN | EPOLLOUT | EPOLLRDNORM | EPOLLWRNORM)

void poll_wait(struct file* file, struct wait_queue_head* wait_address,
			poll_table* p);

/*
 * fasync_helper() and kill_fasync() -- the two fcntl.c entry points the
 * driver's .fasync fops and its notify paths call:
 *
 *     .fasync = virtio_console_fasync     ->  fasync_helper(fd, filp, on,
 *                                              &port->fasync)   :1079
 *     kill_fasync(&port->fasync, SIGIO, POLL_OUT)               :1319
 *
 * mainline declares both in <linux/fs.h>, which is the one header DCL does
 * not touch, and neither belongs in <linux/poll.h> upstream -- but POLL_IN /
 * POLL_OUT and SIGIO above are already here for the kill_fasync() call, and
 * this header is where the file's notification surface sits in DCL. Keeping
 * the three together beats scattering them: a call site has to see the band,
 * the signal and the function that takes them, and it reaches this header
 * whether or not it can reach fs.h.
 *
 * `struct fasync_struct` is complete in mainline's fs.h; here it is only ever
 * named behind a `&port->fasync` and passed on, so a forward declaration is
 * the whole of its type a caller can use -- port->fasync itself is a member
 * of a struct the driver declares in mainline's shape, where the member is a
 * pointer.
 *
 * Bodies in DCL/linux_cdev_shim.c. fasync_helper() returns 0, which is what
 * it returns when the entry was inserted or removed cleanly; DCL delivers no
 * signals (SIGIO is defined above as ABI shape), so nothing can observe the
 * difference between a registered fasync and an unregistered one. That is
 * not a stub pretending to work -- it is the correct answer for a kernel with
 * no signal delivery, and kill_fasync() being a no-op is its necessary
 * corollary.
 */
struct fasync_struct;

int fasync_helper(int fd, struct file* filp, int on,
			struct fasync_struct** fapp);
void kill_fasync(struct fasync_struct** fp, int sig, int band);

#endif /* __LINUX_POLL_H__ */
