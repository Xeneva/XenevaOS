#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

/*
 * DCL <linux/uaccess.h> -- the user-access family mainline drivers copy in.
 *
 * XenevaOS keeps the calling process's page tables active while it runs a
 * syscall: the devfs bridge hands raw user pointers straight to
 * ->read/->write, and this kernel already dereferences them there. So the
 * copy_*_user family is a direct copy wearing mainline's contract:
 *
 *   copy_to_user/copy_from_user/clear_user  return bytes NOT copied, 0 == ok
 *   access_ok                               returns 1 (no separate range)
 *
 * That keeps a mainline .c honest -- its `if (copy_to_user(...)) return
 * -EFAULT;` branches behave exactly as upstream -- while the memory model
 * stays Xeneva's. A VMA probe can be added behind access_ok later without
 * touching a single caller.
 *
 * Mainline declares these in include/linux/uaccess.h.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef __user
#define __user
#endif

#ifdef __cplusplus
extern "C" {
#endif

unsigned long copy_to_user(void __user* to, const void* from, unsigned long n);
unsigned long copy_from_user(void* to, const void __user* from, unsigned long n);
unsigned long clear_user(void __user* to, unsigned long n);

/* mainline: 0 = success, -EFAULT = the address is not usable */
long copy_from_kernel_nofault(void* dst, const void* src, unsigned long size);

/* no-op in a kernel that never returns into a "failed syscall" state */
static inline void force_successful_syscall_return(void) {}

#ifdef __cplusplus
}
#endif


/*
 * put_user(x, ptr) -- store one value into a userspace address, returning 0
 * on success and -EFAULT when the address is not usable.
 *
 * DCL's memory model is the one described at the top of this header: the
 * syscall keeps the caller's page tables, so the store is a direct one, and
 * there is no fault to recover from -- hence the unconditioal 0.  The
 * expression form matters because serial_core.c:1074 is
 *
 *     return put_user(result, value);
 *
 * i.e. the macro has to *yield* the status, not merely perform the store; a
 * do/while() form would have compiled as an implicit-int statement and
 * returned garbage.
 */
#define put_user(x, ptr)						\
	({								\
		*(ptr) = (x);						\
		0;							\
	})

/*
 * get_user(x, ptr) -- the read half, and it must yield the status for the
 * same reason put_user() does: tty_ioctl.c:884 writes
 *
 *     if (get_user(arg, (unsigned int __user *)arg))
 *
 * so a do/while() form would have been an implicit-int expression whose value
 * was garbage, and the guard would have taken its failure arm on success.
 *
 * Body is the store read backwards; 0 is the success it must yield.  mainline
 * adds a probe for a faulting userspace address, and DCL's put_user() does
 * not have that either -- both run in the kernel, where the pointer arrived
 * from a call the kernel already validated.  That is pre-existing, not
 * introduced here.
 */
#define get_user(x, ptr)						\
	({								\
		(x) = *(ptr);						\
		0;							\
	})
#endif /* __LINUX_UACCESS_H__ */
