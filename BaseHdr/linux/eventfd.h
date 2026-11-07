/*
 * DCL eventfd.h -- drm_syncobj's eventfd signalling is inert.
 *
 * eventfd_ctx_fdget() fails the ioctl up front (no eventfds on Xeneva), so
 * the signal/put sides never run; they exist to keep the syncobj code
 * linking and parsing.  One-argument eventfd_signal() is the v7.2 signature.
 */
#ifndef _LINUX_EVENTFD_H
#define _LINUX_EVENTFD_H

#include <linux/err.h>
#include <linux/types.h>

struct eventfd_ctx {
	int _dcl_dummy;
};

static inline struct eventfd_ctx *eventfd_ctx_fdget(int fd)
{
	(void)fd;
	return ERR_PTR(-ENOSYS);
}

static inline int eventfd_signal(struct eventfd_ctx *ctx)
{
	(void)ctx;
	return 0;
}

static inline void eventfd_ctx_put(struct eventfd_ctx *ctx)
{
	(void)ctx;
}

#endif /* _LINUX_EVENTFD_H */
