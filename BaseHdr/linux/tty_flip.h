#ifndef _LINUX_TTY_FLIP_H
#define _LINUX_TTY_FLIP_H

/*
 * DCL <linux/tty_flip.h> -- the driver-facing side of the flip buffer.
 *
 * Ported from mainline v7.2. This is the API a UART interrupt handler calls:
 * tty_insert_flip_char() puts one byte in, tty_flip_buffer_push() makes it
 * visible. Everything else in the header is convenience layered on
 * __tty_insert_flip_string_flags().
 *
 * The fast path in tty_insert_flip_char() is worth reading slowly, because
 * the flag byte is the subtle part:
 *
 *   change = !tb->flags && (flag != TTY_NORMAL);
 *   if (!change && tb->used < tb->size) { ... store ... return 1; }
 *
 * A buffer created without flags (tty_prepare_flip_string(), and every
 * normal-character allocation) has no flag array at all -- flag_buf_ptr()
 * would point into the next buffer's memory. So a non-NORMAL flag forces an
 * allocation for a flags-capable buffer before anything is written; and once
 * a flags buffer is in use, each byte's flag is written *only if* the buffer
 * is flagged, which is what the inner `if (tb->flags)` checks. Getting this
 * wrong does not crash -- it silently records every byte as TTY_NORMAL, and
 * a break or parity error simply never reaches the ldisc.
 */

#include <linux/tty_buffer.h>
#include <linux/tty_port.h>

struct tty_ldisc;

int tty_buffer_set_limit(struct tty_port* port, int limit);
unsigned int tty_buffer_space_avail(struct tty_port* port);
int tty_buffer_request_room(struct tty_port* port, size_t size);
size_t __tty_insert_flip_string_flags(struct tty_port* port, const u8* chars,
				      const u8* flags, bool mutable_flags,
				      size_t size);
size_t tty_prepare_flip_string(struct tty_port* port, u8** chars, size_t size);
void tty_flip_buffer_push(struct tty_port* port);

/**
 * tty_insert_flip_string_fixed_flag - add characters to the tty buffer
 *
 * Queue a series of bytes to the tty buffering. All the characters passed are
 * marked with the supplied flag.
 *
 * Returns: the number added.
 */
static inline size_t tty_insert_flip_string_fixed_flag(struct tty_port* port,
						       const u8* chars, u8 flag,
						       size_t size)
{
	return __tty_insert_flip_string_flags(port, chars, &flag, false, size);
}

/**
 * tty_insert_flip_string_flags - add characters to the tty buffer
 *
 * Queue a series of bytes to the tty buffering. For each character the flags
 * array indicates the status of the character.
 *
 * Returns: the number added.
 */
static inline size_t tty_insert_flip_string_flags(struct tty_port* port,
						  const u8* chars,
						  const u8* flags, size_t size)
{
	return __tty_insert_flip_string_flags(port, chars, flags, true, size);
}

/**
 * tty_insert_flip_char - add one character to the tty buffer
 *
 * Queue a single byte @ch to the tty buffering, with an optional flag.
 */
static inline size_t tty_insert_flip_char(struct tty_port* port, u8 ch, u8 flag)
{
	struct tty_buffer* tb = port->buf.tail;
	int change;

	change = !tb->flags && (flag != TTY_NORMAL);
	if (!change && tb->used < tb->size) {
		if (tb->flags)
			*flag_buf_ptr(tb, tb->used) = flag;
		*char_buf_ptr(tb, tb->used++) = ch;
		return 1;
	}
	return __tty_insert_flip_string_flags(port, &ch, &flag, false, 1);
}

static inline size_t tty_insert_flip_string(struct tty_port* port,
					    const u8* chars, size_t size)
{
	return tty_insert_flip_string_fixed_flag(port, chars, TTY_NORMAL, size);
}

size_t tty_ldisc_receive_buf(struct tty_ldisc* ld, const u8* p, const u8* f,
			     size_t count);

void tty_buffer_lock_exclusive(struct tty_port* port);
void tty_buffer_unlock_exclusive(struct tty_port* port);

#endif /* _LINUX_TTY_FLIP_H */
