#ifndef __LINUX_LIST_H__
#define __LINUX_LIST_H__

#include <_null.h>
#if !defined(__cplusplus)
/* Xeneva's list_add(list_t*, void*) must be declared before the list_add
 * dispatch macro below, whichever header a file includes first. */
#include <list.h>
#endif

struct list_head {
	struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_prev_entry(pos, member) \
	list_entry((pos)->member.prev, typeof(*(pos)), member)

/*
 * The *_or_null accessors are mainline's guard against a list that turned
 * out to be empty: list_first_entry() on an empty list computes a container
 * out of the head's own address and returns it, so the caller gets a
 * plausible-looking pointer that must not be dereferenced.  These return
 * NULL instead.  READ_ONCE appears only inside macros, never inside a
 * function body, which is why list.h does not have to include <kernel.h> to
 * use it -- <kernel.h> includes this header at :110, long before READ_ONCE
 * is defined at :576, and a macro is expanded where it is written, not
 * where it is defined.
 */
#define list_entry_or_null(ptr, type, member) ({ \
	struct list_head *head__ = (ptr); \
	struct list_head *pos__ = READ_ONCE(head__->next); \
	pos__ != head__ ? list_entry(pos__, type, member) : NULL; \
	})

#define list_first_entry_or_null(ptr, type, member) \
	list_entry_or_null(ptr, type, member)

#define list_last_entry_or_null(ptr, type, member) ({ \
	struct list_head *head__ = (ptr); \
	struct list_head *pos__ = READ_ONCE(head__->prev); \
	pos__ != head__ ? list_entry(pos__, type, member) : NULL; \
	})

#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
	     pos = n, n = pos->next)

#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member); \
	     &pos->member != (head); \
	     pos = list_entry(pos->member.next, typeof(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member), \
	     n   = list_entry(pos->member.next, typeof(*pos), member); \
	     &pos->member != (head); \
	     pos = n, n = list_entry(n->member.next, typeof(*pos), member))

#define list_for_each_entry_reverse(pos, head, member) \
	for (pos = list_entry((head)->prev, typeof(*pos), member); \
	     &pos->member != (head); \
	     pos = list_entry(pos->member.prev, typeof(*pos), member))

/*
 * list_entry_is_head() -- the loop condition shared by the four
 * "resume from here" iterators below.  They differ from
 * list_for_each_entry() in where they start: that macro rewinds to the
 * first element, these take `pos` as given and stop when it comes back
 * around to the head.  `pos` is therefore not assigned an initial value,
 * and the caller must already hold a valid element.
 */
#define list_entry_is_head(pos, head, member) \
	(&(pos)->member == (head))

#define list_for_each_entry_continue(pos, head, member) \
	for (pos = list_next_entry(pos, member); \
	     !list_entry_is_head(pos, head, member); \
	     pos = list_next_entry(pos, member))

#define list_for_each_entry_continue_reverse(pos, head, member) \
	for (pos = list_prev_entry(pos, member); \
	     !list_entry_is_head(pos, head, member); \
	     pos = list_prev_entry(pos, member))

#define list_for_each_entry_from(pos, head, member) \
	for (; !list_entry_is_head(pos, head, member); \
	     pos = list_next_entry(pos, member))

#define list_for_each_entry_from_reverse(pos, head, member) \
	for (; !list_entry_is_head(pos, head, member); \
	     pos = list_prev_entry(pos, member))

/*
 * Mutation -- added for <linux/wait.h>, which keeps its queues in these
 * lists. DCL had only the iteration macros, so a wait queue would have had
 * nowhere to put an entry.
 *
 * LANDMINE, recorded so nobody has to rediscover it: Xeneva's own
 * <list.h> -- a different, older API over `list_t` -- also exports a
 * `list_add(list_t*, void*)`, and the two cannot be declared in one
 * translation unit (they are both `list_add`, with different arguments; C
 * has no overload). Nothing in the ported tty/serial set includes that
 * header, so they coexist in the tree -- but a file that needs Xeneva's
 * process/socket/network headers *and* a mainline list has to be split in
 * two, or one of the two APIs renamed. DCL/linux_irq_shim.c takes the first
 * route: the thread name it publishes in dcl_current() is fetched by
 * DCL/current_ident.c, the one native-header TU, because
 * <Hal/AA64/sched.h> (pthread merge, ad87f135) pulls <list.h> in just as
 * <process.h> does.
 *
 * list_del() is deliberately idempotent: it clears the node and a second
 * remove on an already-removed node is a no-op rather than a write through
 * stale pointers. Mainline poisons the node instead, to catch the bug -- a
 * reasonable trade when a mistake trips a debug kernel, but the wrong one
 * here, where a doubly-removed wait entry would corrupt a queue that nothing
 * else is checking, on a system with no lock protecting it either way.
 */

static inline void __list_add(struct list_head* new,
				struct list_head* prev, struct list_head* next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void INIT_LIST_HEAD(struct list_head* list)
{
	list->next = list;
	list->prev = list;
}

static inline void linux_list_add(struct list_head* new, struct list_head* head)
{
	__list_add(new, head, head->next);
}

/*
 * Xeneva's own BaseHdr/list.h already exports list_add(list_t*, void*). Keep
 * both APIs: dispatch on the second argument's type. Inside the macro body the
 * name list_add is not re-expanded, so the default branch is the real function.
 */
#if !defined(__cplusplus)
#define list_add(new, head) \
	_Generic((head), struct list_head*: linux_list_add, default: list_add)((new), (head))
#endif

static inline void list_add_tail(struct list_head* new, struct list_head* head)
{
	__list_add(new, head->prev, head);
}

static inline void list_del(struct list_head* entry)
{
	if (entry->next) {
		entry->next->prev = entry->prev;
		entry->prev->next = entry->next;
		entry->next = 0;
		entry->prev = 0;
	}
}

static inline void list_del_init(struct list_head* entry)
{
	list_del(entry);
	INIT_LIST_HEAD(entry);
}

/*
 * __list_del_entry() -- unlink without touching the entry's own pointers.
 *
 * Separate from list_del() because list_del() nulls entry->next and
 * entry->prev, which is what the rest of DCL expects of a deleted node, but
 * is not what the move/replace primitives below want: they relink the same
 * node somewhere else and need its neighbours' addresses, not a cleared
 * copy of them.  Mainline's list_del() poisons instead of nulling and its
 * __list_del_entry() leaves the node intact; DCL cannot poison (no
 * LIST_POISON in a freestanding build that has no page-fault reporting to
 * turn a stale dereference into a diagnosis), so the split between the two
 * carries the same meaning with NULL in place of the poison values.
 */
static inline void __list_del_entry(struct list_head* entry)
{
	if (entry->next) {
		entry->next->prev = entry->prev;
		entry->prev->next = entry->next;
	}
}

static inline void list_move(struct list_head* list, struct list_head* head)
{
	__list_del_entry(list);
	list_add(list, head);
}

static inline void list_move_tail(struct list_head* list, struct list_head* head)
{
	__list_del_entry(list);
	list_add_tail(list, head);
}

static inline void list_replace(struct list_head* old, struct list_head* new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

static inline int list_empty(const struct list_head* head)
{
	return head->next == head;
}

/*
 * list_is_first() / list_is_last() -- the two predicates the DRM bridge
 * chain walks with.  They answer "is this node the end of the chain", which
 * is a different question from list_empty() ("is there anything here at
 * all"); drm_bridge_get_next_bridge() needs the former because it returns
 * NULL rather than the head's container when the walk runs off the end.
 *
 * The argument order is (list, head) -- the node being tested first -- which
 * is the opposite of what the surrounding code might suggest, since
 * list_is_head() above takes the same two arguments in the same order for
 * the same reason: these read as "is `list` the first/last entry of `head`".
 */
static inline int list_is_first(const struct list_head* list,
				const struct list_head* head)
{
	return list->prev == head;
}

static inline int list_is_last(const struct list_head* list,
			       const struct list_head* head)
{
	return list->next == head;
}

/*
 * list_is_head() / list_is_singular() / __list_cut_position() /
 * list_cut_position() -- mainline's, verbatim, for virtio_console.c:393:
 *
 *     list_cut_position(&tmp_list, &pending_free_dma_bufs,
 *                       pending_free_dma_bufs.prev);
 *
 * which cuts the whole pending-free list off under the lock so the buffers can
 * be released without it held. The four travel together because
 * list_cut_position() is the only *entry point* but cannot be written without
 * the other three: it asks list_is_singular() whether there is exactly one
 * entry (in which case cutting is a no-op unless entry is the head),
 * list_is_head() whether entry names the head rather than a member, and
 * __list_cut_position() does the relinking.
 *
 * The detail that makes __list_cut_position() worth copying rather than
 * re-deriving is `new_first`: entry->next is read into a local *before*
 * `entry->next = list` overwrites it, so head's new first element is the one
 * that followed the cut point rather than the cut point's own list member.
 * Getting that order wrong produces a list that is self-consistent, silent,
 * and wrong -- the failure mode list debug code exists to catch and DCL does
 * not have.
 *
 * mainline: include/linux/list.h:480-522.
 */
static inline int list_is_head(const struct list_head* list,
			       const struct list_head* head)
{
	return list == head;
}

static inline int list_is_singular(const struct list_head* head)
{
	return !list_empty(head) && (head->next == head->prev);
}

static inline void __list_cut_position(struct list_head* list,
				       struct list_head* head,
				       struct list_head* entry)
{
	struct list_head* new_first = entry->next;

	list->next = head->next;
	list->next->prev = list;
	list->prev = entry;
	entry->next = list;
	head->next = new_first;
	new_first->prev = head;
}

static inline void list_cut_position(struct list_head* list,
				     struct list_head* head,
				     struct list_head* entry)
{
	if (list_empty(head))
		return;
	if (list_is_singular(head) && !list_is_head(entry, head) &&
	    (entry != head->next))
		return;
	if (list_is_head(entry, head))
		INIT_LIST_HEAD(list);
	else
		__list_cut_position(list, head, entry);
}

/*
 * __list_splice() / list_splice() / list_splice_tail() -- move a whole list
 * onto another head without copying any node.  The helper does the
 * relinking against explicit neighbours so the two entry points can name
 * them: list_splice() puts the moved nodes first under `head`,
 * list_splice_tail() last.  Neither touches `list`, so the caller keeps
 * ownership of the source head and it still describes its (now empty)
 * original contents -- which is why drm_flip_work.c can splice the queued
 * list out and then re-initialise it.
 *
 * The empty case is deliberately not guarded here: splicing an empty list
 * would set head->next to head itself and corrupt the destination.  Every
 * call site in the vendored tree sits behind a list_empty() test; this is
 * mainline's contract too, and inventing a silent early return would hide
 * exactly the caller bug that test exists to catch.
 */
static inline void __list_splice(const struct list_head* list,
				 struct list_head* prev,
				 struct list_head* next)
{
	struct list_head* first = list->next;
	struct list_head* last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

static inline void list_splice(const struct list_head* list,
			       struct list_head* head)
{
	if (!list_empty(list))
		__list_splice(list, head, head->next);
}

static inline void list_splice_tail(const struct list_head* list,
				    struct list_head* head)
{
	if (!list_empty(list))
		__list_splice(list, head->prev, head);
}

/*
 * list_cut_before() -- the mirror of list_cut_position(), cut at the node
 * *before* `entry` rather than after it.
 *
 * drm_encoder.c:208 calls it as
 *
 *     list_cut_before(&tmplist, &encoder->bridge_chain,
 *                     &encoder->bridge_chain);
 *
 * with `entry` naming the head, so the whole chain moves: entry == head is
 * the branch that INIT_LIST_HEAD()s the destination and leaves the source
 * to __list_cut_before(), which links every node over to `list`.  Note the
 * singular-list guard reads `head->next != entry && head != entry` -- with
 * one node and entry at the head there is nothing to cut, but the node
 * still has to be moved, so the guard must not reject that case.
 */
static inline void __list_cut_before(struct list_head* list,
				     struct list_head* head,
				     struct list_head* entry)
{
	struct list_head* new_first = entry->prev;

	list->next = head->next;
	list->next->prev = list;
	list->prev = new_first;
	new_first->next = list;
	head->next = entry;
	entry->prev = head;
}

static inline void list_cut_before(struct list_head* list,
				   struct list_head* head,
				   struct list_head* entry)
{
	if (list_empty(head))
		return;
	if (list_is_singular(head) &&
	    (head->next != entry && head != entry))
		return;
	if (entry == head)
		INIT_LIST_HEAD(list);
	else
		__list_cut_before(list, head, entry);
}

/*
 * list_count_nodes() -- length of a list, O(n) and allocation-free.
 *
 * vkms_config.c counts configured CRTCs and planes this way, so it is the
 * one consumer that cannot afford to track a counter alongside the list:
 * the list is built by configfs and read back by paths that never see the
 * writes.  Written as a walk rather than an accumulator in the list itself
 * because mainline's does not carry a length, and DCL keeping one would
 * mean list_add() and friends had to maintain it -- a cost on every insert
 * in the tree to serve two call sites in one driver.
 */
static inline size_t list_count_nodes(const struct list_head* head)
{
	const struct list_head* pos;
	size_t count = 0;

	list_for_each(pos, head)
		count++;

	return count;
}

#endif
