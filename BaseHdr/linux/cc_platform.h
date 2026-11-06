/*
 * DCL cc_platform.h -- confidential-computing queries answer "off".
 *
 * drm_cache.c asks cc_platform_has() whether the guest memory is encrypted;
 * Xeneva runs no SEV/TDX, so the query is always false and the plain memcpy
 * cache path stays selected.
 */
#ifndef _LINUX_CC_PLATFORM_H
#define _LINUX_CC_PLATFORM_H

/*
 * <stdbool.h> and not <linux/types.h>, which is this tree's aggregator: this
 * header is a leaf whose only need is `bool` and `false`, and pulling the
 * aggregator in to name them drags the whole kernel surface behind a header
 * that answers one question. mainline includes types.h here because mainline's
 * types.h is not an aggregator.
 */
#include <stdbool.h>

enum cc_attr {
	CC_ATTR_MEM_ENCRYPT,
	CC_ATTR_guest_MEM_ENCRYPT,
	CC_ATTR_MEM_PKEY,
	CC_ATTR_MEM_PKEYS,
	CC_ATTR_MEM_RW,
};

static inline bool cc_platform_has(enum cc_attr attr)
{
	(void)attr;
	return false;
}

#endif /* _LINUX_CC_PLATFORM_H */
