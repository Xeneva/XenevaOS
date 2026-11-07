/*
 * DCL/linux_math_shim.c -- the arithmetic and string entry points that the
 * link needs as real functions, for the cases where a header definition
 * cannot do the job.
 *
 * Three bodies, each missing its natural header for a different reason:
 *
 *  - __div64_32() is the back half of <asm/div64.h>'s do_div(): the macro
 *    expands to a 64/32 divide when the dividend's high word is non-zero,
 *    and on a 32-bit CPU that has to be a call into lib/div64.c. Several
 *    DRM translation units (drm_modes.c, drm_rect.c, drm_vblank.c,
 *    drm_fourcc.c, string_helpers.c) expand a do_div() with a runtime
 *    divisor, so the symbol is referenced from all of them and can only be
 *    defined once, here.
 *
 *  - memmove() is declared by Xeneva's own <string.h> (:57) but no file in
 *    the image defines it -- DCL/mem.c is mainline's drivers/char/mem.c and
 *    KernelAA64/string.c stops at memcpy(). drm_edid.c:2130 is the caller:
 *    edid_filter_invalid_blocks() compacts the valid 128-byte blocks toward
 *    the front of the same buffer, so source and destination overlap by
 *    design. A memcpy() in its place would corrupt the block it is still
 *    reading (KernelAA64's memcpy branches on overlap, but the symbol asked
 *    for is memmove and nothing satisfies it) -- the corruption would only
 *    appear for EDIDs that contain an invalid block, i.e. exactly the EDIDs
 *    this function exists to clean up.
 *
 *  - scnprintf() wraps _vsnprintf() (<Log/_print.h>, body in
 *    KernelAA64/Log/_print.c:814). It cannot be a header inline because it
 *    needs va_list plumbing from KernelAA64's own header, and <linux/string.h>
 *    should not pull that into every translation unit; it cannot be a macro
 *    over _snprintf because its whole point is a return value that never
 *    counts characters that were not written. drm_dev_wedged_event()
 *    (drm_drv.c:572, :579) accumulates the return into `len` and then chops a
 *    trailing comma with `event_string[len - 1] = '\0'`; a snprintf-style
 *    would-have-written length would push that chop past the end of the
 *    buffer.
 *
 * No <string.h>/<asm/div64.h> includes: the declarations they carry are
 * compatible with the definitions below, but this file is the single TU where
 * the definitions exist, and the fewer headers that get the chance to turn a
 * name into a macro first -- the trap DCL/linux_lock_shim.c documents -- the
 * better. <stdint.h>/<stddef.h> are Xeneva's own; <linux/kernel.h> is here
 * for E2BIG, which is one of its early error codes (:281); <Log/_print.h> is
 * the declaration of the formatter being wrapped. The native header comes
 * first because <linux/kernel.h> defines `current` and friends as macros
 * (see the include-order note in DCL's other shims).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <Log/_print.h>
#include <linux/kernel.h>	/* E2BIG */

/*
 * __div64_32 -- divide a 64-bit dividend by a 32-bit divisor, leave the
 * quotient in the dividend and return the remainder.
 *
 * mainline's body (lib/div64.c) is a shift-and-subtract loop because the
 * machines that need the function at all are the ones without a 64-bit
 * divide instruction, and the loop is the portable fallback. This target is
 * aarch64: udiv/msub do the whole job, so the hardware division *is* the
 * generic implementation, written the way include/asm-generic/div64.h uses
 * it (`__rem = __div64_32(&(n), __base)` must update n in place).
 *
 * The declaration in <asm/div64.h>:174 has exactly this signature; it does
 * not need to be repeated here for the compiler, but the types are spelled
 * the same so the linker sees the same function.
 */
uint32_t __div64_32(uint64_t* n, uint32_t base)
{
	uint32_t rem = (uint32_t)(*n % base);

	*n /= base;
	return rem;
}

/*
 * memmove -- copy n bytes between possibly-overlapping regions.
 *
 * The direction test is the entire difference from memcpy: when the
 * destination starts inside the source, a forward byte loop overwrites
 * source bytes before they are read, so the copy has to run backwards from
 * the end. Comparing as integers rather than as pointers keeps the overlap
 * test defined even for unrelated objects, which is how drm_edid.c:2130
 * arrives here (dst_block and src_block are two offsets into one EDID
 * buffer, so they are related, but a caller need not be).
 *
 * The early `dest == src` return is not an optimisation: the forward loop
 * would also be correct, but the shrink/expand pair of drm_edid compaction
 * passes identical pointers when a block is already in place, and that is
 * the common case for a well-formed EDID.
 */
void* memmove(void* dest, const void* src, size_t n)
{
	unsigned char* d = (unsigned char*)dest;
	const unsigned char* s = (const unsigned char*)src;
	uintptr_t du = (uintptr_t)dest;
	uintptr_t su = (uintptr_t)src;

	if (d == s || n == 0)
		return dest;

	if (du < su || du - su >= n) {
		/* No overlap, or dest entirely before src: forward copy --
		 * the bytes ahead of the cursor have already been read. */
		while (n--)
			*d++ = *s++;
	} else {
		/* dest starts inside src: copy backwards so the byte at the
		 * far end is read before the write-at-the-front reaches it. */
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}

	return dest;
}

/*
 * scnprintf -- bounded, NUL-terminating formatted output whose return is the
 * number of characters actually stored.
 *
 * _vsnprintf already does the bounded formatting (it memsets the buffer and
 * stops at the caller's size, and its own MAX_STRING_LENGTH cap is above the
 * 32-byte event buffer the one caller uses); what it does not do is
 * scnprintf's return contract. Mainline defines:
 *
 *     scnprintf:  chars written, clamped to size - 1 on truncation
 *     snprintf:   chars that *would* have been written
 *
 * drm_drv.c:579 depends on the clamped form: the return is added to `len`,
 * the next append starts at event_string + len, and event_string[len - 1] is
 * then overwritten with '\0' to drop the trailing comma. With an
 * ungrown length that chop would land inside the last recovery name; with a
 * length past the buffer it would land outside event_string entirely.
 *
 * The NUL at buf[n] is written unconditionally after the clamp, which also
 * repairs the edge where _xeprint's own terminator lands on buf[size] (its
 * output counter is capped at `outputlen`, so a format that exactly fills
 * the buffer can terminate one past the last stored character). It cannot
 * repair a byte already written outside the buffer, but the call sites here
 * never fill it exactly, and a caller that did would get a terminated
 * string back rather than an unterminated one.
 */
int scnprintf(char* buf, size_t size, const char* fmt, ...)
{
	va_list args;
	int n;

	if (size == 0)
		return 0;

	va_start(args, fmt);
	n = _vsnprintf(buf, size, fmt, args);
	va_end(args);

	if (n < 0)
		n = 0;
	if ((size_t)n >= size)
		n = (int)size - 1;
	buf[n] = '\0';

	return n;
}

/*
 * strscpy_pad -- bounded NUL-terminated copy with the destination's tail
 * zero-filled; returns the copied length, or -E2BIG when count is 0 or the
 * source did not fit.
 *
 * The five callers copy a mode/property name into a fixed 32-byte field
 * (drm_property.c:131, :424, :477 and drm_modes.c:2622, :2663), several from
 * a userspace struct whose name field is not guaranteed to carry a NUL
 * inside its 32 bytes. Two consequences follow, and both are why this is not
 * just strscpy plus a memset at the call site:
 *
 *  - the read of src must stop at count - 1, or a name with no terminator in
 *    the ioctl buffer would walk off the end of it -- the loop condition
 *    below is strnlen(src, count - 1)'s guarantee written out;
 *  - the tail must be zeroed on success, because what follows the copy is
 *    strcmp()/printf("%s") on a struct that was not zero-initialised -- the
 *    padding is the difference between a name and the previous contents of
 *    the field.
 *
 * On truncation the copied bytes stay put and the last byte becomes NUL
 * (mainline's sized_strscpy_pad leaves the partial copy in place and does
 * not pad when the copy failed), so userspace still gets a terminated,
 * truncated name rather than an empty one.
 */
ssize_t strscpy_pad(char* dest, const char* src, size_t count)
{
	size_t i;

	if (count == 0)
		return -E2BIG;

	for (i = 0; i + 1 < count && src[i] != '\0'; i++)
		dest[i] = src[i];

	if (src[i] != '\0') {
		/* Truncated: still terminate, so no reader of dest can run
		 * into whatever follows it in the struct. */
		dest[count - 1] = '\0';
		return -E2BIG;
	}

	/* Whole source fit: NUL-terminate it and zero the rest of the
	 * field. `i` is the source length at this point and is what the
	 * return contract asks for, so it is saved before the pad loop. */
	{
		size_t len = i;

		while (i < count)
			dest[i++] = '\0';

		return (ssize_t)len;
	}
}
