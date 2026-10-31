/*
 * DCL/tty_shim.c -- stage 2 of milestone 3: the flip buffer.
 *
 * Stage 2 vendors drivers/tty/tty_buffer.c untouched (DCL/tty_buffer.c, md5
 * against v7.2) and this file supplies the two things it cannot supply for
 * itself, plus a boot self-test.
 *
 * What this stage proves is worth stating, because "it compiled" is not much
 * of a proof for a piece of code whose entire job is to hand bytes from an
 * interrupt handler to a line discipline:
 *
 *   - allocation rounds to a 256-byte stride and accounts for it (the
 *     mem_used/freed accounting has to balance or tty_buffer_free_all() warns);
 *   - the used/commit/read protocol keeps bytes invisible to the consumer
 *     until tty_flip_buffer_push(), and visible afterwards;
 *   - the flag byte lives at `data + size` and comes back out intact;
 *   - queue_work() runs flush_to_ldisc() inline, so there is no window in
 *     which committed data has nobody to deliver it.
 *
 * The self-test installs its own tty_port_client_ops rather than mainline's
 * tty_port_default_client_ops. That is not a shortcut: the default ops
 * forwards to tty_ldisc_receive_buf() through port->tty->ldisc, and at stage 2
 * there is no tty and no ldisc -- the default would dereference NULL for a
 * reason that has nothing to do with buffering. The real default arrives with
 * tty_port.c / tty_io.c (stage 5), where port->tty exists.
 *
 * system_dfl_wq is the other symbol: tty_buffer.c:66 picks it when a port has
 * no driver-specific flip queue (`flip_wq ?: system_dfl_wq`). DCL has one
 * workqueue and it runs work inline (see <linux/workqueue.h>), so this object
 * exists for the choice to be well-formed, not to be scheduled on.
 *
 * system_percpu_wq is the same object one line down, for two drm call sites
 * that name a queue explicitly (drm_probe_helper.c:629,
 * drm_self_refresh_helper.c:221). Both hand it to mod_delayed_work(), which
 * ignores it for the reason queue_delayed_work() ignores it.
 *
 * system_long_wq is a third name for it, asked for by drm_writeback.c:570's
 * cleanup_work. mainline's split between the three is about CPU affinity and
 * how long a worker may be held; on a runner that executes work inline there
 * is one queue and naming it three ways must not imply three.
 *
 * All three live here rather than in a workqueue shim of their own because
 * the three pointers are that one fact -- DCL has a single queue, everything
 * system_*wq names is it -- and splitting them across files would suggest a
 * distinction that does not exist.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/minmax.h>
#include <linux/tty.h>
#include <linux/tty_buffer.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/termios.h>
#include <linux/kref.h>
#include <linux/atomic.h>
#include <linux/llist.h>
#include <linux/lockdep.h>
#include <linux/err.h>
#include "tty.h"

/*
 * Declared as a pointer in <linux/workqueue.h> (see the note there: `E1 ?: E2`
 * will not decay a struct lvalue, so the object spelling does not parse at
 * tty_buffer.c:66). The object it points at is file-scope static -- nothing
 * else needs its address, only its existence.
 */
static struct workqueue_struct dcl_system_dfl_wq = { "system_dfl" };
struct workqueue_struct* system_dfl_wq = &dcl_system_dfl_wq;

/*
 * Same object, deliberately. Pointing all three at dcl_system_dfl_wq keeps
 * `queue_work(a) then queue_work(b)` ordered the same way whichever name a
 * caller used, which is the only ordering this inline runner has to get
 * right.
 */
struct workqueue_struct* system_percpu_wq = &dcl_system_dfl_wq;
struct workqueue_struct* system_long_wq = &dcl_system_dfl_wq;

/* ---------------------------------------------------------------------- */
/* the self-test                                                          */
/* ---------------------------------------------------------------------- */

static unsigned int dcl_tb_received;
static unsigned char dcl_tb_last_char;
static unsigned char dcl_tb_last_flag;
static int dcl_tb_saw_flag_array;

/*
 * A receive_buf() that counts instead of forwarding. Returning `count` means
 * "consumed": flush_to_ldisc() advances head->read by the return value and
 * memsets that many bytes to 0. Returning 0 would stall the loop (line 506
 * breaks on it) and leave the buffer permanently full, which is exactly the
 * failure a real n_tty can produce when its input queue is full -- here it
 * would just be a bug in the test.
 */
static size_t dcl_tb_receive_buf(struct tty_port* port, const u8* cp,
				 const u8* fp, size_t count)
{
	size_t i;

	(void)port;
	for (i = 0; i < count; i++) {
		dcl_tb_last_char = cp[i];
		if (fp) {
			dcl_tb_last_flag = fp[i];
			dcl_tb_saw_flag_array = 1;
		} else {
			dcl_tb_saw_flag_array = 0;
		}
		dcl_tb_received++;
	}
	return count;
}

static const struct tty_port_client_operations dcl_tb_client_ops = {
	.receive_buf = dcl_tb_receive_buf,
	.lookahead_buf = NULL,
	.write_wakeup = NULL,
};

/*
 * DclTtyBufferTestRun -- stage 2's runtime gate, called from init.c after
 * DclPrimTestRun() so the log reads in milestone order.
 */
void DclTtyBufferTestRun(void)
{
	static struct tty_port port;
	const u8 msg[] = "flip";
	int ok = 0, fail = 0;
	size_t n;

#define CHECK(cond) do { if (cond) ok++; else fail++; } while (0)

	memset(&port, 0, sizeof(port));
	tty_buffer_init(&port);
	port.client_ops = &dcl_tb_client_ops;

	/* 1. tty_buffer_init() gives a working empty buffer head */
	CHECK(port.buf.head == &port.buf.sentinel);
	CHECK(port.buf.tail == &port.buf.sentinel);
	CHECK(port.buf.mem_limit > 0);
	CHECK(tty_buffer_space_avail(&port) == (unsigned int)port.buf.mem_limit);

	/* 2. a limit below MIN_TTYB_SIZE is refused, not accepted.
	 *    (4096 is the number mainline's own default is built from; the
	 *    constant TTY_BUFFER_PAGE is private to tty_buffer.c, so the
	 *    test names the value rather than reaching into the file.) */
	CHECK(tty_buffer_set_limit(&port, 128) == -EINVAL);
	CHECK(tty_buffer_set_limit(&port, 4096) == 0);

	/* 3. bytes go in */
	n = tty_insert_flip_string(&port, msg, sizeof(msg) - 1);
	CHECK(n == sizeof(msg) - 1);
	CHECK(port.buf.tail != &port.buf.sentinel);
	CHECK(port.buf.tail->used == sizeof(msg) - 1);

	/* 4. ...and are not delivered until the push. This is the property
	 *    that lets a driver write a burst of characters without the ldisc
	 *    seeing a half-written one: `commit` stays 0 until
	 *    tty_flip_buffer_push(). */
	CHECK(dcl_tb_received == 0);
	CHECK(port.buf.tail->commit == 0);

	tty_flip_buffer_push(&port);

	/* 5. the push delivers, inline, and every byte arrives */
	CHECK(dcl_tb_received == sizeof(msg) - 1);
	CHECK(dcl_tb_last_char == 'p');		/* "flip" ends in p */
	CHECK(port.buf.head->read == sizeof(msg) - 1);
	CHECK(port.buf.head->commit == port.buf.tail->used);

	/* 6. a non-NORMAL flag forces a flags-capable buffer, and the flag
	 *    byte comes back at data + size. If flag_buf_ptr()'s stride were
	 *    wrong this reads a character as a flag and TTY_BREAK would not
	 *    survive the round trip. */
	n = tty_insert_flip_char(&port, 'x', TTY_BREAK);
	CHECK(n == 1);
	tty_flip_buffer_push(&port);
	CHECK(dcl_tb_received == sizeof(msg));	/* one more byte */
	CHECK(dcl_tb_saw_flag_array == 1);
	CHECK(dcl_tb_last_flag == TTY_BREAK);
	CHECK(dcl_tb_last_char == 'x');

	/* 7. the fast path: a NORMAL flag into a non-flags buffer needs no
	 *    reallocation and still counts as one byte delivered. */
	n = tty_insert_flip_char(&port, 'y', TTY_NORMAL);
	CHECK(n == 1);
	tty_flip_buffer_push(&port);
	CHECK(dcl_tb_received == sizeof(msg) + 1);

	/* 8. accounting balances, or tty_buffer_free_all() warns at us. */
	tty_buffer_free_all(&port);
	CHECK(port.buf.head == &port.buf.sentinel);
	CHECK(port.buf.tail == &port.buf.sentinel);
	CHECK(atomic_read(&port.buf.mem_used) == 0);

#undef CHECK

	UARTDebugOut("dcl tty_buffer: %d ok, %d failed\r\n", ok, fail);
}
