#ifndef __LINUX_SECURITY_H__
#define __LINUX_SECURITY_H__

/* mainline's <linux/security.h> includes this, and serial_core.c relies on
 * the chain: it calls capable(CAP_SYS_ADMIN) at :349, :938, :1136 and :1995
 * without including <linux/capability.h> itself.  DCL's capable() is a
 * `static inline`, so an implicit declaration of it would not have resolved
 * at link time either -- this include is what makes those four call sites
 * real rather than merely compiling. */
#include <linux/capability.h>

/*
 * DCL <linux/security.h> -- lockdown hooks only. mainline mem.c gates
 * open_port() on security_locked_down(LOCKDOWN_DEV_MEM); with no LSM in
 * Xeneva the hook always clears. Provided by DCL/linux_mm_shim.c.
 */

#define LOCKDOWN_NONE		0
#define LOCKDOWN_INTEGRITY_MAX	1
#define LOCKDOWN_DEV_MEM	2

int security_locked_down(int reason);


/*
 * LOCKDOWN_TIOCSSERIAL -- the reason serial_core.c:954 passes to
 * security_locked_down() before it will accept a TIOCSSERIAL ioctl.
 *
 * Not in mainline's uapi/linux/lockdown.h order (upstream runs
 * NONE, INTEGRITY_MAX, CONFIDENTIALITY_MAX, DEV_MEM, ...), because DCL's
 * header already numbers its three reasons its own way and every caller
 * treats them as "nonzero means refused" rather than as a sequence.  Appended
 * so it does not disturb the values mem.c already passes.  The answer behind
 * it is unchanged: DCL/linux_mm_shim.c's security_locked_down() returns 0 for
 * every reason, so this one and LOCKDOWN_DEV_MEM both mean "allowed" --
 * correct for a single-user system with no LSM, which is what this is.
 */
#define LOCKDOWN_TIOCSSERIAL	3
#endif /* __LINUX_SECURITY_H__ */
