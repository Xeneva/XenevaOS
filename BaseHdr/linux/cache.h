#ifndef __LINUX_CACHE_H
#define __LINUX_CACHE_H

/*
 * DCL <linux/cache.h> -- cache-line alignment types and the cacheline-group
 * markers.
 *
 * mainline's version also carries ____cacheline_aligned_in_smp,
 * ____cacheline_internodealigned_in_smp, INTERNODE_CACHE_* and the arch hooks
 * for cache_line_size(). None of those have a caller in this tree, so they are
 * not here: an uncompiled definition reads as supported and is worse than an
 * absent one.
 */

#include <linux/kernel.h>	/* __u8, size_t */
#include <asm/cache.h>		/* L1_CACHE_SHIFT / L1_CACHE_BYTES */

/*
 * SMP_CACHE_BYTES -- the coherency-line size, i.e. the unit ____cacheline_aligned
 * aligns to. On aarch64 it is the L1 line. Provided because dma-mapping.h's
 * group markers and virtio-rng.c's buffer sizing both key off it.
 */
#ifndef SMP_CACHE_SHIFT
#define SMP_CACHE_SHIFT L1_CACHE_SHIFT
#endif

#ifndef SMP_CACHE_BYTES
#define SMP_CACHE_BYTES L1_CACHE_BYTES
#endif

#ifndef SMP_CACHE_ALIGN
#define SMP_CACHE_ALIGN(x) (((x) + (SMP_CACHE_BYTES - 1)) & ~(SMP_CACHE_BYTES - 1))
#endif

#ifndef ____cacheline_aligned
#define ____cacheline_aligned __attribute__((__aligned__(SMP_CACHE_BYTES)))
#endif

/*
 * __cacheline_group_begin/__end -- mainline's spelling, byte for byte:
 *
 *     #define __cacheline_group_begin(GROUP) \
 *         __u8 __cacheline_group_begin__##GROUP[0]
 *
 * It is a zero-length array member, which is a GNU extension and a legal
 * member declaration here, used to fence a run of fields inside a struct so a
 * checker can assert they sit in their own cacheline. No section is emitted
 * (mainline's alternative definition, used when the toolchain supports it,
 * puts them in .data.cacheline_group.<GROUP>; DCL has no such checker).
 *
 * Two things a caller gets wrong on the first try, so they are written down:
 *
 *  - the macros take NO trailing semicolon. The source writes
 *    `__dma_from_device_group_begin();` and that `;` is the member's.
 *    Emitting one inside the macro would leave a stray declaration.
 *  - GROUP may be empty. virtio-rng.c's struct virtrng_info calls both with
 *    `()`, so GROUP pastes to nothing and the member is plain
 *    `__cacheline_group_begin__[0]`. That is why this cannot be a variadic
 *    or no-argument macro: it has to accept exactly one, possibly-empty token.
 */
#ifndef __cacheline_group_begin
#define __cacheline_group_begin(GROUP) __u8 __cacheline_group_begin__##GROUP[0]
#endif

#ifndef __cacheline_group_end
#define __cacheline_group_end(GROUP) __u8 __cacheline_group_end__##GROUP[0]
#endif

#endif /* __LINUX_CACHE_H */
