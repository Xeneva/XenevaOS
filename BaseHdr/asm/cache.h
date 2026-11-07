#ifndef __ASM_CACHE_H
#define __ASM_CACHE_H

/*
 * DCL <asm/cache.h> -- cache-line geometry for aarch64.
 *
 * mainline gets these from arch/arm64/include/asm/cache.h, which is not
 * present here (there is no arch/ tree in BaseHdr at all; asm/ holds only the
 * few headers a ported source names directly). The values are the ones the
 * architecture guarantees: AArch64 has a 64-byte D-cache line, so
 * L1_CACHE_SHIFT is 6.
 *
 * They are not decoration. virtio-rng.c sizes its entropy buffer as
 *
 *     #if SMP_CACHE_BYTES < 32
 *         u8 data[32];
 *     #else
 *         u8 data[SMP_CACHE_BYTES];
 *     #endif
 *
 * so this number decides whether the driver allocates 32 or 64 bytes, and
 * the size flows into the device's minimum read and into copy_data()'s clamp.
 * mainline's comment calls it "minimal size returned by rng_buffer_size()".
 */

#ifndef L1_CACHE_SHIFT
#define L1_CACHE_SHIFT 6
#endif

#ifndef L1_CACHE_BYTES
#define L1_CACHE_BYTES (1 << L1_CACHE_SHIFT)
#endif

#ifndef L1_CACHE_ALIGN
#define L1_CACHE_ALIGN(x) (((x) + (L1_CACHE_BYTES - 1)) & ~(L1_CACHE_BYTES - 1))
#endif

/*
 * ARCH_DMA_MINALIGN -- the alignment DMA-capable allocations must have, which
 * mainline takes from arch/arm64/include/asm/cache.h. A number here rather
 * than mainline's `____cacheline_aligned` attribute spelling: both call sites
 * in this tree wrap it in __aligned(), and `__aligned(__attribute__(...))`
 * does not parse. L1_CACHE_BYTES (64) is the same value mainline's spelling
 * produces, which is the point -- an allocation DMA'd into must start on a
 * cache line or the device and the CPU disagree about which line it is.
 *
 * The one caller is drm_managed.c:56, `u8 __aligned(ARCH_DMA_MINALIGN) data[]`
 * inside struct drmres, so a managed allocation gets its payload on a line of
 * its own.
 *
 * ARCH_HAS_DMA_MINALIGN is deliberately *not* defined alongside it.
 * dma-mapping.h tests that one and switches ____dma_from_device_aligned to
 * the aligned spelling; flipping it would change virtio-rng.c's buffer
 * layout, which the note there records as a decision rather than an omission.
 */
#ifndef ARCH_DMA_MINALIGN
#define ARCH_DMA_MINALIGN L1_CACHE_BYTES
#endif

#endif /* __ASM_CACHE_H */
