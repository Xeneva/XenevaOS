/*
 * DCL <linux/timerqueue.h> -- the expiry-node struct, and only that.
 *
 * mainline keeps struct timerqueue_node and struct timerqueue_head here with
 * a red-black tree of them behind timerqueue_add/min/removel (kernel/time/).
 * DCL has no hrtimer queue at all -- hrtimer.h's four static inlines are the
 * whole surface, and there is no clock base for a queued timer to sit on --
 * so the tree and its three operations are not absent by oversight, they are
 * absent because there is no queue to give them to.
 *
 * The node is a different case: drm_vblank.c:2311 reads
 * `vtimer->timer.node.expires` to compare a re-read expiry time against the
 * one it already has, and that is a *member access on struct hrtimer*, which
 * means the type has to exist and has to carry the member. It is mainline's
 * spelling and mainline's two fields so that a real queue could be dropped
 * in underneath without the read sites changing.
 *
 * Include order is mainline's: rbtree for struct rb_node, ktime for
 * ktime_t. ktime.h routes through kernel.h, which is the aggregator -- the
 * same exposure hrtimer.h already carries by including ktime.h at its own
 * line 57, so nothing new reaches a source by adding this header beside it.
 */
#ifndef _LINUX_TIMERQUEUE_H
#define _LINUX_TIMERQUEUE_H

#include <linux/rbtree.h>	/* struct rb_node */
#include <linux/ktime.h>	/* ktime_t */

struct timerqueue_node {
	struct rb_node node;
	ktime_t expires;
};

#endif /* _LINUX_TIMERQUEUE_H */
