#ifndef __LINUX_LLIST_H__
#define __LINUX_LLIST_H__

/*
 * DCL <linux/llist.h> -- the lockless singly-linked list tty_buffer.c keeps
 * its *free* buffers on.
 *
 * It exists for one specific shape of traffic: a tiny buffer (<= 256 bytes)
 * is recycled far more often than it is created, and the free list is touched
 * both from flush_to_ldisc() and from the driver's allocation path. A singly
 * linked list with an atomic head swap needs no per-node back pointers and no
 * lock, which is what those two callers want.
 *
 * Deliberately a subset of mainline: llist_add_batch() and the _cc variants
 * are omitted because nothing in the staged tty/serial set calls them. Adding
 * them when something does beats carrying a surface nobody has compiled.
 *
 * The subtle part is llist_for_each_entry_safe(): it must not run its body
 * when handed a NULL node (an empty list), and `container_of(NULL, type, m)`
 * is a *computed address*, not NULL -- so the guard is the address of the
 * member itself, which folds back to exactly 0:
 *
 *     pos = container_of(NULL, struct tty_buffer, free);   // pos != NULL
 *     &pos->member  ==  NULL                               // but this is 0
 *
 * That is mainline's own trick (member_address_is_nonnull), and the arithmetic
 * is done in integers so it never forms a non-canonical pointer the CPU could
 * touch -- the body is guarded before anything is dereferenced.
 *
 * The integer is uintptr_t, not mainline's `unsigned long`. This target is
 * aarch64 *Windows*, which is LLP64: unsigned long is 32 bits, so casting a
 * pointer through it truncates. That is not a theoretical concern here -- it
 * is what produced "cast to smaller integer type 'unsigned long' from
 * 'struct llist_node *'" on tty_buffer.c:142, and a buffer whose address ends
 * in 0x????????0000 would have read as 0 after truncation, silently ending
 * the free-list walk one buffer early. uintptr_t is the type that is wide
 * enough by definition, and it silences the warning as a side effect.
 */

#include <linux/kernel.h>	/* container_of, bool, uintptr_t */

struct llist_node {
	struct llist_node* next;
};

struct llist_head {
	struct llist_node* first;
};

#define llist_entry(ptr, type, member) container_of(ptr, type, member)

/*
 * uintptr_t, not unsigned long: see the note above this block. On this LLP64
 * target unsigned long would truncate the address to 32 bits.
 */
#define member_address_is_nonnull(ptr, member) \
	((uintptr_t)&(ptr)->member != (uintptr_t)0)

#define llist_for_each_entry_safe(pos, n, node, member)			\
	for (pos = llist_entry((node), typeof(*pos), member);		\
	     member_address_is_nonnull(pos, member) &&			\
		(n = llist_entry(pos->member.next, typeof(*n), member), 1); \
	     pos = n)

static inline void init_llist_head(struct llist_head* head)
{
	head->first = 0;
}

static inline bool llist_empty(const struct llist_head* head)
{
	return __atomic_load_n(&head->first, __ATOMIC_ACQUIRE) == 0;
}

/*
 * Head is a single pointer, so both directions use a CAS rather than trusting
 * a load-then-store: tty_buffer_free() can run from the work path while the
 * driver allocates from IRQ context on the same core.
 */
static inline int llist_add(struct llist_node* new, struct llist_head* head)
{
	struct llist_node* first;

	do {
		first = __atomic_load_n(&head->first, __ATOMIC_RELAXED);
		__atomic_store_n(&new->next, first, __ATOMIC_RELAXED);
	} while (!__atomic_compare_exchange_n(&head->first, &first, new, 0,
			__ATOMIC_RELEASE, __ATOMIC_RELAXED));

	return first == 0;	/* mainline: true if the list was empty */
}

static inline struct llist_node* llist_del_all(struct llist_head* head)
{
	return __atomic_exchange_n(&head->first, 0, __ATOMIC_ACQ_REL);
}

static inline struct llist_node* llist_del_first(struct llist_head* head)
{
	struct llist_node* first;
	struct llist_node* next;

	do {
		first = __atomic_load_n(&head->first, __ATOMIC_ACQUIRE);
		if (!first)
			return 0;
		next = __atomic_load_n(&first->next, __ATOMIC_RELAXED);
	} while (!__atomic_compare_exchange_n(&head->first, &first, next, 0,
			__ATOMIC_ACQ_REL, __ATOMIC_RELAXED));

	return first;
}

#endif /* __LINUX_LLIST_H__ */
