#ifndef __LINUX_ATOMIC_H__
#define __LINUX_ATOMIC_H__

/*
 * DCL <linux/atomic.h> -- refcounted counters for the tty layer.
 *
 * DCL had no atomic API at all: nothing in Xeneva needed one, because its own
 * code serialises at the process level. The tty core does need it, and for a
 * reason that is worth stating -- tty_buffer.c uses `atomic_t` as a *turn
 * counter*, not as a lock: `buf->priority` is incremented by
 * tty_buffer_lock_exclusive() so flush_to_ldisc() can see that the line
 * discipline has taken the buffer over and back off mid-loop (tty_buffer.c:482).
 * That read has to be exact or a byte gets delivered twice or dropped.
 *
 * The operations below are real, on clang's __atomic builtins, so aarch64
 * emits LDXR/STXR rather than a plain load/store. That is not belt-and-braces:
 * tty_flip_buffer_push() runs from the UART interrupt handler while
 * flush_to_ldisc() runs from whoever called queue_work() (see
 * <linux/workqueue.h> -- DCL runs work inline), so these two genuinely do
 * race on one core. Making them plain stores would have been the classic
 * "works until it doesn't" bug.
 *
 * Signatures follow mainline: atomic_read() takes a const pointer, atomic_add()
 * takes the value first, *_return() hands back the post-operation value, and
 * atomic_add_unless() is the "add unless it already equals" form
 * serial_core.c uses for its port refcount.
 */

#include <stdbool.h>

typedef struct {
	int counter;
} atomic_t;

#define ATOMIC_INIT(i) { (i) }

/*
 * atomic64_t -- 64-bit counter.  kobject.h declares `extern atomic64_t
 * uevent_seqnum;`, so the *type* has to exist even though nothing here
 * increments it; laid out as long long to be 8 bytes under LLP64 (aarch64
 * windows: long is 4).  Same turn-taking rationale as atomic_t above: single
 * owner, no lock needed, so the accessors go straight to the compiler's
 * atomic builtins rather than to a lock.
 */
typedef struct {
	long long counter;
} atomic64_t;

#define ATOMIC64_INIT(i) { (i) }

static inline long long atomic64_read(const atomic64_t* v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_SEQ_CST);
}

static inline void atomic64_set(atomic64_t* v, long long i)
{
	__atomic_store_n(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic64_inc(atomic64_t* v)
{
	__atomic_add_fetch(&v->counter, 1LL, __ATOMIC_SEQ_CST);
}

/* drm_vblank.c:203 -- vblank_count_inc is 1 + a jump for a missed tick. */
static inline void atomic64_add(long long i, atomic64_t* v)
{
	(void)__atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST);
}

/*
 * atomic64_fetch_add(i, v) -- the value *before* the add, which is what makes
 * it a fetch: dma-fence.c:180's dma_fence_context_alloc() returns it as the
 * new context's id, so two contexts can never share one even though the
 * counter is shared.  atomic64_add_return would hand back post-add and give
 * the first caller id 1 where mainline gives 0.
 */
static inline long long atomic64_fetch_add(long long i, atomic64_t* v)
{
	return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST);
}

/* drm_file.c:144 -- file->client_id, assigned from the post-increment value. */
static inline long long atomic64_inc_return(atomic64_t* v)
{
	return __atomic_add_fetch(&v->counter, 1LL, __ATOMIC_SEQ_CST);
}

static inline int atomic_read(const atomic_t* v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_SEQ_CST);
}

static inline void atomic_set(atomic_t* v, int i)
{
	__atomic_store_n(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int atomic_add_return(int i, atomic_t* v)
{
	return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int atomic_sub_return(int i, atomic_t* v)
{
	return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic_add(int i, atomic_t* v)
{
	(void)__atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic_sub(int i, atomic_t* v)
{
	(void)__atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic_inc(atomic_t* v)
{
	atomic_add(1, v);
}

static inline void atomic_dec(atomic_t* v)
{
	atomic_sub(1, v);
}

static inline int atomic_inc_return(atomic_t* v)
{
	return atomic_add_return(1, v);
}

static inline int atomic_dec_return(atomic_t* v)
{
	return atomic_sub_return(1, v);
}

/* "was it the last reference?" -- tty_port_put()'s free path. */
static inline bool atomic_dec_and_test(atomic_t* v)
{
	return atomic_dec_return(v) == 0;
}

/* drm_file.c:383 -- drm_open()'s dev->open_count, return ignored. */
static inline int atomic_fetch_inc(atomic_t* v)
{
	return __atomic_fetch_add(&v->counter, 1, __ATOMIC_SEQ_CST);
}

/*
 * atomic_read_acquire(v) -- a load the reader may not reorder with what
 * follows.  dma-fence-array.c:122 reads num_pending this way to pair with the
 * atomic_dec_and_test() that publishes the last pending fence, and the
 * comment at :113 says so; both sides are seq_cst here, which is strictly
 * stronger, so the pairing holds.
 */
static inline int atomic_read_acquire(const atomic_t* v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_ACQUIRE);
}

static inline int atomic_xchg(atomic_t* v, int i)
{
	return __atomic_exchange_n(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int atomic_cmpxchg(atomic_t* v, int old, int new_v)
{
	int expected = old;
	(void)__atomic_compare_exchange_n(&v->counter, &expected, new_v, 0,
			__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return expected;
}

/* Returns the value *before* the conditional add (mainline's contract). */
static inline int atomic_add_unless(atomic_t* v, int i, int u)
{
	int c = atomic_read(v);

	do {
		if (c == u)
			break;
	} while (!__atomic_compare_exchange_n(&v->counter, &c, c + i, 0,
			__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));

	return c;
}

static inline void atomic_or(int i, atomic_t* v)
{
	(void)__atomic_fetch_or(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic_and(int i, atomic_t* v)
{
	(void)__atomic_fetch_and(&v->counter, i, __ATOMIC_SEQ_CST);
}

/*
 * atomic_long_t -- the width-augmented form. Only one structure in the tty
 * headers uses it: struct ld_semaphore's `count` (tty_ldisc.h), which mainline
 * keeps as a long because the reader/writer counts are packed into one word
 * with bits to spare.
 *
 * The operations listed are the ones ldsem's implementation needs; adding the
 * rest of mainline's atomic_long_* family would be a second API nobody has
 * called yet.
 */
typedef struct {
	long counter;
} atomic_long_t;

#define ATOMIC_LONG_INIT(i) { (i) }

static inline long atomic_long_read(const atomic_long_t* v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_SEQ_CST);
}

static inline void atomic_long_set(atomic_long_t* v, long i)
{
	__atomic_store_n(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic_long_add(long i, atomic_long_t* v)
{
	(void)__atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline long atomic_long_add_return(long i, atomic_long_t* v)
{
	return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic_long_inc(atomic_long_t* v)
{
	atomic_long_add(1, v);
}

static inline void atomic_long_dec(atomic_long_t* v)
{
	atomic_long_add(-1, v);
}

static inline int atomic_long_dec_and_test(atomic_long_t* v)
{
	return atomic_long_add_return(-1, v) == 0;
}

/*
 * atomic_long_inc_return_relaxed -- mainline's relaxed fetch-add; the one
 * call site in this tree is ww_acquire_init()'s lockdep stamp
 * (linux/ww_mutex.h:203).  DCL has no weak memory to be relaxed about, so
 * the sequential form is the same operation.
 */
static inline long atomic_long_inc_return_relaxed(atomic_long_t* v)
{
	return atomic_long_add_return(1, v);
}

static inline long atomic_long_cmpxchg(atomic_long_t* v, long old, long new_v)
{
	long expected = old;
	(void)__atomic_compare_exchange_n(&v->counter, &expected, new_v, 0,
			__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return expected;
}

/*
 * atomic_long_try_cmpxchg(v, old, new) -- the conditional form, and the one
 * tty_ldsem.c actually uses (five sites: :90, :114, :170, :245, :351), every
 * one of them `if (atomic_long_try_cmpxchg(&sem->count, &count, ...))`
 * inside a retry loop.
 *
 * Difference from the function above, and the reason both exist: the expected
 * value goes in by pointer and is *rewritten* with what the word actually
 * held when the exchange fails.  The loops depend on that -- they compute the
 * next value from `count` on each pass, so a stale count would re-apply the
 * same adjustment forever.  Returning the old value instead (as
 * atomic_long_cmpxchg does) would leave `count` untouched and turn every
 * retry into a repeat.
 *
 * __atomic_compare_exchange_n's second argument is exactly this contract, so
 * the body is one call with `weak = 0`: the loops have their own retry
 * condition and do not need a spurious failure, and seq_cst on both halves
 * matches the rest of this header.
 */
static inline bool atomic_long_try_cmpxchg(atomic_long_t* v, long* old,
					   long new_v)
{
	return __atomic_compare_exchange_n(&v->counter, old, new_v, 0,
			__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

/*
 * cmpxchg(ptr, old, new) -- mainline's generic typed compare-and-swap, the
 * name <linux/atomic.h> re-exports from the arch.  DCL has no arch header
 * for it, and the dma-buf port brought the two callers that need it:
 *
 *   dma-fence-array.c:36, :42  cmpxchg(&array->base.error, PENDING_ERROR, error)
 *   dma-fence-chain.c:65       cmpxchg(&chain->prev, RCU_INITIALIZER(prev),
 *                                      RCU_INITIALIZER(replacement))
 *
 * Those are an `int *` and a `struct dma_fence __rcu **`, which is why this
 * cannot be an int-shaped helper: the fence-chain result is wrapped in
 * unrcu_pointer(), whose expansion is `typeof(*(p)) *`, and an untyped
 * return makes that dereference an int -- which is what the first build of
 * those two files reported ("indirection requires pointer operand ('int'
 * invalid)").  typeof(*(ptr)) gives the macro a type it can hand back
 * without a cast at either call site.
 *
 * The body is clang's builtin because its contract is mainline's exactly:
 * the second argument is where the *actual* old value is stored back, and
 * the call's own result says whether the swap happened -- not what the value
 * was.  So the expression's value is the local, read after the exchange, and
 * it is the old value whether or not it matched.  Objects of 4 and 8 bytes
 * are both supported, which covers the int and the pointer in reach.
 */
#define cmpxchg(ptr, old, new)						\
({									\
	typeof(*(ptr)) __old = (old);					\
	typeof(*(ptr)) __new = (new);					\
	(void)__atomic_compare_exchange_n((ptr), &__old, __new, 0,	\
			__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);		\
	__old;								\
})

#endif /* __LINUX_ATOMIC_H__ */
