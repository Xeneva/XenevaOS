#ifndef __LINUX_INIT_H__
#define __LINUX_INIT_H__

/*
 * DCL <linux/init.h> -- initcall plumbing.
 *
 * mainline: fs_initcall(fn) stamps fn into .initcallN.init so do_initcalls()
 * runs it after the core is up. Xeneva has no initcall sections in its PE
 * image, so the macro instead stores fn in a named global pointer and
 * DclRunInitcalls() -- DCL/linux_mm_shim.c, which names the initcalls it
 * wants in the order it wants them -- calls them. Keeping the pointer
 * (rather than an empty macro) also keeps the static __init function
 * referenced, so it neither warns nor gets dropped.
 *
 * Runner naming: __dcl_initcall_<fn>.
 */

#ifndef __init
#define __init
#endif

#ifndef __exit
#define __exit
#endif

#ifndef __weak
#define __weak __attribute__((weak))
#endif

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

#define DCL_INITCALL(fn) \
	int (*const __dcl_initcall_##fn)(void) __attribute__((used)) = fn

#define fs_initcall(fn) DCL_INITCALL(fn)
#define core_initcall(fn) DCL_INITCALL(fn)
#define device_initcall(fn) DCL_INITCALL(fn)
#define late_initcall(fn) DCL_INITCALL(fn)
#define module_init(fn) DCL_INITCALL(fn)

/*
 * subsys_initcall -- one more level of mainline's 0..6 ordering, used by
 * Vendored/drivers/dma-buf/dma-fence.c:131 to install its stub fence provider.
 * DCL has no ordering between initcalls: DclRunInitcalls() (DCL/
 * linux_mm_shim.c) names each one explicitly in the order it wants, so this
 * lands in the same bucket as every other level here.  What matters is that
 * it exists at all -- without the macro the call site parses as an implicit
 * int function call and the whole file fails from there.
 *
 * dma_fence_init_stub has to be added to that runner's list before it will
 * actually run; see the ordering comment there.
 */
#define subsys_initcall(fn) DCL_INITCALL(fn)

#endif /* __LINUX_INIT_H__ */
