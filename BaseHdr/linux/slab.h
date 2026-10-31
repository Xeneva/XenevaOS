#ifndef __LINUX_SLAB_H__
#define __LINUX_SLAB_H__

#include <Mm/kmalloc.h>
#include <Mm/vmmngr.h>	/* PAGE_SIZE -- tty_buffer.c computes TTY_BUFFER_PAGE from it */
#include <linux/kernel.h>	/* gfp_t, uintptr_t, E2BIG */
#include <linux/gfp_types.h>	/* __GFP_ZERO -- the one gfp bit this
				 * header does not drop, read by
				 * dcl_kmalloc_flags() below */
#include <linux/overflow.h>	/* struct_size/flex_array_size -- mainline's
				 * <linux/slab.h> takes it at :18, and
				 * lib/dma-resv.c's dma_resv_list_alloc()
				 * sizes its fence array with struct_size().
				 * DCL's copy of this header had no such
				 * include, so overflow.h was reachable from
				 * nowhere in the tree and struct_size simply
				 * was not a macro in that translation unit. */
#include <linux/sprintf.h>	/* _vsnprintf, for kasprintf below */
#include <linux/string.h>	/* memcpy -- kmemdup()'s copy (added below) */
/*
 * asm/cache.h for ARCH_DMA_MINALIGN, and the reason is the same one the
 * overflow.h comment above gives: mainline reaches it as
 * asm/cache.h -> linux/cache.h, and in this tree linux/cache.h is included by
 * exactly four headers (debug_locks.h, mmzone.h, dma-mapping.h, itself), none
 * of which drm_managed.c includes. So the definition sat in asm/cache.h with
 * no includer reaching it, and drm_managed.c:56's
 * `u8 __aligned(ARCH_DMA_MINALIGN) data[]` failed with the identifier
 * undeclared. slab.h is the path rather than the new home -- the definition
 * stays in asm/cache.h where mainline puts it -- and it is a reasonable one
 * because ARCH_DMA_MINALIGN is a statement about what an allocator may
 * promise, which is what this header already owns in ARCH_KMALLOC_MINALIGN.
 */
#include <asm/cache.h>		/* ARCH_DMA_MINALIGN */
/*
 * kdev_t.h for old_encode_dev(), which is not an allocator dependency: the
 * two debug messages drm_ioctl.c:884/:923 build from a dev_t call it, and
 * their translation unit reaches neither fs.h nor cdev.h, the two headers
 * that carry kdev_t.h in mainline and in this tree. It does reach this one
 * (drm_crtc.h -> hdmi.h -> device.h -> devres.h), so the include is made
 * here rather than restating a definition that already exists in exactly
 * one place. Where the pinned fs.h has already spelled MAJOR/MINOR/MKDEV,
 * kdev_t.h's spellings of the same three re-expand to the same values
 * (MINORBITS is 20 and fs.h shifts by 20), so that case is
 * -Wmacro-redefined and nothing more.
 */
#include <linux/kdev_t.h>

#define KMALLOC_SHIFT_HIGH  22
#define KMALLOC_MIN_SIZE    8

/*
 * ARCH_KMALLOC_MINALIGN -- the alignment every kmalloc() return already has,
 * from mainline's <linux/slab.h>. 8, the alignment of the widest basic type,
 * because Xeneva's allocator hands out TLSF blocks that are at least 8-byte
 * aligned (Mm/kmalloc.h); nothing here can promise a cache line.
 *
 * One caller: drm_format_helper.c:190 rounds its scratch-buffer length up to
 * it before aligning the per-plane offsets inside that buffer, so the number
 * is load-bearing for the *layout* of the buffer rather than only for whether
 * an allocation is legal. Under-promising it (say 1) would leave a plane
 * starting at an unaligned offset; over-promising it (a cache line) would
 * waste up to 56 bytes per scratch buffer and would be a claim the allocator
 * does not actually make.
 */
#ifndef ARCH_KMALLOC_MINALIGN
#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long long)
#endif

struct kmem_cache;

/*
 * mainline: kmalloc(size, gfp). Xeneva's allocator takes the size only and
 * DCL drops the flags -- GFP_KERNEL is 0 here -- with one exception:
 * __GFP_ZERO selects the zeroing path. It cannot be dropped, because
 * vendored code reaches the zeroing through the flag and nowhere else:
 * drmm_kzalloc() calls drmm_kmalloc(dev, size, gfp | __GFP_ZERO), and
 * drm_managed.c's alloc_dr() builds the whole drmres block -- header plus
 * payload -- with one kmalloc_node_track_caller(tot_size, gfp, nid). Ignore
 * the flag and drmm_kzalloc() hands back whatever TLSF held last: vkms's
 * plane came back with a garbage properties.count, and the first
 * drm_object_attach_property() turned it into a wild store.
 * dcl_kmalloc_flags() below is the dispatch that reads it.
 *
 * Zero-size requests are the one disagreement vendored code trips over:
 * mainline's kmalloc(0) -- and kzalloc(0), kcalloc(0, ...) -- answer with
 * ZERO_SIZE_PTR, which is non-NULL, and callers are written for that.
 * drm_atomic_commit_init() is the example that found this:
 *
 *     state->colorops = kzalloc_objs(*state->colorops, num_colorop);
 *     if (!state->colorops)
 *             goto fail;
 *
 * and a driver with no color pipelines reports num_colorop == 0 -- vkms
 * does -- so a NULL here becomes -ENOMEM on every atomic commit before the
 * ioctl has looked at anything the caller sent.  The native allocators
 * refuse zero (Mm/kmalloc.c: both return NULL for an empty request, kmalloc
 * on `!size`, kcalloc on either factor), so the request is rounded up to
 * one byte rather than special-cased: the pointer stays real, zeroed where
 * the name promises it, and freeable -- none of which ZERO_SIZE_PTR would
 * be here, since kfree() expects a TLSF block.
 *
 * The helper below is expanded at a point where the macro does not exist
 * yet, so its body resolves to the native one-argument kmalloc() from
 * Mm/kmalloc.h; no recursion, and native DCL files that include
 * Mm/kmalloc.h directly keep calling the plain function.
 */
static inline void* dcl_kmalloc(unsigned int size) {
	return kmalloc(size ? size : 1);
}

/* defined with the rest of the kzalloc family further down */
static inline void* dcl_kzalloc(unsigned int size);

/* __GFP_ZERO -> the zeroing route, everything else -> a plain block. This is
 * the one GFP bit that has to be read rather than dropped; the block above
 * records the drmm_kzalloc() path that made that true. */
static inline void* dcl_kmalloc_flags(unsigned int size, gfp_t flags) {
	if (flags & __GFP_ZERO)
		return dcl_kzalloc(size);
	return dcl_kmalloc(size);
}

#define kmalloc(size, flags) dcl_kmalloc_flags(size, flags)

/*
 * kcalloc(n, size, flags) -- the same shape as kmalloc above, except DCL's
 * native kcalloc() (Mm/kmalloc.h) takes two arguments and mainline's takes
 * three, and the two cannot both be named `kcalloc`.
 *
 * The trailing `...` rather than a named third parameter is the point: this
 * one macro has to accept both spellings.  BaseHdr's own dcl_kzalloc() above
 * calls `kcalloc(1, size)` and every native caller of Mm/kmalloc.h still
 * does, while Vendored/lib/string_helpers.c:768 writes
 * `kcalloc(n, sizeof(char *), gfp)` with a flag word DCL drops anyway.  A
 * named third parameter would break the two-argument callers, which is the
 * opposite trade to the one that works.
 *
 * Like dcl_kmalloc, dcl_kcalloc is defined before the macro exists so that
 * its body resolves to the native two-argument function rather than to
 * itself; the comment at the top of this header is the same reasoning.
 *
 * An empty product (either factor 0) takes the same one-byte route the
 * kmalloc block above explains: mainline answers kcalloc(0, ...) with
 * ZERO_SIZE_PTR, the native allocator answers NULL, and vendored callers
 * check for the former.
 */
static inline void* dcl_kcalloc(size_t n, size_t size) {
	if (!n || !size)
		return kcalloc(1, 1);
	return kcalloc(n, size);
}

#define kcalloc(n, size, ...) dcl_kcalloc((n), (size))

/*
 * krealloc(ptr, size, flags) -- the realloc shape of the same problem.
 * DCL's native krealloc() (Mm/kmalloc.h) takes two arguments, mainline's
 * takes three, and the third is dropped for the same reason the others are:
 * TLSF ignores gfp flags entirely.
 *
 * The variadic third parameter again, so that the native two-argument
 * spelling keeps working -- it is the only spelling anywhere in the tree
 * today besides the vendored three-argument ones (drm_atomic.c:1005,
 * drm_edid.c:2146, drm_format_helper.c:86 and others), so both have to
 * compile in the same header.
 *
 * The size is taken as size_t and narrowed to the native unsigned int.
 * Every vendored caller passes a value it just computed as
 * `count * sizeof(*item)`, so the narrowing is a truncation risk that
 * already exists on the native side (kmalloc() takes unsigned int too) and
 * is not widened here: making just this one entry point 64-bit would let a
 * caller believe the allocation was sized faithfully when the allocator
 * underneath still truncates.  Fixing that means changing the native
 * allocator's signature, which is a separate decision.
 */
static inline void* dcl_krealloc(void* ptr, size_t size) {
	return krealloc(ptr, (unsigned int)size);
}

#define krealloc(ptr, size, ...) dcl_krealloc((ptr), (size))

/*
 * krealloc_array(ptr, n, size, flags) -- mainline's: an overflow-checked
 * n * size, then krealloc().  The check is the whole point of the helper --
 * drm_atomic.c:1300 and dma-resv.c:591 both grow an array whose length
 * comes from connector/fence counts, and a wrapped product would allocate
 * a small block and then be filled as a large one.  Mainline routes it
 * through array_size(); DCL has that in <linux/overflow.h>, included above,
 * so the multiplication is exactly mainline's and not a re-derivation.
 */
#define krealloc_array(ptr, n, size, flags) \
	dcl_krealloc((ptr), array_size((n), (size)))

/*
 * kmalloc_size_roundup(size) -- mainline's is an extern (slab.h:1463) with
 * its body in mm/slab_common.c, and the one caller here is
 * dma-resv.c:104:
 *
 *     size = kmalloc_size_roundup(struct_size(list, table, max_fences));
 *     list = kmalloc(size, GFP_KERNEL);
 *     list->max_fences = (size - offsetof(typeof(*list), table)) /
 *                        sizeof(*list->table);
 *
 * so `size` is used twice: as the request, and then -- by subtracting the
 * header -- as the basis for how many fence pointers fit.  The contract
 * mainline is satisfying is "the allocator will actually use at least this
 * many bytes", which is what makes the second line safe.
 *
 * DCL returns the request unchanged rather than pretending to a bucket
 * table: TLSF rounds internally and exposes no way to ask a size what it
 * became, and inventing a rounding here would be a number that looks like
 * allocator knowledge this tree does not have.  Unchanged satisfies "at
 * least this many" exactly, and the capacity arithmetic that follows comes
 * out at exactly the struct_size requested -- the extra room TLSF may hand
 * back is simply not claimed.  Under-reporting the capacity wastes a few
 * slots; over-reporting it would write past the block.
 */
static inline size_t kmalloc_size_roundup(size_t size)
{
	return size;
}

/*
 * kzalloc(size, flags) -- allocate and zero.
 *
 * DCL needs this as its own thing because the native allocator does not zero.
 * kmalloc() hands back whatever the TLSF free list last held; only kcalloc()
 * (KernelAA64/Mm/kmalloc.c) memsets, and it does so because *it* is the one
 * promising zeroed memory. Going through kcalloc(1, size) rather than
 * kmalloc()+memset() means the zeroing is one native call with one size
 * computation instead of an allocate-then-sweep, and it is the same code
 * path that already carries the "this must be zero" contract.
 *
 * This matters more than it looks: virtio-rng.c does
 *
 *     vi = kzalloc_obj(struct virtrng_info);
 *
 * and then reads vi->data_avail, vi->data_idx, vi->hwrng_register_done and
 * vi->hwrng_removed before ever writing them. With a non-zeroing kmalloc()
 * that is a garbage entropy-buffer length, a random memcpy bound, and a
 * driver that may skip or repeat its own cleanup -- all invisible until the
 * numbers happen to be large.
 *
 * The flags argument is accepted and dropped: zeroing here is unconditional
 * rather than opt-in, so the caller cannot ask for less than the name
 * promises. kmalloc() is the one that has to read __GFP_ZERO, and its block
 * above records why.
 *
 * size 0 gets one byte rather than the native NULL, for the contract reason
 * the kmalloc block above records -- kzalloc_objs() over a zero count is how
 * that path was found, and it arrives here.
 */
static inline void* dcl_kzalloc(unsigned int size) {
	return kcalloc(1, size ? size : 1);
}

#define kzalloc(size, flags) dcl_kzalloc(size)

/*
 * kzalloc_obj(TYPE, [flags]) -- mainline's "one object of this type, zeroed":
 *
 *     vi = kzalloc_obj(struct virtrng_info);
 *
 * mainline's slab.h spells it `__alloc_objs(kzalloc, default_gfp(__VA_ARGS__),
 * typeof(TYPE), 1)`, i.e. sizeof(typeof(TYPE)) bytes with room for exactly one.
 *
 * The trailing flags are accepted and dropped, matching kmalloc_obj() below
 * and for the same reason. This used to be a single named parameter on the
 * argument that a silent no-op flag is worse than a syntax error: a caller
 * writing `kzalloc_obj(T, GFP_KERNEL)` would be told at once that the flag
 * went nowhere. That trade no longer holds, because the caller that turned
 * up -- drm_modeset_lock.c:151, `kzalloc_obj(*ctx, GFP_KERNEL | __GFP_NOFAIL)`
 * -- lives in Vendored/, which has to stay byte-identical to v7.2 and so
 * cannot be rewritten to the one-argument spelling. The error would be
 * correct and unactionable, which is worse than either alternative.
 *
 * Note the argument is a dereference at that call site, `*ctx` rather than a
 * type name; sizeof() takes both, so nothing special is needed for it.
 *
 * __GFP_NOFAIL in particular deserves its mention: it means the allocation
 * may not fail, which DCL's TLSF heap cannot promise for any gfp value. The
 * call site is guarded by an earlier size check that makes the failure
 * unreachable in practice, so the flag is satisfied by construction rather
 * than by the allocator. Nothing else in gfp_t is honoured either.
 */
DEFINE_FREE(kfree, void*, kfree(_T))

#define kzalloc_obj(type, ...) dcl_kzalloc((unsigned int)sizeof(type))

/*
 * kmalloc_obj(type, ...) -- the uninitialised twin of kzalloc_obj, and the
 * two shapes the drivers actually write:
 *
 *     p->em485 = kmalloc_obj(struct uart_8250_em485, GFP_ATOMIC);
 *                                     (8250_port.c:560)
 *     port = kmalloc_obj(struct port);
 *                                  (virtio_console.c:1328, :1974)
 *
 * kzalloc_obj() takes the type alone; kmalloc_obj() historically took flags
 * as a second parameter, which is why the trailing parameter is `...` and not
 * a named one: mainline's spelling varies by call site, and a macro that
 * insists on exactly two rejects the one-argument caller with "too few
 * arguments provided to function-like macro invocation" -- an error about the
 * macro's arity that says nothing about which side is wrong.
 *
 * The flags are swallowed rather than honoured for the reason the block below
 * gives at length: GFP_ATOMIC means "must not sleep", DCL's allocator does
 * not block on anything -- it is a bump over the TLSF heap -- so the guarantee
 * GFP_ATOMIC exists to make is satisfied whether or not the flag is looked at.
 * Nothing else in gfp_t is honoured either (see the flags comment above
 * kmalloc_flex: an unrecognised gfp bit should be a compile error there
 * rather than a silently different allocation -- and with `...` it is now
 * silently accepted, which is the one thing this change trades away).
 */
#define kmalloc_obj(type, ...) dcl_kmalloc((unsigned int)sizeof(type))

/*
 * kmalloc_flex(p, member, count, flags) -- allocate "struct + count of the
 * flexible array".
 *
 * The call site is tty_buffer.c:187:
 *
 *     p = kmalloc_flex(*p, data, 2 * size, GFP_ATOMIC | __GFP_NOWARN);
 *
 * -- the first argument is a *dereference*, because mainline's macro (slab.h
 * v7.2) is `__alloc_flex(kmalloc, gfp, typeof(VAR_OR_TYPE), FAM, COUNT)`,
 * i.e. `typeof(*p)` -- the struct, and the size follows from that type. The
 * spelling below has to reproduce that, and the trap is where the parameter
 * sits. Writing `sizeof(*(p))` substitutes `*p` into `*(p)`, giving
 * `sizeof(*(*p))` -- a dereference of a struct -- which is precisely the
 * "indirection requires pointer operand" error clang reported on the first
 * build. So the parameter appears bare: `typeof(p)` with the argument tokens
 * `*p` becomes `typeof(*p)`, the struct itself -- and a struct is not a
 * pointer, so `(typeof(p))0` fails again ("used type 'typeof (*p)' (aka
 * 'struct tty_buffer') where arithmetic or pointer type is required"). The
 * null pointer comes from `typeof(&p)`: `&*p` is `struct tty_buffer *`, and
 * `((typeof(&p))0)->data` is then an ordinary member access through a null
 * pointer, unevaluated, giving sizeof(u8) == 1.
 *
 * The count is in *elements of the flexible member*, and the element size
 * comes from the member rather than a hardcoded 1, so a change to `data[]`'s
 * type cannot silently mis-size every allocation.
 *
/*
 * The flags argument is accepted and ignored, and it is now `...` rather than
 * a named fourth parameter: virtio_console.c:411 writes the three-argument
 * form -- `kmalloc_flex(*buf, sg, pages)` -- because mainline's macro dropped
 * its gfp argument, while tty_buffer.c:187 still writes the four-argument
 * `kmalloc_flex(*p, data, 2 * size, GFP_ATOMIC | __GFP_NOWARN)`. A macro that
 * insists on one count of parameters rejects the other caller with "too few
 * arguments provided to function-like macro invocation", an error about
 * arity rather than about anything either caller did wrong. Both forms
 * substitute the same expansion; the swalled arguments are the GFP flags,
 * which are documentation in DCL and not policy (see the note above).
 *
 * This is where GFP_ATOMIC | __GFP_NOWARN from tty_buffer.c lands, and both
 * are advisory here. A failed allocation still returns NULL -- tty_buffer_alloc()
 * checks for it -- so the "don't warn on failure" half of __GFP_NOWARN is moot:
 * DCL's allocator does not warn.
 *
 * __GFP_NOWARN exists as a name only so that expression parses. GFP flags are
 * documentation in DCL, not policy, except for __GFP_ZERO: <linux/gfp_types.h>
 * comes in at the top of this header, so its real bit is defined by the time
 * this fallback is reached (and skipped) -- dcl_kmalloc_flags() is the reader.
 */
#ifndef __GFP_NOWARN
#define __GFP_NOWARN  0
#endif
#ifndef __GFP_ZERO
#define __GFP_ZERO    0
#endif
#ifndef __GFP_RETRY_MAYFAIL
#define __GFP_RETRY_MAYFAIL 0
#endif

#define kmalloc_flex(p, member, count, ...)				\
	dcl_kmalloc((unsigned int)(sizeof(typeof(p)) +			\
		    sizeof(*((typeof(&p))0)->member) * (size_t)(count)))

/*
 * kzalloc_flex(p, member, count) -- kmalloc_flex() zeroed.  Mainline spells
 * both as the same __alloc_flex() over a different allocator (slab.h:1148 and
 * :1156), and this pair is the same split: dcl_kzalloc() is dcl_kmalloc()
 * with the zeroing, so the only difference is which one the expansion calls.
 * The `...` is mainline's gfp slot, kept for the same reason kmalloc_flex
 * keeps it -- a caller that writes the four-argument form should still
 * compile, and the flags are documentation here.
 *
 * One caller: dma-fence-array.c:182, `kzalloc_flex(*array, callbacks,
 * num_fences)` at the tail of dma_fence_array_alloc(), sizing the
 * callbacks[] flexible array struct dma_fence_array ends in.  The
 * `(typeof(&p))0` spelling is what makes that call work: `p` is the
 * *expression* `*array`, so &p is the address of a local pointer and the
 * member lookup has the struct type to resolve against.
 */
#define kzalloc_flex(p, member, count, ...)				\
	dcl_kzalloc((unsigned int)(sizeof(typeof(p)) +			\
		    sizeof(*((typeof(&p))0)->member) * (size_t)(count)))


/*
 * kzalloc_objs(type, count) -- an array of `count` objects, zeroed.
 *
 * serial_core.c wants it twice: kzalloc_objs(struct uart_state, drv->nr) at
 * :2737 to hold a driver's port states, and
 * kzalloc_objs(*uport->tty_groups, num_groups) at :3109 for the attribute
 * groups the tty device exposes.  Both are `struct`-or-expression first, count
 * second, which is why this is not kzalloc_obj() with an extra argument
 * bolted on: kmalloc_obj takes flags there, kzalloc_obj takes nothing, and a
 * single name with a different second parameter would be a trap.  Separate
 * name, and the plural is mainline's own distinction (kcalloc_array ->
 * kcalloc).
 *
 * The cast to unsigned int matches what the rest of this header hands
 * dcl_kzalloc; a count that overflows 32 bits of byte length would wrap here
 * rather than fail, and the only counts in reach are the number of UART ports
 * (small) and their attribute groups (smaller).
 */
#define kzalloc_objs(type, count) \
	dcl_kzalloc((unsigned int)(sizeof(type) * (unsigned int)(count)))

/*
 * kmalloc_objs(type, count) -- the uninitialised twin, same two-argument
 * shape as kzalloc_objs() above.
 *
 * virtio_console.c is the first caller: :1820 `vqs = kmalloc_objs(struct
 * virtqueue *, nr_queues);` and its three siblings at :1821-1823, each
 * allocating an array of pointers. The first argument is a *type* here --
 * `struct virtqueue *` -- and sizeof() of a pointer type is exactly what an
 * array of those pointers needs, so the expansion is the same
 * sizeof * count as kzalloc_objs without the zeroing.
 *
 * Named separately rather than as kzalloc_objs with a flag bolted on, for the
 * reason that block gives: two callers of two names cannot disagree about
 * whether the memory comes back zeroed, whereas one name with a third
 * parameter could be read either way at the call site.
 */
#define kmalloc_objs(type, count) \
	dcl_kmalloc((unsigned int)(sizeof(type) * (unsigned int)(count)))

/*
 * kvmalloc_objs(type, count) -- kmalloc_objects again, under the name mainline
 * uses when the size is not known to be small enough for kmalloc().
 *
 * mainline's kvmalloc() tries kmalloc() and falls back to vmalloc() above a
 * page or so; DCL has no vmalloc, so the fallback is not a fallback but a
 * different allocator with a different failure mode, and taking it here would
 * mean deciding a threshold nothing has measured. It expands to the same
 * dcl_kmalloc() as kmalloc_objs, which means the answer to "what happens with
 * a large npages" is "the same thing that happens with a large count today".
 *
 * One caller: drm_gem.c:687's `pages = kvmalloc_objs(struct page *, npages)`,
 * the array drm_gem_get_pages builds so it can walk a GEM object's pages one
 * at a time. Undefined, that line reads as a call to a function of unknown
 * type whose first argument is a type name, and clang reports "expected
 * expression" at the comma inside `struct page *` -- the message names the
 * comma, not the macro.
 */
#define kvmalloc_objs(type, count) \
	dcl_kmalloc((unsigned int)(sizeof(type) * (unsigned int)(count)))

/*
 * kvzalloc_objs(type, count) -- the zeroing spelling, added with the
 * virtio-gpu port: virtgpu_submit.c:109 and :200 size their syncobj arrays
 * with it. mainline's v-named pair differs only in the fallback both DCL
 * spellings share, so this expands to kzalloc_objs() and the overflow note
 * there applies here too.
 */
#define kvzalloc_objs(type, count) \
	kzalloc_objs(type, count)

/*
 * The rest of the kvmalloc family and the two string helpers, as functions
 * rather than more macros: a vendored caller names each of them as a call --
 * vkms_composer.c:568/:574 (kvmalloc), drm_property.c:565 (kvzalloc),
 * drm_gem.c:728 (kvfree), drm_managed.c:134 (ksize), vkms_config.c:19 and
 * drm_managed.c:154/:239 (kstrdup_const/kfree_const). Nothing declared them,
 * so those call sites compiled as implicit declarations returning int, which
 * truncates a 64-bit pointer to its low half -- the definitions in
 * DCL/linux_alloc_shim.c fix the link, but the declarations here are what
 * make the calls type-correct.
 *
 * mainline puts kstrdup_const()/kfree_const() in <linux/string.h>; DCL's
 * string.h is memcpy-and-friends only, and this header already owns the
 * string half of the allocation surface (kstrdup(), kmemdup(),
 * memdup_user_nul()), so they land beside those.
 */
void *kvmalloc(size_t size, gfp_t flags);
void *kvzalloc(size_t size, gfp_t flags);
void kvfree(const void *addr);
size_t ksize(const void *objp);
const char *kstrdup_const(const char *s, gfp_t gfp);
void kfree_const(const void *x);

/*
 * kmalloc_array(n, size, flags) -- mainline's overflow-checked array
 * allocation (v7.2 slab.h:1202). The check is the whole point, as with
 * krealloc_array() above: drm_plane.c:402/:427 sizes a format or modifier
 * table from a drm ioctl count, and a wrapped product would allocate a small
 * block and then be filled as a large one. array_size() would do the same
 * check, but mainline spells this one with check_mul_overflow() and the NULL
 * answer on overflow, which is what the callers test for.
 */
static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;
	return dcl_kmalloc_flags((unsigned int)bytes, flags);
}

/*
 * kvmalloc_array(n, size, flags) -- the v-named spelling of the same
 * overflow-checked array allocation, added with the virtio-gpu port: the
 * submit path sizes its syncobj arrays with it. mainline picks kmalloc and
 * falls back to vmalloc above a threshold; DCL's kvmalloc family has no
 * vmalloc to fall back to -- see the note over kvmalloc_objs() -- so the
 * body is kmalloc_array()'s under the name its caller names.
 */
static inline void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;
	return dcl_kmalloc_flags((unsigned int)bytes, flags);
}

/*
 * kmalloc_node_track_caller(size, flags, node) -- kmalloc() with a NUMA node
 * and caller tracking, both of which this tree absorbs. One TLSF heap means
 * there are no nodes to choose between, and the tracking is slab leak
 * accounting, which DCL does not do (its allocator keeps its own statistics).
 * The caller is drm_managed.c:97, which builds a drmres header plus its
 * flexible data[] in one block and would otherwise get no prototype at all.
 */
static inline void *kmalloc_node_track_caller(size_t size, gfp_t flags, int node)
{
	(void)node;
	return dcl_kmalloc_flags((unsigned int)size, flags);
}

/*
 * memdup_array_user()/vmemdup_array_user() -- mainline's string.h inlines
 * over memdup_user()/vmemdup_user(), declared here for the same reachability
 * reason as the two string helpers above (drm_lease.c:512, drm_gem.c:849).
 * Bodies in DCL/linux_alloc_shim.c.
 */
void *memdup_array_user(const void __user *src, size_t n, size_t size);
void *vmemdup_array_user(const void __user *src, size_t n, size_t size);

/*
 * memdup_user()/vmemdup_user() -- the pair those inlines are *over*,
 * declared here for the same reachability reason as they are. mainline
 * declares the pair in <linux/string.h>; DCL's string.h is
 * memcpy-and-friends only, and this header already owns the string half of
 * the allocation surface. The virtio-gpu ioctl blob paths and the submit
 * queue name them directly, and neither had a definition. Bodies in
 * DCL/linux_alloc_shim.c beside the array forms; IS_ERR() answers because
 * the callers check with IS_ERR().
 */
void *memdup_user(const void __user *src, size_t len);
void *vmemdup_user(const void __user *src, size_t len);

/*
 * kmemdup(src, len, gfp) -- copy `len` bytes into a fresh allocation.
 *
 * mainline declares it in <linux/slab.h> too (kernel: mm/util.c), so this is
 * its home and not an import. One caller, virtio_console.c:1118:
 *
 *     data = kmemdup(buf, count, GFP_ATOMIC);
 *     if (!data)
 *         return -ENOMEM;
 *
 * which needs exactly the contract mainline gives: NULL on failure, and the
 * failure *checked for*, which is why the body must not silently fall back to
 * anything. The gfp flag is swallowed like every other one in this header --
 * see the note above kmalloc_obj -- and memcpy comes from <linux/string.h>,
 * which this header now includes for it (kernel.h names memcpy but only
 * commentates on where mainline gets it from).
 *
 * size_t length, unsigned int size: the cast matches what the rest of this
 * header hands the allocator, and a length beyond 4 GB would wrap here rather
 * than fail. The only caller's count is a virtio-console control message.
 */
/*
 * struct kmem_cache -- mainline's per-cache descriptor, cut to the one
 * field its callers read here.  DCL allocates from one TLSF heap whether
 * or not the caller named a cache, so the descriptor carries only the
 * object size kmem_cache_alloc_lru() needs to hand back.  The only cache
 * in reach of this tree is radix-tree.h's extern radix_tree_node_cachep,
 * whose size is set in DCL/drm_stubs.c.
 */
struct kmem_cache {
	unsigned int size;
};

/*
 * kmem_cache_alloc_lru(cache, lru, gfp) -- allocate from `cache`.  The lru
 * node mainline tracks the allocation against is dropped with every other
 * gfp flag here; see the flags note at the top of this header.
 */
static inline void* kmem_cache_alloc_lru(struct kmem_cache* cache,
					 const void* lru, gfp_t gfp)
{
	(void)lru;
	return kmalloc(cache->size, gfp);
}

/*
 * DCL delta: the cache lifecycle virtio-gpu's vbuf allocator drives --
 * virtgpu_vq.c:75 creates the cache, :101/:208/:628 allocate from it,
 * :208/:89 free and :83 destroys it. A DCL cache is the size in its
 * descriptor and nothing else: every allocation comes from the one TLSF
 * heap whatever name it was asked for, so create() allocates the descriptor
 * out of that same heap, destroy() returns it, and the two allocation
 * spellings bottom out in kmalloc/kzalloc by way of the descriptor's size.
 *
 * `align`, the flags word and the constructor have nothing to act on and are
 * accepted because mainline's signature is what the vendored caller compiles
 * against. The one consequence a reader should see: it is a real allocation,
 * so create() can fail, and the caller checks -- which is the same failure
 * path mainline's create() has.
 */
static inline struct kmem_cache* kmem_cache_create(const char* name,
						   unsigned int size,
						   unsigned int align,
						   unsigned int flags,
						   void (*ctor)(void*))
{
	struct kmem_cache* cache;

	(void)name;
	(void)align;
	(void)flags;
	(void)ctor;
	cache = dcl_kmalloc(sizeof(*cache));
	if (!cache)
		return NULL;
	cache->size = size;
	return cache;
}

static inline void kmem_cache_destroy(struct kmem_cache* cache)
{
	kfree(cache);
}

static inline void* kmem_cache_zalloc(struct kmem_cache* cache, gfp_t gfp)
{
	(void)gfp;
	return dcl_kzalloc(cache->size);
}

static inline void kmem_cache_free(struct kmem_cache* cache, void* obj)
{
	(void)cache;
	kfree(obj);
}

static inline void* kmemdup(const void* src, size_t len, gfp_t gfp)
{
	void* p = dcl_kmalloc((unsigned int)len);

	(void)gfp;
	if (p && len)
		memcpy(p, src, len);
	return p;
}

/*
 * kstrdup(s, gfp) -- copy a NUL-terminated string into a fresh allocation.
 *
 * The string twin of kmemdup() above, and it sits beside it for that reason:
 * mainline keeps both in mm/util.c under one "duplicate this" heading, and
 * the two differ only in how the length is found. Callers are drm_print.c's
 * printer setup, which test the result against NULL the way mainline
 * documents, so the failure must be a real NULL and not a fallback.
 *
 * The terminator is included in the copy, which is why the length is
 * strlen + 1 and why memcpy is safe here: `src` is a C string by contract,
 * so the run it scans cannot pass the end of what it points at.
 */
static inline char* kstrdup(const char* s, gfp_t gfp)
{
	size_t len = strlen(s) + 1;
	char* p = (char*)dcl_kmalloc((unsigned int)len);

	(void)gfp;
	if (p)
		memcpy(p, s, len);
	return p;
}

/*
 * memdup_user_nul(src, len) -- copy `len` bytes out of a user pointer and
 * append a terminator, so a driver gets a kernel string of exactly the
 * length the user asked for. Callers are lib/string_helpers.c:179 and
 * drm_ioctl.c:590, and both test with IS_ERR() rather than NULL -- which is
 * the part worth getting right, because mainline distinguishes "no memory"
 * (-ENOMEM) from "the user pointer was bad" (-EFAULT), and a NULL here would
 * read as neither.
 *
 * The copy is memcpy rather than copy_from_user, and that is not a shortcut:
 * <linux/mm_shim.c>'s opening comment records that Xeneva runs a syscall with
 * the calling process's page tables live and the devfs bridge already hands
 * raw user pointers to ->read/->write, so copy_from_user *is* a memcpy there
 * too. The two ERR_PTR arms are kept because they are the contract, not
 * because of a boundary.
 *
 * The len >= INT_MAX arm is mainline's guard against `len + 1` overflowing
 * the allocation size; it is unreachable for a drm ioctl name and cheap to
 * keep.
 */
static inline void* memdup_user_nul(const void* src, size_t len)
{
	char* p;

	if (len >= (size_t)0x7fffffff)
		return ERR_PTR(-EINVAL);

	p = (char*)dcl_kmalloc((unsigned int)(len + 1));
	if (!p)
		return ERR_PTR(-ENOMEM);

	memcpy(p, src, len);
	p[len] = '\0';
	return p;
}

/*
 * get_zeroed_page() / free_page() -- the tty xmit buffer's allocator
 * (serial_core.c:255 and :296, one page per port).
 *
 * TWO THINGS ABOUT THE SIGNATURE, both because this is aarch64-unknown-windows:
 *
 * 1. The parameter and return are `uintptr_t`, not mainline's `unsigned
 *    long`.  This target is LLP64: unsigned long is 4 bytes, a pointer is 8,
 *    and get_zeroed_page hands back a heap address the TLSF allocator carved
 *    out of `0xFFFFC00000000000`.  Declaring it `unsigned long` would narrow
 *    on the way out, so every page handed to a port would be an address in
 *    the low 4 GB that nothing maps -- and free_page() would then be handed
 *    back a number that could not identify it.  This is the partner half of
 *    a two-part fix: the two `unsigned long` lines in serial_core.c that
 *    carry the address have to be uintptr_t too, and doing only one half of
 *    that pair would achieve nothing.
 *
 * 2. It is one page from the ordinary allocator, not from a page allocator:
 *    Xeneva's pmmngr manages physical pages for the initial identity map and
 *    has no per-page free list for the kernel heap, while kcalloc() already
 *    zeroes and kfree() already understands the block.  The page needs to be
 *    zeroed because kfifo_init() is handed it as a fresh ring and reads
 *    nothing that was not written.  PAGE_SIZE comes from <Mm/vmmngr.h>,
 *    included at the top of this header.
 */
static inline uintptr_t get_zeroed_page(gfp_t gfp)
{
	(void)gfp;
	return (uintptr_t)dcl_kzalloc(PAGE_SIZE);
}

static inline void free_page(uintptr_t addr)
{
	if (addr)
		kfree((void*)addr);
}

/*
 * kasprintf(gfp, fmt, ...) -- allocate and format, mainline's
 * include/linux/kernel.h spelling; it is here because this is the header
 * that owns allocation and serial_core.c includes it directly for kzalloc_objs.
 *
 * The window is fixed at DCL_KASPRINTF_MAX rather than mainline's
 * measure-then-allocate pair.  There is no measuring call to hand the format
 * to twice: _vsnprintf() would report a length, but the sizing pass upstream
 * does needs vsnprintf(NULL, 0, ...) to be legal, and DCL's formatter lives
 * in Xeneva's _print.c with no NULL-buffer contract to rely on -- guessing
 * that it has one would be a heap overrun waiting on an assumption.  So the
 * buffer is allocated first at a size no caller can exceed and the copy is
 * bounded, which fails as a truncated name instead of as a write past the
 * end.
 *
 * The single caller is serial_core.c:3087, kasprintf(GFP_KERNEL, "%s%u",
 * drv->dev_name, name_base + line) -- a driver name and a port number, an
 * order of magnitude inside 128.  If a ported file ever formats a longer
 * thing here, the number to raise is DCL_KASPRINTF_MAX and the check is
 * that kasprintf's caller still tests for NULL.
 */
#define DCL_KASPRINTF_MAX 128

static inline char* kasprintf(gfp_t gfp, const char* fmt, ...)
{
	char* buf;
	va_list ap;
	int n;

	(void)gfp;			/* GFP is advisory in DCL, as above */

	buf = (char*)dcl_kmalloc(DCL_KASPRINTF_MAX);
	if (!buf)
		return NULL;		/* serial_core.c:3089 tests this */

	va_start(ap, fmt);
	n = _vsnprintf(buf, DCL_KASPRINTF_MAX, fmt, ap);
	va_end(ap);

	if (n < 0)
	{
		kfree(buf);
		return NULL;
	}
	buf[n < DCL_KASPRINTF_MAX ? n : DCL_KASPRINTF_MAX - 1] = '\0';
	return buf;
}

/*
 * kvasprintf(gfp, fmt, ap) -- kasprintf() with the caller's va_list, which is
 * how mainline splits the pair (sprintf.h:18). Declared here beside its
 * varargs twin, whose fixed window and bounded copy its body in
 * DCL/linux_alloc_shim.c repeats; the three callers -- drm_crtc.c:286,
 * drm_plane.c:439, drm_encoder.c:120 -- format a driver-supplied name and
 * kfree() the result later.
 */
char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap);

#endif
