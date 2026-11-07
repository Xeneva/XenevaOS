#ifndef __LINUX_GFP_H__
#define __LINUX_GFP_H__

/*
 * DCL <linux/gfp.h> -- allocation flags.  mainline's is 16.8 KB and brings
 * mmzone.h -> nodemask.h -> numa.h -> percpu behind it, i.e. the entire NUMA
 * surface, in exchange for two constants.
 *
 * DCL already owns both, in <linux/kernel.h>:
 *     :55    typedef unsigned int gfp_t;
 *     :111   #define GFP_KERNEL 0
 *     :112   #define GFP_ATOMIC 1
 * and <linux/slab.h> already accepts mainline's two-argument kmalloc(size,
 * gfp) by dropping the flag (`#define kmalloc(size, flags) dcl_kmalloc(size)`).
 * So there is nothing to *define* here -- this header exists so a mainline
 * source that writes `#include <linux/gfp.h>` resolves, and it routes rather
 * than redefines: a second gfp_t and a second GFP_KERNEL in one translation
 * unit would be a redefinition error, and mainline's GFP_KERNEL is a bitmask
 * of __GFP_RECLAIM|__GFP_IO|__GFP_FS that nothing here interprets.
 *
 * Routing the two constants is not the same as having no vocabulary, though,
 * and the include below is the difference: mainline's gfp.h includes
 * gfp_types.h, which carries the individual __GFP_* flags. drm_managed.h
 * needs one of them -- drmm_kzalloc() and drmm_kzalloc_array() write
 * `flags | __GFP_ZERO`. Without the include those two inline functions fail
 * in the *header* that declares them, so no drm caller ever reaches the
 * -ENOSYS path gfpflags_allow_blocking() sits beside.
 */
#include <linux/kernel.h>
#include <linux/gfp_types.h>

/*
 * gfpflags_allow_blocking(gfp) -- "may this allocation sleep?".  DCL's
 * allocator never sleeps and never refuses, so the honest answer for every
 * flag word this tree builds (GFP_KERNEL is 0, see kernel.h:111) is yes;
 * the one caller, xarray.c:339, takes either arm to the same
 * kmem_cache_alloc_lru() and only wants the lock dropped before it.  The
 * parameter is still read at the call site, so it stays a real parameter
 * of a real function rather than a macro that would vanish it.
 */
static inline int gfpflags_allow_blocking(gfp_t gfp)
{
	(void)gfp;
	return 1;
}

#endif /* __LINUX_GFP_H__ */
