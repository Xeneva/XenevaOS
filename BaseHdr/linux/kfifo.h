#ifndef __LINUX_KFIFO_H__
#define __LINUX_KFIFO_H__

/*
 * DCL <linux/kfifo.h> -- the byte ring behind a tty port's transmit buffer.
 *
 * serial_core.c keeps everything waiting to go out the UART in one of these:
 * tty_port_alloc_xmit_buf() hands it a PAGE_SIZE buffer (serial_core.c:262),
 * uart_write() pushes bytes in, and the driver's start_tx pulls them out and
 * writes them to the shift register. 8250_port.c:1722 decides when to raise a
 * write wakeup by comparing kfifo_len() against WAKEUP_CHARS, so the length
 * has to be exact or the tty never learns the port drained.
 *
 * Deliberate simplification: mainline's kfifo is a macro-generated type-safe
 * family with a __kfifo core, a mask for power-of-two indexing, and record
 * variants for framed data. None of that is needed for a byte ring -- and
 * mainline's own `size` must be a power of two, which a macro cannot check.
 * This implementation indexes with `% size` instead: one division per byte on
 * a 115200-baud transmit path is nothing, and it cannot be wrong if a future
 * caller passes a size that is not a power of two. The `in`/`out` counters
 * free-run and wrap, so occupancy is `in - out` in plain unsigned arithmetic.
 *
 * The subset is exactly what the staged set calls: kfifo_init, kfifo_reset,
 * kfifo_is_empty, kfifo_len, kfifo_avail, kfifo_put, kfifo_in. kfifo_get and
 * kfifo_out were absent for two stages on the grounds that nothing read
 * through this ring; stage 3 changed that, and serial_core.h now reads it
 * three ways (uart_fifo_out, uart_fifo_get, uart_xmit_advance), so all three
 * are at the bottom of this file.
 */

#include <linux/kernel.h>	/* u8, size_t */
#include <linux/string.h>	/* memcpy */

struct kfifo {
	unsigned char* buf;
	unsigned int size;	/* capacity in bytes */
	unsigned int in;	/* free-running write counter */
	unsigned int out;	/* free-running read counter */
};

/* tty_port.h's spelling: `DECLARE_KFIFO_PTR(xmit_fifo, u8)` */
#define DECLARE_KFIFO_PTR(fifo, type) struct kfifo fifo

/* buffer is supplied later (or never: serial_core allocates it on demand) */
#define kfifo_init(f, buffer, sz)					\
	do {								\
		(f)->buf = (unsigned char*)(buffer);			\
		(f)->size = (unsigned int)(sz);				\
		(f)->in = 0;						\
		(f)->out = 0;						\
	} while (0)

/* Takes the object, not a pointer -- serial_core.c:293's spelling. */
#define INIT_KFIFO(fifo) kfifo_init(&(fifo), NULL, 0)

#define kfifo_reset(fifo)						\
	do { (fifo)->in = 0; (fifo)->out = 0; } while (0)

#define kfifo_is_empty(fifo)	((fifo)->in == (fifo)->out)

#define kfifo_len(fifo)		((fifo)->in - (fifo)->out)

#define kfifo_avail(fifo)	((fifo)->size - (fifo)->in + (fifo)->out)

static inline unsigned int kfifo_put(struct kfifo* f, unsigned char val)
{
	if (!f->buf || kfifo_avail(f) == 0)
		return 0;
	f->buf[f->in % f->size] = val;
	f->in++;
	return 1;
}

static inline unsigned int kfifo_in(struct kfifo* f, const void* buf,
				    unsigned int len)
{
	unsigned int avail;
	unsigned int i;

	if (!f->buf)
		return 0;

	avail = kfifo_avail(f);
	if (len > avail)
		len = avail;

	for (i = 0; i < len; i++)
		f->buf[(f->in + i) % f->size] = ((const unsigned char*)buf)[i];
	f->in += len;
	return len;
}


/*
 * The three reads serial_core.h asks for and this header did not have.
 *
 * mainline spells all three as macros built on __kfifo_out with an element
 * size; DCL's struct kfifo is byte-oriented (no esize field, `size` is a byte
 * capacity), so the bodies are plain byte loops below -- same three
 * behaviours, which is what the callers actually depend on:
 *
 *   kfifo_out(f, buf, n)     copy out and *consume* up to n bytes
 *   kfifo_get(f, val)        copy out and consume exactly one byte; the count
 *                            it returns is 0 or 1, which is why
 *                            uart_fifo_get() can add it straight to
 *                            icount.tx as "one more byte left the fifo"
 *   kfifo_skip_count(f, n)   consume without copying, for
 *                            uart_xmit_advance() -- the transmit path has
 *                            already accounted for the bytes it handed to the
 *                            UART and only needs the ring to move on
 *
 * The clamp to kfifo_len() matters in all three: mainline's variants return
 * short rather than reading past the consumer mark, and a ring that could be
 * asked for more than it holds would otherwise wrap and re-deliver bytes.
 *
 * out is a free-running counter here (kfifo_len is `in - out`, taken mod
 * `size` on access), so advancing it needs no mask -- the same convention the
 * two writers above already use for `in`.
 */
static inline unsigned int kfifo_out(struct kfifo* f, void* buf,
									unsigned int n)
{
	unsigned int i;

	if (!f->buf)
		return 0;
	if (n > kfifo_len(f))
		n = kfifo_len(f);

	for (i = 0; i < n; i++)
		((unsigned char*)buf)[i] = f->buf[(f->out + i) % f->size];
	f->out += n;
	return n;
}

static inline unsigned int kfifo_get(struct kfifo* f, unsigned char* val)
{
	return kfifo_out(f, val, 1);
}

static inline void kfifo_skip_count(struct kfifo* f, unsigned int n)
{
	if (!f->buf)
		return;
	if (n > kfifo_len(f))
		n = kfifo_len(f);
	f->out += n;
}

#endif /* __LINUX_KFIFO_H__ */
