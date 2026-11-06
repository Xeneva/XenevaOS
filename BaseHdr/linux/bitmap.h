#ifndef __LINUX_BITMAP_H__
#define __LINUX_BITMAP_H__

#include <linux/kernel.h>
#include <linux/bitops.h>	/* set_bit, clear_bit, test_bit, BITS_TO_LONGS,
				 * DECLARE_BITMAP. mainline's <linux/bitmap.h>
				 * includes this header too, and for the same
				 * reason: the two files are one vocabulary
				 * split by nothing but history. This one's own
				 * copies of those went away with the include
				 * -- they were duplicates, and bitops.h's
				 * take a cast that makes them work with any
				 * pointer where these did not.
				 *
				 * DECLARE_BITMAP had to move there rather than
				 * stay: this header includes bitops.h at its
				 * top, and kernel.h's chain reaches
				 * nodemask_types.h, which uses it, before
				 * returning. The full reasoning is at the top
				 * of bitops.h.
				 *
				 * bitops.h is also what xarray.c actually
				 * needs: it includes only this header but
				 * calls __set_bit/__test_and_*_bit, and a
				 * static inline it cannot see is a symbol the
				 * linker cannot find. */

#define test_and_set_bit(nr, addr) \
  ({int __old = test_bit(nr, addr); set_bit(nr, addr); __old; })

#define test_and_clear_bit(nr,addr) \
  ({ int __old = test_bit(nr, addr); clear_bit(nr,addr); __old;})

#define bitmap_zero(dst, nbits) \
 memset(dst, 0, BITS_TO_LONGS(nbits) * sizeof(unsigned long))

#define bitmap_copy(dst, src, nbits) \
 memcpy(dst, src, BITS_TO_LONGS(nbits) * sizeof(unsigned long))

/*
 * bitmap_fill / bitmap_empty / bitmap_clear -- the three the xarray port
 * needs, added together because xarray.c:95-113 uses all three to maintain
 * one mark word and a partial answer would be worse than none.
 *
 * The tail handling is not decoration. BITS_PER_LONG is 32 in this build --
 * this target is LLP64, so `long` is four bytes -- and a request is not a
 * multiple of it: node_any_mark() asks about XA_CHUNK_SIZE bits, which is a
 * whole number of words, but bitmap_clear() is asked for a *range* starting
 * at an arbitrary bit (xarray.c:136 passes xa_offset + 1), so both edges can
 * land mid-word. Masking the last word in bitmap_empty() is what stops a
 * set bit past the end of the requested range from making a node look
 * marked, and it is the difference between the mark logic working and it
 * quietly drifting.
 *
 * mainline's bodies live in lib/bitmap.c and reach the same three answers
 * through native_fill()/find_next_bit(); these are the same semantics in a
 * form that needs no second file, since the only consumer is xarray.c.
 */
static inline void bitmap_fill(unsigned long* dst, unsigned int nbits)
{
	unsigned int nlongs = BITS_TO_LONGS(nbits);
	unsigned int tail = nbits % BITS_PER_LONG;

	memset(dst, 0xff, nlongs * sizeof(unsigned long));
	if (tail)
		dst[nlongs - 1] &= (1UL << tail) - 1;
}

static inline int bitmap_empty(const unsigned long* src, unsigned int nbits)
{
	unsigned int nlongs = BITS_TO_LONGS(nbits);
	unsigned int tail = nbits % BITS_PER_LONG;
	unsigned int i;

	for (i = 0; i < nlongs; i++)
	{
		unsigned long w = src[i];

		if (i + 1 == nlongs && tail)
			w &= (1UL << tail) - 1;
		if (w)
			return 0;
	}
	return 1;
}

static inline void bitmap_clear(unsigned long* map, unsigned int start, int len)
{
	unsigned long* p = map + (start / BITS_PER_LONG);
	unsigned int bits = start % BITS_PER_LONG;

	while (len > 0)
	{
		unsigned int take = BITS_PER_LONG - bits;
		unsigned long mask;

		if (take > (unsigned int)len)
			take = (unsigned int)len;

		/*
		 * The guard is the shift width, not an optimisation: when
		 * take reaches BITS_PER_LONG, `1UL << take` would shift by
		 * the width of the type, which is undefined. ~0UL is the
		 * answer that case was reaching for, and it is also the only
		 * one that covers the whole word from bit 0.
		 */
		if (take == BITS_PER_LONG)
			mask = ~0UL;
		else
			mask = ((1UL << take) - 1) << bits;
		*p &= ~mask;

		len -= take;
		bits += take;
		if (bits == BITS_PER_LONG)
		{
			p++;
			bits = 0;
		}
	}
}

/*
 * bitmap_set -- the counterpart of bitmap_clear above, same body with the
 * mask OR-ed in instead of cleared.
 *
 * mainline's lives in lib/bitmap.c; this tree has no lib/bitmap.c, and the
 * only caller is drm_edid.c's HDMI 2.0 YCbCr 4:2:0 parsing, which records a
 * VIC by setting one bit in each of two capability maps
 * (`bitmap_set(hdmi->y420_cmdb_modes, vic, 1)` at :6089 and the same for
 * y420_vdb_modes at :6124). It is called with len 1 at both sites, but the
 * range form is written out anyway: it is the same loop as bitmap_clear,
 * so a second, single-bit implementation would be the one that could drift
 * from the clear side when the maps are next walked in bulk.
 *
 * The `take == BITS_PER_LONG` branch is bitmap_clear's, for the same
 * reason: `1UL << BITS_PER_LONG` is an undefined shift, and a full-word run
 * is ~0UL with no shift needed. The tail of the range may also land
 * mid-word, which is why the mask is built at `bits` rather than starting
 * at bit 0.
 */
static inline void bitmap_set(unsigned long* map, unsigned int start, int len)
{
	unsigned long* p = map + (start / BITS_PER_LONG);
	unsigned int bits = start % BITS_PER_LONG;

	while (len > 0)
	{
		unsigned int take = BITS_PER_LONG - bits;
		unsigned long mask;

		if (take > (unsigned int)len)
			take = (unsigned int)len;

		if (take == BITS_PER_LONG)
			mask = ~0UL;
		else
			mask = ((1UL << take) - 1) << bits;
		*p |= mask;

		len -= take;
		bits += take;
		if (bits == BITS_PER_LONG)
		{
			p++;
			bits = 0;
		}
	}
}

#endif /* __LINUX_BITMAP_H__ */
