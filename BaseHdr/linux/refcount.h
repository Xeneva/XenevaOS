/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DCL <linux/refcount.h> -- mainline's saturating refcount, over DCL's atomics.
 *
 * Why this exists at all: BaseHdr/drm/ is 101 byte-ports of mainline headers,
 * and struct kref in mainline holds a refcount_t. drm_connector.c:1070 does
 * not merely call a kref helper -- it reaches inside the struct and writes
 *
 *     if (!refcount_dec_and_test(&conn->base.refcount.refcount))
 *             return;
 *
 * so the field has to be a refcount_t and this header has to provide the
 * operations. DCL's own kref.h had the field as atomic_t, which is the older
 * mainline layout; kref.h is rewritten below to match mainline's, and
 * atomic_t stays as it is because tty/serial code uses it directly as a
 * counter, not as a reference count.
 *
 * Saturation is the whole reason refcount_t is not just atomic_t, and it is
 * kept here rather than simplified away. A wrapped reference count turns a
 * live object into a freed one (or the reverse), and it does so silently,
 * long after the mistake that caused it. Saturating trades that for a
 * permanently stuck counter, which is at least loud in the debugger and
 * cannot be mistaken for a working object.
 *
 * Two differences from mainline's implementation, neither semantic:
 *
 *   - Ordering. Mainline picks relaxed/release/acquire per operation to save
 *     instructions. Every DCL atomic is __ATOMIC_SEQ_CST, which is stronger
 *     than all three, so the guarantees mainline documents hold here; the
 *     per-operation ordering simply is not spelled out because there is one
 *     ordering and it is the strong one.
 *
 *   - The fetch-and-return forms. Mainline uses atomic_fetch_add_relaxed()
 *     and atomic_fetch_sub_release(), which return the value *before* the
 *     operation. DCL's atomic_add_return()/atomic_sub_return() return the
 *     value after, so the before-value is recovered by adding/subtracting i
 *     back. Same number, different arithmetic.
 *
 * refcount_dec_not_one() is not ported. Mainline declares it extern with the
 * body in lib/refcount.c, and its only caller in this tree was
 * drm_gem_shmem_helper.c, which is not vendored (see Vendored/README.md for
 * why). It can be added with that caller.
 */

#ifndef __LINUX_REFCOUNT_H__
#define __LINUX_REFCOUNT_H__

#include <linux/atomic.h>
#include <linux/compiler.h>
/*
 * Not a mainline include -- mainline reaches NULL through its own headers
 * and never names the dependency. This port does name it, in the four inline
 * wrappers that pass NULL for mainline's optional `int *oldp` out-parameter,
 * and <linux/atomic.h> does not carry it here. <linux/stddef.h> is where DCL
 * defines NULL; kernel.h reaches the same macro via <_null.h>, but that is
 * kernel.h's business rather than a contract refcount.h can lean on.
 */
#include <linux/stddef.h>	/* NULL */
#include <linux/refcount_types.h>

#define REFCOUNT_INIT(n)	{ .refs = ATOMIC_INIT(n), }
#define REFCOUNT_MAX		0x7fffffff
/*
 * Mainline: INT_MIN / 2. Written as the literal because this tree builds
 * freestanding and <limits.h> is not on the kernel include path; the value
 * is what matters, and it is chosen to sit roughly equidistant from 0 and
 * INT_MAX so that the "bad value" range it opens up is as small as an
 * unchecked counter can make it.
 */
#define REFCOUNT_SATURATED	((int)0xc0000000)

enum refcount_saturation_type {
	REFCOUNT_ADD_NOT_ZERO_OVF,
	REFCOUNT_ADD_OVF,
	REFCOUNT_ADD_UAF,
	REFCOUNT_SUB_UAF,
	REFCOUNT_DEC_LEAK,
};

/*
 * Defined in DCL/drm_refcount_shim.c. Mainline's version WARNs and then
 * sets the counter to REFCOUNT_SATURATED; the set is the load-bearing half,
 * since without it the counter keeps moving and the corruption the warning
 * describes actually happens. The warning half has nowhere to go -- DCL has
 * no WARN_ON that a developer would see from a driver fault -- so the
 * saturation is preserved and the diagnostic is not pretended to.
 */
void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t);

/**
 * refcount_set - set a refcount's value
 * @r: the refcount
 * @n: value to which the refcount will be set
 */
static inline void refcount_set(refcount_t *r, int n)
{
	atomic_set(&r->refs, n);
}

/**
 * refcount_read - get the refcount value
 * @r: the refcount
 *
 * Return: the refcount value
 */
static inline unsigned int refcount_read(const refcount_t *r)
{
	return (unsigned int)atomic_read(&r->refs);
}

/*
 * __refcount_add_not_zero() -- add @i unless the count is already zero.
 *
 * The zero test is inside the loop rather than before it because the count
 * can reach zero between the read and the update; mainline expresses this as
 * an atomic_try_cmpxchg() that refreshes its @old on failure, and the loop
 * below is the same retry with DCL's atomic_cmpxchg(), which reports the
 * observed value instead of updating the expected one in place.
 *
 * Returning the pre-add value is what makes the zero case distinguishable
 * without a second read: old == 0 means "was already dead", and the update
 * was not applied.
 */
static inline __must_check bool __refcount_add_not_zero(int i, refcount_t *r,
							int *oldp)
{
	int old = atomic_read(&r->refs);

	do {
		if (!old)
			break;
	} while (atomic_cmpxchg(&r->refs, old, old + i) != old);

	if (oldp)
		*oldp = old;

	if (old < 0 || old + i < 0)
		refcount_warn_saturate(r, REFCOUNT_ADD_NOT_ZERO_OVF);

	return old;
}

/**
 * refcount_add_not_zero - add a value to a refcount unless it is 0
 * @i: the value to add to the refcount
 * @r: the refcount
 *
 * Will saturate at REFCOUNT_SATURATED and warn.
 *
 * Return: true if the increment was successful, false otherwise
 */
static inline __must_check bool refcount_add_not_zero(int i, refcount_t *r)
{
	return __refcount_add_not_zero(i, r, NULL);
}

/**
 * refcount_inc_not_zero - increment a refcount unless it is 0
 * @r: the refcount
 *
 * Will saturate at REFCOUNT_SATURATED and warn.
 *
 * Return: true if the increment was successful, false otherwise
 */
static inline __must_check bool refcount_inc_not_zero(refcount_t *r)
{
	return __refcount_add_not_zero(1, r, NULL);
}

/**
 * __refcount_add() -- add to a refcount that is expected to be live.
 *
 * The two saturate cases are distinct on purpose: old == 0 means the count
 * was already dead and something is taking a reference to a freed object
 * (ADD_UAF), while old < 0 or a wrapped sum means it overflowed (ADD_OVF).
 * They are different bugs with different fixes, and collapsing them would
 * leave the report describing neither accurately.
 */
static inline void __refcount_add(int i, refcount_t *r, int *oldp)
{
	int old = atomic_add_return(i, &r->refs) - i;

	if (oldp)
		*oldp = old;

	if (!old)
		refcount_warn_saturate(r, REFCOUNT_ADD_UAF);
	else if (old < 0 || old + i < 0)
		refcount_warn_saturate(r, REFCOUNT_ADD_OVF);
}

/**
 * refcount_add - add a value to a refcount
 * @i: the amount to add to the refcount
 * @r: the refcount
 *
 * Will saturate at REFCOUNT_SATURATED and warn.
 */
static inline void refcount_add(int i, refcount_t *r)
{
	__refcount_add(i, r, NULL);
}

/**
 * refcount_inc - increment a refcount
 * @r: the refcount
 *
 * Will saturate at REFCOUNT_SATURATED and warn.
 */
static inline void refcount_inc(refcount_t *r)
{
	__refcount_add(1, r, NULL);
}

/**
 * __refcount_sub_and_test() -- drop @i references and report exhaustion.
 *
 * The true return is reserved for the transition that actually reaches zero
 * (old > 0 && old == i); a count that was already at or below zero is an
 * underflow and returns false after saturating, because reporting "this was
 * the last reference" for an object that was already released would have the
 * caller free it a second time.
 */
static inline __must_check bool __refcount_sub_and_test(int i, refcount_t *r,
							int *oldp)
{
	int old = atomic_sub_return(i, &r->refs) + i;

	if (oldp)
		*oldp = old;

	if (old > 0 && old == i)
		return true;

	if (old <= 0 || old - i < 0)
		refcount_warn_saturate(r, REFCOUNT_SUB_UAF);

	return false;
}

/**
 * refcount_sub_and_test - subtract from a refcount and test if it is 0
 * @i: the amount to subtract from the refcount
 * @r: the refcount
 *
 * Will saturate at REFCOUNT_SATURATED and warn.
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
static inline __must_check bool refcount_sub_and_test(int i, refcount_t *r)
{
	return __refcount_sub_and_test(i, r, NULL);
}

/**
 * refcount_dec_and_test - decrement a refcount and test if it is 0
 * @r: the refcount
 *
 * Will saturate at REFCOUNT_SATURATED and warn.
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
static inline __must_check bool refcount_dec_and_test(refcount_t *r)
{
	return __refcount_sub_and_test(1, r, NULL);
}

/*
 * refcount_dec() -- the plain decrement refcount_dec_and_test() above is
 * built on, and mainline's neighbour to refcount_inc() (v7.2 refcount.h).
 * Added with the virtio-gpu port: DCL/drm_shmem_shim.c's unpin drops
 * pages_pin_count with it. Underflow saturates and warns exactly as the
 * other arms do; the result __refcount_sub_and_test() also hands back is
 * the part mainline's refcount_dec() drops.
 */
static inline void refcount_dec(refcount_t *r)
{
	__refcount_sub_and_test(1, r, NULL);
}

#endif /* __LINUX_REFCOUNT_H__ */
