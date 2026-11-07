/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_RBTREE_TYPES_H
#define _LINUX_RBTREE_TYPES_H

#include <stdint.h>

/*
 * DCL delta -- `uintptr_t`, not mainline's `unsigned long`.
 *
 * This field is not a number: it is the parent pointer with the color bit
 * folded into its bottom two bits, read back by rb_parent() and __rb_color().
 * Its width therefore has to be pointer width.  mainline gets that from
 * `unsigned long` being 64-bit on arm64; this target is
 * aarch64-unknown-windows, which is LLP64 (verified: sizeof(long) == 4), so
 * `unsigned long` keeps the parent address's low 32 bits and every node
 * above the 4 GB mark would walk to a parent in the low 4 GB that nothing
 * maps -- the same narrowing serial_core.c's two address lines have, arriving
 * through a struct field this time.  The writers
 * that fill the field are in rbtree.h and rbtree_augmented.h.
 */
struct rb_node {
	uintptr_t      __rb_parent_color;
	struct rb_node *rb_right;
	struct rb_node *rb_left;
} __attribute__((aligned(sizeof(long))));
/* The alignment might seem pointless, but allegedly CRIS needs it */

struct rb_node_linked {
	struct rb_node		node;
	struct rb_node_linked	*prev;
	struct rb_node_linked	*next;
};

struct rb_root {
	struct rb_node *rb_node;
};

/*
 * Leftmost-cached rbtrees.
 *
 * We do not cache the rightmost node based on footprint
 * size vs number of potential users that could benefit
 * from O(1) rb_last(). Just not worth it, users that want
 * this feature can always implement the logic explicitly.
 * Furthermore, users that want to cache both pointers may
 * find it a bit asymmetric, but that's ok.
 */
struct rb_root_cached {
	struct rb_root rb_root;
	struct rb_node *rb_leftmost;
};

/*
 * Leftmost tree with links. This would allow a trivial rb_rightmost update,
 * but that has been omitted due to the lack of users.
 */
struct rb_root_linked {
	struct rb_root		rb_root;
	struct rb_node_linked	*rb_leftmost;
};

#define RB_ROOT (struct rb_root) { NULL, }
#define RB_ROOT_CACHED (struct rb_root_cached) { {NULL, }, NULL }
#define RB_ROOT_LINKED (struct rb_root_linked) { {NULL, }, NULL }

#endif
