#ifndef __LINUX_MATH64_H__
#define __LINUX_MATH64_H__

/*
 * DCL <linux/math64.h> -- 64-bit division and 32x32->64 multiplication.
 *
 * Every body below is the generic C answer (`/`, `%`, `(u64)a * b`) rather
 * than an arch-specific routine, and that is the correct answer here rather
 * than a simplification: this target is aarch64, which has a hardware 64-bit
 * divider and a 32x32->64 multiplier (umulh), so each of these compiles to
 * one instruction pair with no libgcc call -- the exact problem upstream's
 * per-arch split (lib/div64.c and the per-arch lib directories) exists to
 * avoid on 32-bit CPUs that trap or soft-divide. Matching mainline's
 * *answers* is all that is owed, and for truncating integer division the
 * hardware and the reference agree bit for bit, including the signed
 * forms' round-toward-zero.
 *
 * include <linux/types.h> supplies u64/s64/u32/s32 (kernel.h's typedefs) so
 * the prototypes read like mainline's. The include is safe in either
 * direction: types.h's own kernel.h include is behind kernel.h's include
 * guard, which any caller of this header has already set, and asm/div64.h
 * has pulled types.h in before its bottom include of this file.
 *
 * Not every consumer names this header. drm_mm.c, drm_file.c, drm_rect.c,
 * drm_format_helper.c and drm_fourcc.c reach it only through
 * <asm/div64.h>, which includes it at the bottom -- see the bridge note
 * there. That is why the helpers must be inline here and not in a DCL shim:
 * a definition the caller cannot see is not a definition.
 *
 *   upstream  include/linux/math64.h  (mainline v7.2)
 */

#include <linux/types.h>

/*
 * div64_u64 / DIV64_U64_ROUND_UP -- the one pair this header carried for the
 * tty/serial stage, kept because serial_core.c:451 still uses it:
 *
 *     temp *= NSEC_PER_SEC;
 *     port->frame_time = (unsigned int)DIV64_U64_ROUND_UP(temp, baud);
 *
 * which turns a bit time into the nanoseconds a whole frame takes, rounded
 * *up* because a frame time that rounds down would make uart_poll_timeout()
 * wake before the last stop bit had left the wire -- the caller then polls
 * once more and, on a slow line, can poll forever.  Rounding up is the safe
 * direction and it is what upstream's div64_u64() does behind this macro.
 * Written as a macro rather than an inline for the same reachability reason
 * as the rest, just at an earlier stage: serial_core.c happened to include
 * the header, and a plain expression cannot disagree with itself.
 */
#define div64_u64(a, b)			((u64)(a) / (u64)(b))
#define DIV64_U64_ROUND_UP(n, d)	(div64_u64((u64)(n) + (u64)(d) - 1, (d)))

/*
 * div64_u64_rem -- 64/64 divide, remainder through the out parameter.
 *
 * The two callers, drm_mm.c:575 and :792, use only the remainder: each
 * adjusts an alignment up to the next multiple of `alignment` when
 * `alignment` is not a power of two (a power of two goes through the
 * remainder_mask fast path beside the call). Getting this wrong does not
 * fault -- it places a node at a misaligned start, and only the
 * alignment-aware users of drm_mm (address-space VA allocation with a
 * non-power-of-two guard) would ever see it.
 */
static inline u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

/*
 * div64_s64 -- signed 64/64 divide.
 *
 * Reached from <drm/drm_fixed.h>, whose fixed-point arithmetic is compiled
 * into vkms_composer.o and vkms_formats.o. drm_fixed.h:166 divides one
 * fixed-point value by another and :215-227 computes a per-step tolerance
 * for the blend matrices; both compare the result against zero or against a
 * tolerance, so a divide that did not follow the operands' sign would flip a
 * blend decision rather than merely lose a fraction.
 */
static inline s64 div64_s64(s64 dividend, s64 divisor)
{
	return dividend / divisor;
}

/*
 * div_u64 -- unsigned 64/32 divide.
 *
 * drm_file.c:857 steps a byte count down by SZ_1K for fdinfo units
 * (`sz = div_u64(sz, SZ_1K)`) and drm_vblank.c:659-660 turns
 * pixels * 1000000 into a line/frame duration in nanoseconds
 * (`div_u64((u64) mode->crtc_htotal * 1000000, dotclock)`). The 64-bit
 * dividend is the reason the name exists: a plain division by a 32-bit
 * value would still be a 64-bit operation, but the explicit u32 parameter
 * is what tells a 32-bit compiler it may use the cheap path.
 */
static inline u64 div_u64(u64 dividend, u32 divisor)
{
	return dividend / divisor;
}

/*
 * div_s64 -- signed 64/32 divide.
 *
 * One caller, drm_vblank.c:807: the elapsed-time delta between the queried
 * scanout position and the start of the display scanline is computed as
 * `1000000LL * (vpos * mode->crtc_htotal + hpos)`, which is negative when
 * the query lands before the first line has been scanned. That negative has
 * to stay negative -- it is added to a raw timestamp -- so the unsigned form
 * would be a silent timestamp jump of almost 2^64 nanoseconds.
 */
static inline s64 div_s64(s64 dividend, s32 divisor)
{
	return dividend / divisor;
}

/*
 * mul_u32_u32 -- unsigned 32x32 -> 64 multiply.
 *
 * Both callers multiply quantities that legitimately exceed 32 bits:
 * drm_rect.c:65 scales a clipped source rectangle by the destination
 * extent, and drm_modes.c:1307 computes a fixed-point refresh rate
 * (`mul_u32_u32(num, 1000)` where num already sits in the fixed-point
 * domain). Without the widening cast the product would wrap at 2^32 and the
 * scaled rectangle/rate would be wrong by a multiple of 2^32 -- the
 * truncation would be invisible until a large mode was used.
 */
static inline u64 mul_u32_u32(u32 a, u32 b)
{
	return (u64)a * b;
}

/*
 * KHZ2PICOS -- a frequency in kHz as picoseconds, `(1000000000UL/(a))`.
 *
 * Upstream's home is <uapi/linux/fb.h>:241; DCL's <linux/fb.h> is a stub
 * kept only so the include resolves -- drm_modes.c:35 includes it with the
 * comment "for KHZ2PICOS()" and gets nothing from it. The macro is placed
 * here instead because this header (through the asm/div64.h bridge) is
 * reached by both comparers: drm_edid.c:4409-4410 and :4511-4512 match an
 * EDID mode against CEA mode clocks, drm_modes.c:1495 compares two
 * drm_display_modes. All of them compare `mode->clock` (kHz, an int) after
 * scaling to picoseconds, which is how KHz becomes comparable with EDID's
 * kHz-with-decimals without floating point. 1000000000UL fits unsigned long
 * on this LLP64 target (32 bits) with room to spare, and `a` is a mode clock
 * in kHz, so the only input that would fault is zero -- which no parsed
 * display mode carries.
 */
#ifndef KHZ2PICOS
#define KHZ2PICOS(a)			(1000000000UL / (a))
#endif

#endif /* __LINUX_MATH64_H__ */
