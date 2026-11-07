/*
 * DCL/linux_alloc_shim.c -- the allocator entry points mainline keeps in
 * mm/util.c, mm/slab_common.c and lib/vsprintf.c, as real definitions.
 *
 * All of them sit over one allocator: Xeneva's TLSF heap (Mm/kmalloc.h), the
 * same kmalloc()/kfree() pair slab.h's static inlines use. mainline has two
 * allocators (slab and vmalloc) and the interesting half of each function is
 * the choice between them; DCL has no vmalloc -- BaseHdr/linux/vmalloc.h is
 * an empty shell -- so the kvmalloc family has no second path to take and
 * every block below is an ordinary TLSF block. What the missing second path
 * would have bought, and what its absence costs the callers, is noted at each
 * function.
 *
 * kfree_const()/kstrdup_const() are mm/util.c's rodata-aware pair; ksize() is
 * mm/slab_common.c's; kvasprintf() is lib/vsprintf.c's va_list formatter;
 * memdup_array_user()/vmemdup_array_user() are string.h inlines upstream that
 * the vendored callers here run as calls (nothing in DCL declared them, so
 * they compiled as implicit declarations returning int -- a truncated pointer
 * on this 64-bit target, which is why slab.h now declares them).
 */
#include <linux/slab.h>
#include <linux/err.h>		/* ERR_PTR, for the *_array_user pair */
#include <Mm/tlsf.h>		/* block_header_t -- what ksize() reads */

/*
 * kvmalloc(size, gfp) -- mainline's "try kmalloc, then fall back to vmalloc"
 * (mm/util.c:1725), for any size a vendored caller asks. The one caller here
 * is vkms_composer.c:568/:574, one composited scanline each, tens of KB at
 * 8K-wide modes -- exactly the size where mainline would have taken the
 * fallback. There is none to take: a large request that fails returns NULL,
 * and compose_active_planes() already checks for that and unwinds with
 * -ENOMEM, which is also the outcome a failed vmalloc fallback produces. A
 * threshold here would only decide which allocator gets to fail.
 */
void *kvmalloc(size_t size, gfp_t flags)
{
	(void)flags;
	return dcl_kmalloc((unsigned int)size);
}

/*
 * kvzalloc(size, gfp) -- kvmalloc with __GFP_ZERO, i.e. mainline's
 * `kvmalloc(size, flags | __GFP_ZERO)` (slab.h:1356). DCL's kzalloc() zeroes
 * unconditionally, so the flag is already honoured without being passed.
 * drm_property.c:565 builds a property blob with it and reads the padding
 * bytes the zeroing covers.
 */
void *kvzalloc(size_t size, gfp_t flags)
{
	(void)flags;
	return dcl_kzalloc((unsigned int)size);
}

/*
 * kvfree(addr) -- the free half of both of the above. mainline dispatches on
 * whether the pointer is a vmalloc address; here every pointer kvmalloc()
 * returned is a TLSF block, so there is one branch to take. kfree() already
 * ignores NULL (Mm/kmalloc.c:180), which is the rest of mainline's kvfree
 * contract, and drm_gem.c's error paths (:728, :780, :859) free handles and
 * page arrays that may be NULL or not.
 */
void kvfree(const void *addr)
{
	kfree((void *)addr);
}

/*
 * ksize(objp) -- the size the allocator reserved for objp, read from the TLSF
 * block header that sits immediately before the payload (BaseHdr/Mm/tlsf.h's
 * block_header_t, and tlsf_malloc() returns blk_payload() -- mm/tlsf.c:518).
 *
 * mainline's promise is "may be larger than requested, and the caller may use
 * the difference". The header's size satisfies it: TLSF rounds the request up
 * to its alignment and the block keeps any slack the split did not need. The
 * one caller, drm_managed.c:134, uses the answer as an upper bound in a WARN
 * -- "is this struct drm_device inside the container it was carved out of" --
 * and it has only the pointer, not the request, so an answer derived from the
 * request is not available here: returning 0 would fire that check on every
 * sound container, and a sentinel would silence it on the unsound ones. The
 * header is the one source of truth both sides can see.
 *
 * The debug allocator rewrites the payload's prefix (tlsf.h's
 * __XENEVA_DEBUG_ALLOC__ block: 16-byte backend header plus a redzone in
 * front of the user pointer), so the TLSF header sits 48 bytes back instead
 * of 16. The backend header records the requested size, and a request is a
 * legal answer for a function whose contract is "at least this much".
 */
size_t ksize(const void *objp)
{
#ifdef __XENEVA_DEBUG_ALLOC__
	const tlsf_dbg_hdr_t *dbg;

	if (!objp)
		return 0;
	dbg = (const tlsf_dbg_hdr_t *)((const char *)objp -
				      (TLSF_DBG_HDR_SIZE + TLSF_RZ_SIZE));
	return dbg->user_size;
#else
	const block_header_t *hdr;

	if (!objp)
		return 0;
	hdr = (const block_header_t *)objp - 1;
	return (hdr->size & BLOCK_SIZE_MASK) - TLSF_HEADER_SIZE;
#endif
}

/*
 * kstrdup_const()/kfree_const() -- mainline's pair for strings whose source
 * may live in .rodata (mm/util.c:43/:99): the duplicate half hands the source
 * pointer back unsullied when is_kernel_rodata() says it can, and the free
 * half re-tests before calling kfree().
 *
 * DCL has no is_kernel_rodata(): there is no separately mapped rodata region
 * to test against, and the strings that reach these two are the allocator's
 * own action names (drm_managed.c:154/:239) and vkms's dev_name
 * (vkms_config.c:19) -- caller-owned literals or a kstrdup'd name. So the
 * pair is written to the postcondition instead of the precondition: always
 * duplicate, always free. A kfree_const() that guessed "rodata, do not free"
 * would leak every drm_managed node name; always freeing can only be wrong if
 * a caller passes a non-heap pointer, which the always-duplicate half makes
 * impossible through this pair.
 */
const char *kstrdup_const(const char *s, gfp_t gfp)
{
	return kstrdup(s, gfp);
}

void kfree_const(const void *x)
{
	kfree((void *)x);
}

/*
 * kvasprintf(gfp, fmt, ap) -- kasprintf()'s va_list twin (slab.h), declared
 * there and built the same way for the same reason: DCL's _vsnprintf() has no
 * NULL-buffer measuring mode, so the buffer is allocated first at a size no
 * caller can exceed and the copy is bounded. drm_crtc.c:286, drm_plane.c:439
 * and drm_encoder.c:120 pass name strings like "vkms-crtc%d" -- an order of
 * magnitude inside DCL_KASPRINTF_MAX -- and hand the result to kfree() later,
 * which the plain kmalloc() body satisfies.
 */
char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap)
{
	char *buf;
	int n;

	(void)gfp;

	buf = (char *)dcl_kmalloc(DCL_KASPRINTF_MAX);
	if (!buf)
		return NULL;

	n = _vsnprintf(buf, DCL_KASPRINTF_MAX, fmt, ap);
	if (n < 0) {
		kfree(buf);
		return NULL;
	}
	buf[n < DCL_KASPRINTF_MAX ? n : DCL_KASPRINTF_MAX - 1] = '\0';
	return buf;
}

/*
 * memdup_array_user(src, n, size) / vmemdup_array_user() -- duplicate `n`
 * user-space elements of `size` bytes into a fresh block. mainline has these
 * as string.h inlines over memdup_user()/vmemdup_user(), which DCL does not
 * have (and does not need: copy_from_user() *is* a memcpy in this tree --
 * Xeneva runs the syscall with the caller's page tables live, see
 * <linux/mm_shim.c> -- so there is no user/kernel copy distinction these
 * could preserve).
 *
 * The overflow check is why both callers test IS_ERR() rather than NULL
 * (drm_lease.c:513, drm_gem.c:849): a wrapped count returns -EOVERFLOW and a
 * failed allocation -ENOMEM, neither of which may be a bare NULL, or the
 * caller's PTR_ERR() path would read it as success code 0. The two names
 * differ only in which free the caller is expected to use afterwards (kfree
 * vs kvfree, drm_lease.c:519 vs drm_gem.c:859); both work on a TLSF block, so
 * the v-form is the same call under the name kvfree-compatible callers use.
 */
/* The allocation and copy both memdup forms end in; the array form only adds
 * the overflow check before calling it. */
static void *dcl_memdup_user(const void __user *src, size_t len)
{
	void *p = dcl_kmalloc((unsigned int)len);

	if (!p)
		return ERR_PTR(-ENOMEM);

	memcpy(p, src, len);
	return p;
}

void *memdup_array_user(const void __user *src, size_t n, size_t size)
{
	size_t bytes;

	if (check_mul_overflow(n, size, &bytes))
		return ERR_PTR(-EOVERFLOW);

	return dcl_memdup_user(src, bytes);
}

void *vmemdup_array_user(const void __user *src, size_t n, size_t size)
{
	return memdup_array_user(src, n, size);
}

/*
 * memdup_user() / vmemdup_user() -- the pair the array forms above are
 * inlines over (mainline declares them in <linux/string.h>; see the note
 * in <linux/slab.h> for why the declarations land there here). The
 * virtio-gpu ioctl blob paths and the submit queue reach for them
 * directly, and neither had a definition. The same two facts as above
 * make the body what it is: copy_from_user() *is* memcpy here, so the
 * copy is a plain one, and the IS_ERR() answers are what the callers
 * check. The empty length follows dcl_kmalloc()'s zero handling -- a
 * one-byte block, never a bare NULL.
 */
void *memdup_user(const void __user *src, size_t len)
{
	return dcl_memdup_user(src, len);
}

void *vmemdup_user(const void __user *src, size_t len)
{
	return memdup_user(src, len);
}
