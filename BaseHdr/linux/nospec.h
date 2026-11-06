#ifndef _LINUX_NOSPEC_H
#define _LINUX_NOSPEC_H

/*
 * DCL <linux/nospec.h> -- array_index_nospec(), the bounds-check clamp.
 *
 * Only the part a ported driver actually calls is here. mainline's header
 * also carries barrier_nospec() and the arch_prctl / arch_seccomp speculation
 * prctls; nothing in DCL uses those, and an uncompiled definition is worse
 * than an absent one (it reads as supported).
 *
 * ### The one real difference from mainline: the second BUILD_BUG_ON is gone
 *
 * mainline's macro ends with:
 *
 *     BUILD_BUG_ON(sizeof(_i) > sizeof(long));
 *     BUILD_BUG_ON(sizeof(_s) > sizeof(long));
 *
 * The first guard is kept. The second cannot survive on this target:
 * aarch64-unknown-windows is LLP64, so sizeof(long) is 4 while sizeof(size_t)
 * is 8 -- and `_s` is routinely a size_t, `sizeof(vi->data)` in virtio-rng.c
 * being the current instance. mainline can only afford that check because on
 * LP64 long and size_t are the same width; here it would make every use of
 * the macro a compile error rather than catch anything.
 *
 * The truncation it guards against is real but harmless for a length: the
 * value is a struct member's size, always far below 4 GB, and it is only ever
 * compared against an index of the same object. So `_s` is cast to
 * unsigned long explicitly rather than silently converted -- the narrowing is
 * written down at the call site of the mask function, where a future reader
 * will see it.
 *
 * If a _s value could ever exceed 4 GB, the right fix is to make the whole
 * clamp size_t-wide and drop array_index_mask_nospec's unsigned long
 * parameters, not to pretend the guard is still there.
 *
 * ### BITS_PER_LONG is 32 here, and that is load-bearing
 *
 * the mask is a sign-bit trick: it must shift the sign bit of the OR'ed
 * value across the word, so that value is cast to signed `long` -- only then
 * is the shift arithmetic and extends the sign -- and the shift count has to
 * be sizeof(long)*8 - 1, i.e. BITS_PER_LONG - 1. kernel.h defines
 * BITS_PER_LONG exactly that way (it was a hardcoded 64 until bringing this
 * file in, which would have shifted a 32-bit value by 63 and produced mask
 * 0 -- silently reading element 0 of every array, forever).
 *
 * The cast was `unsigned long` for a while, and a logical shift extends
 * nothing: in bounds it left mask 1, so `index & mask` clamped every
 * in-bounds index to its low bit. drm_ioctl()'s core dispatch read nr=0x0C
 * (GET_CAP), masked it to 0 and called the VERSION handler. The signed cast
 * is the whole mechanism, not a detail.
 */

#include <linux/kernel.h>	/* size_t, BUILD_BUG_ON, typeof, OPTIMIZER_HIDE_VAR */

/**
 * array_index_mask_nospec() - ~0 when index < size, 0 otherwise
 * @index: array element index
 * @size: number of elements in array
 *
 * If index is out of bounds its sign bit is set (index >= size makes
 * `size - 1 - index` negative, so the OR is negative). Extending that sign
 * bit across the word and inverting gives 0 out of bounds, ~0 in bounds.
 *
 * Both parameters are unsigned long on purpose, to match the width the sign
 * trick operates on -- see the header comment on why size_t does not fit.
 */
static inline unsigned long array_index_mask_nospec(unsigned long index,
						    unsigned long size)
{
	/*
	 * Always compute the mask even if the compiler can prove the index is
	 * in range: it cannot prove it is in range *under speculation*, which
	 * is the whole threat model. OPTIMIZER_HIDE_VAR stops it from folding
	 * the answer away anyway.
	 */
	OPTIMIZER_HIDE_VAR(index);
	return ~(long)(index | (size - 1UL - index)) >>
	       (BITS_PER_LONG - 1);
}

/**
 * array_index_nospec - sanitize an array index after a bounds check
 *
 * For a sequence like:
 *
 *     if (index < size) {
 *         index = array_index_nospec(index, size);
 *         val = array[index];
 *     }
 *
 * if the CPU speculates past the bounds check this clamps the index into
 * [0, size). Used by virtio-rng.c's copy_data() to keep a buggy or malicious
 * virtio-rng backend from steering the memcpy past the end of `vi->data`.
 */
#define array_index_nospec(index, size)					\
({									\
	typeof(index) _i = (index);					\
	typeof(size) _s = (size);					\
	/* see header: _i must fit an unsigned long; _s may be size_t */	\
	BUILD_BUG_ON(sizeof(_i) > sizeof(unsigned long));		\
	unsigned long _mask =						\
		array_index_mask_nospec((unsigned long)_i,		\
					(unsigned long)_s);		\
	(typeof(_i))(_i & _mask);					\
})

#endif /* _LINUX_NOSPEC_H */
