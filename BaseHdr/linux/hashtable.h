#ifndef __LINUX_HASHTABLE_H__
#define __LINUX_HASHTABLE_H__

/*
 * DCL <linux/hashtable.h> -- the small open-chained table 8250_core.c keeps
 * its per-irq port lists in:
 *
 *     #define IRQ_HASH_BITS 5
 *     static DEFINE_HASHTABLE(irq_lists, IRQ_HASH_BITS);
 *     hash_add(irq_lists, &i->node, up->port.irq);
 *     hash_for_each_possible(irq_lists, obj, node, irq) ...
 *     hlist_del(&i->node);
 *
 * It needs three things to be right rather than to merely compile: both the
 * add and the lookup must fold the key the same way (otherwise a port would
 * be filed in one bucket and searched for in another and simply never be
 * found), the bucket count must be a compile-time power of two, and the node
 * unlink must tolerate a node that was never linked (8250_core.c:123 deletes
 * a port it may not have inserted).
 *
 * hash_min() is therefore one macro used by both sides. Mainline folds with
 * Fibonacci hashing; this is the plain mask. The two agree on which bucket a
 * key belongs to -- only the spread differs, and 32 buckets over a handful of
 * UARTs does not care about spread. HASH_BITS() gets its log2 from __builtin_ctz,
 * which is exact for the power-of-two sizes DECLARE/DEFINE_HASHTABLE create.
 */

#include <linux/kernel.h>	/* container_of */

struct hlist_node {
	struct hlist_node* next;
	struct hlist_node** pprev;
};

struct hlist_head {
	struct hlist_node* first;
};

#define INIT_HLIST_HEAD(head)  do { (head)->first = 0; } while (0)
#define INIT_HLIST_NODE(node)  do { (node)->next = 0; (node)->pprev = 0; } while (0)

static inline void hlist_add_head(struct hlist_node* n, struct hlist_head* h)
{
	struct hlist_node* first = h->first;
	n->next = first;
	n->pprev = &h->first;
	h->first = n;
	if (first)
		first->pprev = &n->next;
}

static inline void hlist_del(struct hlist_node* n)
{
	struct hlist_node** p = n->pprev;
	struct hlist_node* next = n->next;

	/* Never linked (or already removed): the unlink is a no-op, not a bug. */
	if (p) {
		*p = next;
		if (next)
			next->pprev = p;
	}
	n->next = 0;
	n->pprev = 0;
}

static inline int hlist_unhashed(const struct hlist_node* n)
{
	return !n->pprev;
}

#define hlist_entry(ptr, type, member) container_of(ptr, type, member)

#define hlist_entry_safe(ptr, type, member) \
	((ptr) ? hlist_entry(ptr, type, member) : (type*)0)

#define hlist_for_each_entry(pos, head, member)				\
	for (pos = hlist_entry_safe((head)->first, typeof(*pos), member);	\
	     pos;								\
	     pos = hlist_entry_safe((pos)->member.next, typeof(*pos), member))

#define DECLARE_HASHTABLE(name, bits) struct hlist_head name[1 << (bits)]
#define DEFINE_HASHTABLE(name, bits) DECLARE_HASHTABLE(name, bits)

#define HASH_BITS(name) ((int)__builtin_ctz(sizeof(name) / sizeof(*(name))))

/* One fold, used by hash_add and hash_for_each_possible alike. */
#define hash_min(key, bits) ((unsigned int)(key) & ((1u << (bits)) - 1u))

#define hash_add(hashtable, node, key)					\
	hlist_add_head(node, &hashtable[hash_min(key, HASH_BITS(hashtable))])

#define hash_del(node) hlist_del(node)

#define hash_empty(hashtable) __hash_empty(hashtable, HASH_BITS(hashtable))

static inline int __hash_empty(struct hlist_head* ht, unsigned int bits)
{
	unsigned int i;
	for (i = 0; i < (1u << bits); i++)
		if (ht[i].first)
			return 0;
	return 1;
}

#define hash_for_each_possible(name, obj, member, key)			\
	hlist_for_each_entry(obj,						\
		&name[hash_min((unsigned int)(key), HASH_BITS(name))], member)

#define hash_for_each_safe(name, bkt, tmp, obj, member)			\
	for ((bkt) = 0, (obj) = 0; (bkt) < (1u << HASH_BITS(name)) &&	\
	     (((obj) = hlist_entry_safe(name[bkt].first, typeof(*obj), member)), 1); \
	     (bkt)++,							\
	     (obj) = 0)

#define hash_for_each_possible_safe(name, obj, tmp, key, member)		\
	hlist_for_each_entry_safe(obj, tmp,				\
		&name[hash_min((unsigned int)(key), HASH_BITS(name))], member)

#define hlist_for_each_entry_safe(pos, n, head, member)			\
	for (pos = hlist_entry_safe((head)->first, typeof(*pos), member);	\
	     pos && ({ n = pos->member.next; 1; });				\
	     pos = hlist_entry_safe(n, typeof(*pos), member))

#endif /* __LINUX_HASHTABLE_H__ */
