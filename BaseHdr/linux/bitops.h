#ifndef __LINUX_BITOPS_H__
#define __LINUX_BITOPS_H__

/*
 * DCL <linux/bitops.h> -- bit arithmetic.
 *
 * tty_buffer.c includes this for one thing: __ALIGN_MASK(), which rounds a
 * requested buffer length up to the next 256-byte boundary
 * (`size = __ALIGN_MASK(size, TTYB_ALIGN_MASK)` at tty_buffer.c:172). That
 * rounding is what lets the free list work at all -- a recycled buffer is only
 * reused when the new request is <= MIN_TTYB_SIZE, and both sides have to
 * agree on the granularity or a 257-byte request silently gets a 256-byte
 * buffer.
 *
 * <linux/kernel.h> already has the same operation spelled __ALIGN_KERNEL_MASK
 * with an extra typeof() cast; __ALIGN_MASK is mainline's name for it, kept
 * here rather than in kernel.h because nothing outside a ported file has ever
 * asked for that spelling.
 *
 * The bit-search helpers used to be deliberately absent, on the grounds that
 * no file in the staged tty/serial set called them. drm_drv.c:574 now does --
 * for_each_set_bit() over the wedge-recovery bitmask in drm_dev_wedged_event()
 * -- so they are here, and the reasoning is restated rather than deleted: a
 * bitmap search still has no caller that would notice how it is implemented,
 * only one that needs the right bit number back. The loops below are a linear
 * walk; mainline's find_next_bit() reads a word at a time and skips whole
 * zero words. That difference is invisible at the one call site, whose
 * bitmap is a single unsigned long, and would stop being invisible the day a
 * driver scans a large mask in a hot path -- at which point the word-wise
 * version belongs here, not a faster loop over the same array.
 */

/*
 * BITS_PER_LONG and DECLARE_BITMAP sit above the kernel.h include, which is
 * not where either would naturally go. kernel.h pulls in a wide subtree
 * (virtio.h, completion.h, wait.h, sched.h) and nodemask_types.h sits at the
 * far end of it, already using DECLARE_BITMAP(bits, MAX_NUMNODES). By the time
 * that chain reaches it this header's own include guard is set, so
 * nodemask_types.h's #include <linux/bitops.h> is a no-op and a definition
 * placed below would not have been seen. Defining them first is what makes
 * that chain parse.
 *
 * For DECLARE_BITMAP that is also the only order that works, not merely one
 * that does: <linux/bitmap.h> includes this header at its top, so a file
 * starting there arrives with DECLARE_BITMAP undefined and bitmap.h's own copy
 * -- mainline's home for it -- not yet reached. It has to live here.
 *
 * BITS_PER_LONG is a fallback only -- <linux/kernel.h> also defines it. Kept
 * in step with kernel.h (sizeof(long)*8, not 64): this target is LLP64, see
 * the note there for what a hardcoded 64 does to the word index and the shift
 * below.
 */
#ifndef BITS_PER_LONG
/* 32 -- the value above, restated as a #if-readable number so the two
 * headers agree token for token when a file includes both (they are
 * otherwise identical and would trip -Wmacro-redefined). */
#define BITS_PER_LONG 32
#endif

/* BITS_TO_LONGS is spelled out rather than called: its definition is below the
 * include above, and this one has to precede it. The formulas have to agree. */
#ifndef DECLARE_BITMAP
#define DECLARE_BITMAP(name, bits) \
	unsigned long name[(bits + BITS_PER_LONG - 1) / BITS_PER_LONG]
#endif

/*
 * fls and fls_long are defined below with the rest of the word-scan family,
 * but declared here, immediately above the kernel.h include, because
 * <linux/log2.h> -- reached through kernel.h:89 -- calls both as it is
 * parsed: fls() in __ilog2_u32() at log2.h:24, fls_long() in
 * __roundup_pow_of_two()/__rounddown_pow_of_two() at log2.h:57/:67. A file
 * that includes this header before kernel.h (drm_drv.c:29 is the one in the
 * DRM set) re-enters this header from log2.h:12 while it is itself parked on
 * that include; the include guard makes the re-entry a no-op, so without
 * these lines log2.h's calls are parsed with no declaration in sight. The
 * calls stay quiet under -Wno-implicit-function-declaration, but the
 * definition below is then a conflicting type for fls_long -- one hard
 * error, in exactly that one object. The declarations cost nothing: same
 * types as the definitions, no code generated.
 */
static inline int fls(unsigned int x);
static inline unsigned int fls_long(unsigned long l);

#include <linux/kernel.h>
/* CHAR_BIT, for BITS_PER_TYPE(). DCL's is <limits.h> at the BaseHdr root:
 * linux/limits.h is the kernel's own limits and carries no CHAR_BIT. */
#include <limits.h>

#ifndef __ALIGN_MASK
#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#endif

/*
 * IS_ALIGNED(x, a) -- "x is a multiple of a". mainline keeps it in
 * <linux/kernel.h> next to ALIGN(); DCL's kernel.h has ALIGN() but not this,
 * and both callers in the DRM set reach bitops.h: drm_dumb_buffers.c:81
 * rejects a driver hw_size_align that is not a page multiple (the size
 * rounding at :86 would otherwise quietly round to the driver's own
 * granularity), drm_file.c:855 stops the KiB unit walk once the byte count
 * stops being a multiple of SZ_1K, so the number printed by drm_fdinfo is
 * never fractional. The cast on `a` is mainline's; both callers pass an int
 * constant, so it changes nothing here, but it is kept so the macro answers
 * like the kernel.h spelling it stands in for when a future caller passes a
 * mask wider than x.
 */
#ifndef IS_ALIGNED
#define IS_ALIGNED(x, a) (((x) & ((typeof(x))(a) - 1)) == 0)
#endif

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

#ifndef BIT_MASK
#define BIT_MASK(nr) (1UL << ((nr) % BITS_PER_LONG))
#endif

#ifndef BITS_TO_LONGS
#define BITS_TO_LONGS(nr) (((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#endif

#ifndef test_bit
#define test_bit(nr, addr) \
	(((unsigned long*)(addr))[(nr) / BITS_PER_LONG] & \
	 (1UL << ((nr) % BITS_PER_LONG)))
#endif

#ifndef set_bit
#define set_bit(nr, addr) \
	(((unsigned long*)(addr))[(nr) / BITS_PER_LONG] |= \
	 (1UL << ((nr) % BITS_PER_LONG)))
#endif

#ifndef clear_bit
#define clear_bit(nr, addr) \
	(((unsigned long*)(addr))[(nr) / BITS_PER_LONG] &= \
	 ~(1UL << ((nr) % BITS_PER_LONG)))
#endif

/*
 * BIT_WORD, __ffs, __set_bit, __test_and_set_bit, __test_and_clear_bit --
 * the __-prefixed base operations, which are a family beside the macros above
 * rather than another spelling of them.
 *
 * mainline's split between the two is atomicity: set_bit() may be atomic on
 * some architectures and __set_bit() explicitly is not, and a caller picks the
 * __ form when it already holds the lock or does not need one. DCL's
 * set_bit() above is already a plain |= with no barrier, so the two are the
 * same operation here -- but they stay two names, because the callers are
 * mainline's (xarray.c:95, :102, :135 and drm_mm.c:478) and a later port that
 * does need the distinction should not find half the family missing.
 *
 * __ffs() is index-of-first-set-bit, and mainline documents the answer as
 * undefined for a word of 0: drm_mm.c:1887 calls it on `last + 1`, which the
 * surrounding loop has already established is non-zero. __builtin_ctzl is
 * what mainline's generic version reaches for as well; `long` is 32 bits on
 * this target and the builtin follows the type, so the width is right.
 *
 * __test_and_*_bit() return the bit's previous state, which is what makes the
 * xarray mark code read as it does -- node_set_mark() answers "was it already
 * set" from the return, not from a second read.
 */
#ifndef BIT_WORD
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#endif

static inline unsigned long __ffs(unsigned long word)
{
	return (unsigned long)__builtin_ctzl(word);
}

static inline void __set_bit(int nr, volatile unsigned long* addr)
{
	addr[BIT_WORD(nr)] |= BIT_MASK(nr);
}

static inline int __test_and_set_bit(int nr, volatile unsigned long* addr)
{
	unsigned long* p = (unsigned long*)addr + BIT_WORD(nr);
	unsigned long old = *p;
	unsigned long mask = BIT_MASK(nr);

	*p = old | mask;
	return (old & mask) != 0;
}

static inline int __test_and_clear_bit(int nr, volatile unsigned long* addr)
{
	unsigned long* p = (unsigned long*)addr + BIT_WORD(nr);
	unsigned long old = *p;
	unsigned long mask = BIT_MASK(nr);

	*p = old & ~mask;
	return (old & mask) != 0;
}

/*
 * __clear_bit and clear_bit_unlock -- the two clears the DRM set calls by
 * name, beside the set/test family above.
 *
 * __clear_bit is the non-atomic member: drm_mm.c:844 drops
 * DRM_MM_NODE_SCANNED_BIT after a scan block is removed, with the scan list
 * already private to the caller. Its body is __set_bit's mirror; written as
 * a typed inline rather than reusing the clear_bit macro so the address
 * keeps the same information __set_bit already gets.
 *
 * clear_bit_unlock is mainline's name for "clear it and let it be seen"
 * when releasing a lock -- drm_mm.c:648 clears DRM_MM_NODE_ALLOCATED_BIT as
 * the node is freed, and any concurrent scanner must not miss that. On this
 * uniprocessor target every bit operation is already a plain
 * read-modify-write with no barrier to order it against, so the two are the
 * same instruction sequence; keeping the name means the vendored call site
 * does not have to be rewritten to say so, and the day this port gains an
 * SMP build the barrier belongs in exactly this one place.
 */
static inline void __clear_bit(int nr, volatile unsigned long* addr)
{
	addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
}

static inline void clear_bit_unlock(int nr, volatile unsigned long* addr)
{
	__clear_bit(nr, addr);
}

/*
 * assign_bit -- set or clear according to a value, in one expression.
 * tty_port.h's flow-control setters are written against it (set_cts_flow,
 * set_check_carrier, set_initialized, ...), all of which take a bool and are
 * called with a literal. Written as a statement-macro rather than a function
 * so `addr` keeps whatever type it arrived with -- tty_port_active() reads
 * through a const pointer and tty_port_set_active() through a mutable one,
 * and a function taking unsigned long* would force a cast on the first.
 */
#ifndef assign_bit
#define assign_bit(nr, addr, val) \
	do { \
		if (val) \
			set_bit((nr), (addr)); \
		else \
			clear_bit((nr), (addr)); \
	} while (0)
#endif

/*
 * The bit-search helpers mainline keeps in lib/find_bit.c. Here they are
 * static inline in the header, because this tree has no lib/find_bit.c and a
 * non-inline definition would need exactly one object file to hold it -- and
 * the file that would need it (drm_drv.c) cannot be edited.
 *
 * Both find_next_* take the bitmap as `const unsigned long *`, which is the
 * type drm_dev_wedged_event() passes (`&method`, an unsigned long). test_bit
 * is the macro above and casts away the const itself, so the loops below need
 * no cast of their own.
 */
#ifndef BITS_PER_TYPE
#define BITS_PER_TYPE(type) (sizeof(type) * CHAR_BIT)
#endif

static inline unsigned long find_next_bit(const unsigned long *addr,
					  unsigned long size,
					  unsigned long offset)
{
	unsigned long i;

	for (i = offset; i < size; i++)
		if (test_bit(i, addr))
			return i;

	return size;
}

static inline unsigned long find_next_zero_bit(const unsigned long *addr,
					       unsigned long size,
					       unsigned long offset)
{
	unsigned long i;

	for (i = offset; i < size; i++)
		if (!test_bit(i, addr))
			return i;

	return size;
}

static inline unsigned long find_first_bit(const unsigned long *addr,
					   unsigned long size)
{
	return find_next_bit(addr, size, 0);
}

static inline unsigned long find_first_zero_bit(const unsigned long *addr,
						unsigned long size)
{
	return find_next_zero_bit(addr, size, 0);
}

/*
 * ffs, fls, fls_long, hweight32/hweight64 -- word scans and population
 * counts, the second family of bit arithmetic the DRM set reached for.
 *
 * mainline gets ffs and fls from <asm-generic/bitops/>; both reduce to the
 * two builtins used here. __builtin_ffs() answers 0 for 0, which is ffs()'s
 * contract; fls() must answer 0 too and __builtin_clz() is undefined for 0,
 * hence the ternary in front of it. drm_blend.c:352 turns the lowest set
 * rotation bit into an array index (ffs of a mask), and
 * drm_client_modeset.c:191 asks for the highest set rotation bit (fls).
 *
 * fls_long is fls() for this target's four-byte long, and the 64-bit arm is
 * removed by the preprocessor rather than left behind a runtime
 * `if (BITS_PER_LONG == 64)`: both arms of such a condition would have to
 * compile, and DCL carries no fls64(). dma-resv.c:335 (the fence-array
 * growth estimate) is the only caller.
 *
 * hweight32/hweight64 are the popcounts. drm_crtc.c:991 and
 * drm_crtc_helper.c:507 ask whether a crtc can be driven by more than one
 * encoder, drm_connector.c:2444/:3351 count encoders and scaling modes, and
 * drm_property.c:218 sizes a bitmask property by how many bits its
 * validation mask has. The builtins follow the argument type, so the 32/64
 * split is exactly what each name promises.
 */
#ifndef ffs
#define ffs(x) __builtin_ffs(x)
#endif

static inline int fls(unsigned int x)
{
	return x ? 32 - __builtin_clz(x) : 0;
}

static inline unsigned int fls_long(unsigned long l)
{
#if BITS_PER_LONG == 64
	return fls64(l);	/* DCL has no fls64(); unreachable at 32 */
#else
	return (unsigned int)fls(l);
#endif
}

#ifndef hweight32
#define hweight32(w) __builtin_popcount(w)
#endif

#ifndef hweight64
#define hweight64(w) __builtin_popcountll(w)
#endif

#ifndef change_bit
#define change_bit(nr, addr) __change_bit((nr), (addr))
#endif
#ifndef __change_bit
static inline void __change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & (BITS_PER_LONG - 1));
	unsigned long *p = (unsigned long *)addr + (nr / BITS_PER_LONG);
	*p ^= mask;
}
#endif

#ifndef bitmap_weight
static inline unsigned int bitmap_weight(const unsigned long *src, unsigned int nbits)
{
	unsigned int w = 0;
	for (unsigned int i = 0; i < nbits; i++)
		w += test_bit(i, src) ? 1 : 0;
	return w;
}
#endif
#ifndef bitmap_subset
static inline bool bitmap_subset(const unsigned long *src1,
				 const unsigned long *src2, unsigned int nbits)
{
	for (unsigned int i = 0; i < nbits; i++)
		if (test_bit(i, src1) && !test_bit(i, src2))
			return false;
	return true;
}
#endif

#ifndef for_each_set_bit_from
#define for_each_set_bit_from(bit, addr, size) \
	for (; (bit) = find_next_bit((addr), (size), (bit)), \
	       (bit) < (size); (bit)++)
#endif

#ifndef for_each_set_bit
#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size)); \
	     (bit) < (size); \
	     (bit) = find_next_bit((addr), (size), (bit) + 1))
#endif

#ifndef for_each_clear_bit
#define for_each_clear_bit(bit, addr, size) \
	for ((bit) = find_first_zero_bit((addr), (size)); \
	     (bit) < (size); \
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))
#endif

/*
 * Two additions at the very bottom -- reachability, not anything bitops.h
 * itself uses, which is why they sit after every definition above rather
 * than at the top with the other includes.
 *
 * <linux/bug.h> is how every WARN_ONCE() user reaches the macro. bug.h
 * already arrives directly in most of them, but drm_cache.c,
 * drm_plane_helper.c and drm_probe_helper.c never include it, and WARN_ONCE
 * has to be a header macro rather than a DCL shim: the whole point is
 * splicing fmt and __VA_ARGS__ into the report at the call site. bitops.h is
 * a header all three already include, and every other WARN_ONCE user reaches
 * it too, so one include here covers all fifteen objects. bug.h's own
 * <asm/bug.h> walks on to <linux/kernel.h>, whose guard the include at the
 * top of this file already spent; by the time this line runs that is a
 * no-op.
 *
 * le32_to_cpup is drm_format_helper.c's pointer load (drm_format_helper.c:
 * 265-268 and the xfrm line helpers below them). mainline gets it from
 * <linux/kernel.h>:38 including <asm/byteorder.h>; DCL's kernel.h does not
 * carry that include, and drm_format_helper.c reaches the byteorder layer
 * through nothing else. Pulling <asm/byteorder.h> in here would also work,
 * but it would put generic.h's object-like le16_to_cpu()/cpu_to_le16()
 * definitions in front of the function-like ones <linux/types.h>:129
 * already publishes, turning six -Wmacro-redefined warnings into a line in
 * every object this tree builds. Only the one name is needed, so only the
 * one name is defined -- spelled exactly as generic.h:101 spells it, so a
 * file that reaches generic.h afterwards sees an identical redefinition and
 * no warning. The uapi header supplies the static inline __le32_to_cpup()
 * the alias points at, and is already part of every include chain that saw
 * generic.h; elsewhere it is one more include guard.
 */
#include <linux/bug.h>

#ifndef le32_to_cpup
#include <uapi/linux/byteorder/little_endian.h>	/* __le32_to_cpup */
#define le32_to_cpup __le32_to_cpup
#endif

#endif /* __LINUX_BITOPS_H__ */
