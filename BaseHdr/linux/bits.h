#ifndef __LINUX_BITS_H__
#define __LINUX_BITS_H__

/*
 * DCL <linux/bits.h> -- the mask macros mainline gets from
 * <uapi/linux/bits.h> and <vdso/bits.h>.
 *
 * __GENMASK was missing from the whole tree, and a missing *macro* does not
 * stay a parse error here: `__GENMASK(15, 0)` compiles as an implicit call
 * to a function of that name, so drm_mode.h's colour macros
 * (DRM_ARGB64_PREP and friends, :1546-1594) produced an undefined symbol
 * called __GENMASK in every object that expanded them --
 * drm_atomic_state_helper.o:78, drm_blend.o and vkms_composer.o.
 *
 * This header is the only place it can go: drm_mode.h:14 includes
 * <linux/bits.h> itself, and DCL has no uapi/linux/bits.h for it to reach
 * first. The BITS_PER_LONG fallback below is the same one bitops.h carries
 * and the spellings have to agree token for token, because the include order
 * decides which wins (drm_mode.h leads with this header, kernel.h leads with
 * bitops.h) and a mismatch would be a -Wmacro-redefined on every drm file.
 *
 * DMA_BIT_MASK is here for the same reachability reason rather than for
 * affinity. Its mainline home is <linux/dma-mapping.h>, which DCL keeps for
 * declarations only, while vkms_drv.c:188 --
 * `dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64))` -- has this header
 * in its include chain through the drm_fourcc.h path. Written
 * self-contained (not over GENMASK_ULL, which kernel.h owns and which may
 * not have been parsed yet) but with mainline's answer: all-ones at 64 bits
 * and (1<<n)-1 below, so the ternary is what keeps the shift defined.
 *
 *   upstream  uapi/linux/bits.h (__GENMASK), dma-mapping.h:118
 *             (DMA_BIT_MASK = GENMASK_ULL((n) - 1, 0))
 */

#ifndef BITS_PER_LONG
/* 32 -- this target is LLP64, see the long note in <linux/kernel.h>. */
#define BITS_PER_LONG 32
#endif

#ifndef __GENMASK
#define __GENMASK(h, l) \
	(((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#endif

#ifndef __GENMASK_ULL
#define __GENMASK_ULL(h, l) \
	(((~0ULL) << (l)) & (~0ULL >> (63 - (h))))
#endif

#ifndef DMA_BIT_MASK
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#endif

#endif /* __LINUX_BITS_H__ */
