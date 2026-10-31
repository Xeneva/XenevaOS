#ifndef _LINUX_TTY_BUFFER_H
#define _LINUX_TTY_BUFFER_H

/*
 * DCL <linux/tty_buffer.h> -- struct tty_buffer and the byte/flag accessors.
 *
 * Ported from mainline include/linux/tty_buffer.h (v7.2), field for field.
 *
 * The layout is the part that cannot be reinterpreted: `data[]` is a flexible
 * array holding *both* the characters and, when flags are in use, a parallel
 * flag byte per character immediately after them -- flag_buf_ptr() finds the
 * flag array by adding b->size (the character capacity) to char_buf_ptr().
 * So `size` is not "how much is in use", it is the fixed stride separating
 * the two arrays inside one allocation, and tty_buffer_reset() setting
 * size = 0 on the sentinel is what makes an empty buffer claim no space at
 * all.
 *
 * `used`/`commit`/`read`/`lookahead` are the producer/consumer cursor set:
 *   used     -- bytes the driver has written (not yet visible to the ldisc)
 *   commit   -- bytes made visible, published by tty_flip_buffer_commit()
 *   read     -- bytes the ldisc has consumed
 *   lookahead-- the ldisc's next candidate position when a consumer stopped
 *               short (tty_buffer.c:404 walks the chain from here)
 * The commit/read gap is what tty_flip_buffer_push() exists to close.
 *
 * The union is the point of the llist: a buffer is either on the live queue
 * (->next) or on the free list (->free), never both, so the two share storage.
 */

#include <linux/atomic.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct tty_buffer {
	union {
		struct tty_buffer* next;
		struct llist_node free;
	};
	unsigned int used;
	unsigned int size;
	unsigned int commit;
	unsigned int lookahead;		/* Lazy update on recv, can become less than "read" */
	unsigned int read;
	bool flags;
	/* Data points here */
	u8 data[] __aligned(sizeof(unsigned long));
};

static inline u8* char_buf_ptr(struct tty_buffer* b, unsigned int ofs)
{
	return b->data + ofs;
}

static inline u8* flag_buf_ptr(struct tty_buffer* b, unsigned int ofs)
{
	return char_buf_ptr(b, ofs) + b->size;
}

/*
 * struct tty_bufhead embeds a `sentinel` tty_buffer, and struct tty_buffer
 * ends in a flexible array -- so a struct with a variable-sized member sits in
 * the middle of another struct. That is a GNU extension and clang says so
 * (-Wgnu-variable-sized-type-not-at-end), once per translation unit, because
 * it comes from a header.
 *
 * mainline's layout is exactly this (include/linux/tty_buffer.h v7.2): the
 * sentinel is what an empty buffer head points at, `size` reads 0 after
 * tty_buffer_reset(&buf->sentinel, 0), and the fields after it (`free`,
 * `mem_used`, `mem_limit`, `tail`) are what the sentinel's zero-length data[]
 * leaves room for. It is deliberate, it is the whole design of the empty
 * buffer, and it cannot be moved to the end without changing what
 * tty_buffer_find() compares against. So the layout is kept and the notice is
 * scoped to this struct: it says "extension", not "bug".
 */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"
#endif
struct tty_bufhead {
	struct tty_buffer* head;	/* Queue head */
	struct workqueue_struct* flip_wq;
	struct work_struct work;
	struct mutex lock;
	atomic_t priority;
	struct tty_buffer sentinel;
	struct llist_head free;		/* Free queue head */
	atomic_t mem_used;		/* In-use buffers excluding free list */
	int mem_limit;
	struct tty_buffer* tail;	/* Active buffer */
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/*
 * When a break, frame error, or parity error happens, these codes are
 * stuffed into the flags buffer.
 */
#define TTY_NORMAL	0
#define TTY_BREAK	1
#define TTY_FRAME	2
#define TTY_PARITY	3
#define TTY_OVERRUN	4

#endif /* _LINUX_TTY_BUFFER_H */
