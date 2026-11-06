#ifndef __LINUX_STRING_H__
#define __LINUX_STRING_H__

/*
 * DCL <linux/string.h> -- memcpy/memset/memcmp and friends.
 *
 * These are Xeneva's own routines (BaseHdr/string.h), not mainline's lib/:
 * the tty layer copies into and out of flip buffers with them, and a second
 * implementation would only be a second thing to get right. Mainline includes
 * <linux/string.h> for the same reason -- to see the prototypes -- so the
 * header exists to resolve the include and nothing more.
 *
 * tty_buffer.c's use is exactly memcpy() into char_buf_ptr(), memset() over
 * the flags byte for a run of same-flagged characters, and the zeroing of
 * consumed bytes in receive_buf() so a buffer can be handed on.
 *
 * The DRM/KMS stage then made this header carry real content, all of it for
 * callers that reach no other string header:
 *
 *   - strnchr, str_has_prefix, mem_is_zero and strtomem_pad, which mainline
 *     keeps here. drm_modes.c parses the video= command line with the first
 *     two, drm_edid.c asks whether a 128-byte block is all zero, and
 *     drm_connector.c:614-615 copies an HDMI vendor/product string into a
 *     fixed-width, possibly non-NUL-terminated field.
 *
 *   - str_yes_no, re-exported from <linux/string_choices.h> rather than
 *     copied. drm_edid.c:6319 prints an HDMI capability as yes/no and
 *     includes neither that header nor <linux/string_helpers.h>, its other
 *     carrier in this tree. Copying the body would collide with the header
 *     in the files that do include it -- two static definitions of the same
 *     name in one translation unit is not a warning.
 *
 *   - strscpy_pad and scnprintf as declarations only, with bodies in
 *     DCL/linux_math_shim.c. Both need something this header cannot supply:
 *     strscpy_pad's error return is -E2BIG, which kernel.h:281 defines
 *     *after* it includes this file at :121, so an inline here cannot be
 *     parsed in every kernel.h-initiated chain; scnprintf must reach
 *     _vsnprintf's va_list plumbing (<Log/_print.h>, KernelAA64's own
 *     header). A declaration is all the call sites need either way.
 */

#include <string.h>		/* native memcpy/memset/memcmp/strlen */
#include <stddef.h>		/* size_t; ssize_t for strscpy_pad */
#include <stdbool.h>		/* bool, for mem_is_zero */
#include <linux/string_choices.h>	/* str_yes_no */

/*
 * strreplace(str, old, new) -- swap every `old` character in `str` for `new`
 * and hand the same pointer back.  mainline declares it here (:282) although
 * the body lives in lib/string_helpers.c:985, and the declaration is load
 * bearing: string_helpers.c:747 calls it, and with no prototype in scope the
 * call is implicitly `int strreplace()`, which clang then rejects as
 * "conflicting types" when it reaches the real definition 240 lines later.
 */
char *strreplace(char *str, char old, char new);

/*
 * strnchr(s, count, c) -- strchr() bounded to `count` bytes.
 *
 * The difference from strchr() is the whole reason drm_modes.c uses it
 * (drm_modes.c:2302, :2390, :2392, :2396): the string being searched is a
 * slice of a larger command-line buffer -- the connector name before the
 * ':' or '-' or '@' separators -- and it is *not* NUL-terminated at the
 * point the caller cares about. `name_end`/`options_off` are how long that
 * slice is. A plain strchr() would run past the slice into the rest of the
 * option string and report the separator belonging to a later field, so
 * `video=HDMI-A-1:1280x720@60` would be read as a name that itself contains
 * '@'. mainline's body is this loop in lib/string.c; count is decremented
 * before the NUL test so `count == 0` answers NULL rather than reading s.
 */
static inline char *strnchr(const char *s, size_t count, int c)
{
	for (; count-- && *s != '\0'; ++s)
		if (*s == (char)c)
			return (char *)s;
	return NULL;
}

/*
 * str_has_prefix(str, prefix) -- strlen(prefix) if str starts with prefix,
 * else 0.
 *
 * The return has to be the *length*, not a boolean, for the one call site:
 * drm_modes.c:2318 matches a command-line name against each named mode with
 *
 *     ret = str_has_prefix(name, mode->name);
 *     if (ret != name_end)
 *             continue;
 *
 * where `name` is the unbounded slice and `name_end` its measured length, so
 * the comparison is "starts with mode->name *and* is exactly that long". A
 * strncmp-style 0/1 answer would compare `1 != name_end` and every named
 * mode would be rejected except a one-character one. strncmp is bounded by
 * len, so a short `str` (NUL before len) also answers 0, as mainline
 * documents.
 */
static inline size_t str_has_prefix(const char *str, const char *prefix)
{
	size_t len = strlen(prefix);

	return strncmp(str, prefix, len) == 0 ? len : 0;
}

/*
 * mem_is_zero(s, n) -- every one of the first n bytes is zero.
 *
 * Only caller: edid_block_is_zero() at drm_edid.c:1857, which answers
 * whether an EDID block is a blank/absent one (`mem_is_zero(edid,
 * EDID_LENGTH)`). mainline implements it over memchr_inv(); DCL has no
 * memchr_inv and the block is 128 bytes on a cold path, so the plain byte
 * loop is the same answer without a new lib helper. The void* is walked as
 * unsigned char* deliberately: the EDID buffer's alignment is not known to
 * this header and a word-wide read would be a fault risk for no gain.
 */
static inline bool mem_is_zero(const void *s, size_t n)
{
	const unsigned char *p = (const unsigned char *)s;
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i])
			return false;
	return true;
}

/*
 * strtomem_pad(dest, src, pad) -- copy the NUL-terminated `src` into the
 * fixed-width array `dest`, filling whatever is left with `pad`.
 *
 * drm_connector.c:614-615 is the call:
 *
 *     strtomem_pad(connector->hdmi.vendor, vendor, 0);
 *     strtomem_pad(connector->hdmi.product, product, 0);
 *
 * where the fields are `unsigned char vendor[DRM_CONNECTOR_HDMI_VENDOR_LEN]
 * __nonstring` (drm_connector.h:1933) -- HDMI 2.1 wants them fixed-width with
 * no NUL, and a bytes-typed readout must see the padding rather than
 * whatever the struct held before. The source strings come from userspace
 * through the connector init path and are not guaranteed to be shorter than
 * the field, so the copy must stop at sizeof(dest) without reading the
 * source past it -- the loop below reads src[i] only while i < dest_len,
 * which is strnlen(src, dest_len)'s guarantee without needing a strnlen DCL
 * does not carry.
 *
 * The helper is a real inline and the macro supplies sizeof(dest) and the
 * compile-time array check: dest is an array, not a pointer, and the size is
 * not an argument (same contract as mainline's strtomem_pad).
 * __must_be_array(dest) is the check -- its _Static_assert stops the build
 * for a pointer, where sizeof would silently be the pointer's size and the
 * copy would truncate -- but its *value* is not part of the length here.
 * mainline adds it (it folds to 0 on GNU/Linux), and on this target it does
 * not: aarch64-unknown-windows gives an empty struct -- the shape
 * __BUILD_BUG_ON_ZERO_MSG folds to -- size 4, so
 * `sizeof(dest) + __must_be_array(dest)` made every copy run 4 bytes past
 * the field until the check was split onto its own line. The do/while is
 * mainline's shape too; both call sites are statements.
 */
static inline void __dcl_strtomem_pad(void *dest, size_t dest_len,
				      const char *src, char pad)
{
	unsigned char *d = (unsigned char *)dest;
	size_t i = 0;

	while (i < dest_len && src[i] != '\0') {
		d[i] = (unsigned char)src[i];
		i++;
	}
	while (i < dest_len)
		d[i++] = (unsigned char)pad;
}

#define strtomem_pad(dest, src, pad) do {				\
	(void)__must_be_array(dest);					\
	__dcl_strtomem_pad((dest), sizeof(dest), (src), (pad));		\
} while (0)

/*
 * strscpy_pad(dest, src, count) -- bounded copy, NUL-terminated, with the
 * remainder of the destination zero-filled; returns the number of characters
 * copied (excluding the NUL), or -E2BIG if count was 0 or the copy was
 * truncated.
 *
 * All five callers (drm_property.c:131, :424, :477 and drm_modes.c:2622,
 * :2663) copy into a fixed 32-byte name field, several of them from a
 * userspace-provided struct that may carry no NUL at all within the field;
 * the zero-fill is what makes the kernel-side copy well-defined for the
 * strcmp()s and printf("%s")s that follow, and the bounded read is what
 * keeps a missing terminator from walking off the ioctl buffer. The body
 * lives in DCL/linux_math_shim.c beside the other real functions -- a
 * header inline would need -E2BIG, which is not defined until kernel.h:281,
 * after this header is parsed at kernel.h:121.
 */
ssize_t strscpy_pad(char *dest, const char *src, size_t count);

/*
 * scnprintf(buf, size, fmt, ...) -- vsnprintf that returns what it actually
 * wrote, clamped to size - 1 when the output did not fit.
 *
 * Called from drm_dev_wedged_event() (drm_drv.c:572 and :579) to build the
 * "WEDGED=<recovery>,..." uevent string: the second call appends at
 * event_string + len and the return is added to len, then the trailing comma
 * is chopped with `event_string[len - 1] = '\0'`. That arithmetic only works
 * if len never counts characters that were not written -- snprintf's
 * would-have-written return would push len past the buffer and the chop
 * would land on the wrong byte. The clamped return is the entire difference
 * between scnprintf and snprintf, and it is why this cannot just be
 * sprintf.h's `#define snprintf _snprintf`.
 *
 * The body is in DCL/linux_math_shim.c: it wraps _vsnprintf
 * (<Log/_print.h>), whose va_list plumbing a BaseHdr header should not pull
 * into every translation unit. __printf(3, 4) is mainline's annotation and
 * is kept so the two call sites' arguments are checked against the format.
 */
int scnprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);

#endif /* __LINUX_STRING_H__ */
