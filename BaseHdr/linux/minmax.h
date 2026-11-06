#ifndef __LINUX_MINMAX_H__
#define __LINUX_MINMAX_H__

/*
 * DCL <linux/minmax.h> -- min/max/min_t/max_t/clamp/ARRAY_SIZE.
 *
 * Every definition here is guarded *and* token-identical to the copies already
 * in <linux/kernel.h> and <linux/mm.h>, because those two define min() and
 * ARRAY_SIZE() themselves (mm.h guards, kernel.h does not). Two headers
 * spelling the same macro differently is a -Wmacro-redefined warning at best
 * and, for ARRAY_SIZE, a mismatch in parameter names that the preprocessor
 * counts as a real difference. Matching exactly means whichever header loads
 * first wins silently -- which is what the tty_buffer.c include list, with
 * both <linux/minmax.h> and <linux/mm.h> reachable, needs.
 *
 * min_t()/max_t() do the cast *before* the comparison; that is the whole
 * point, and it is what tty_buffer.c:315 relies on when it writes
 * `min_t(size_t, size - copied, TTY_BUFFER_PAGE)` -- without the cast, an
 * int and a size_t would be compared under the usual arithmetic conversions
 * and a negative difference would wrap to a huge unsigned value.
 */

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min_t
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#endif

#ifndef max_t
#define max_t(type, a, b) ((type)(a) > (type)(b) ? (type)(a) : (type)(b))
#endif

/*
 * MAX_T/MIN_T -- mainline's typed spelling of the two above, defined in terms
 * of them rather than repeated, so the cast-before-compare property is not a
 * second thing to get right. Their only caller is drm_color_mgmt.c:539's
 * `MAX_T(int, DRM_COLOR_ENCODING_MAX, ret)`, which clamps a driver's
 * get_color_encoding() answer into the connector's enum range -- the cast
 * matters because the return is a signed int and one arm of the comparison is
 * an enum constant, and a negative answer must stay negative rather than
 * wrap past the enum bound.
 */
#ifndef MAX_T
#define MAX_T(type, a, b) max_t(type, a, b)
#endif

#ifndef MIN_T
#define MIN_T(type, a, b) min_t(type, a, b)
#endif

/*
 * swap(a, b) -- exchange two lvalues in place, mainline's spelling
 * (minmax.h:307). The local is declared with typeof(a) rather than a fixed
 * type so it keeps whatever width and signedness the first argument has --
 * drm_crtc.c:688 swaps two `int` display dimensions before the clipping
 * arithmetic (a rotated primary plane is checked in the axes the framebuffer
 * is sampled on, not the ones the mode is stated in), while vkms_formats.c
 * swaps __u16 pixel components at :267 and u64 fixed-point matrix entries at
 * :867-869. A typed helper would force a cast at two of the three.
 *
 * do/while(0) rather than a bare comma expression, because swap() is a
 * statement and mainline spells it that way: `if (rotated) swap(a, b);
 * else ...` has to stay one statement. Same shape as assign_bit() in
 * bitops.h and WRITE_ONCE() in kernel.h.
 */
#ifndef swap
#define swap(a, b) \
	do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)
#endif

#ifndef clamp
#define clamp(val, lo, hi) ((val) < (lo) ? (lo) : ((val) > (hi) ? (hi) : (val)))
#endif

/*
 * clamp_val -- mainline's spelling here is
 * `__careful_clamp(typeof(val), val, lo, hi)`, which typechecks the three
 * arguments through __types_ok3()/BUILD_BUG_ON_MSG; DCL has none of that
 * apparatus, and both callers pass a lo/hi pair already in the type they
 * mean: drm_color_mgmt.c:144 clamps an s64 magnitude against literals, and
 * vkms_composer.c:231-234 clamps four __u16 pixel components against 0 and
 * 0xffff. So this is the plain comparison, and the name is kept only because
 * the callers are mainline's.
 */
#ifndef clamp_val
#define clamp_val(val, lo, hi) clamp((val), (lo), (hi))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#endif /* __LINUX_MINMAX_H__ */
