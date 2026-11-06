/* DCL <linux/notifier.h> -- notifier chain types, not chains.
 *
 * mainline keeps the full atomic/blocking/raw/SRCU chain machinery here.
 * The drm core that DCL vendors only ever carries a struct notifier_block
 * around -- drm_connector embeds one for the privacy screen, and the
 * registration call behind it is the IS_ENABLED() inline stub in
 * drm_privacy_screen_consumer.h -- so what this header owes is the block,
 * the callback type and the NOTIFY_* vocabulary the stubs still spell.
 * No chain register/call surface is exported: nothing in the build calls
 * one, and the SRCU notifier head it would take is exactly the surface
 * that made the real port cost more than it bought.
 */
#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H

/*
 * <linux/mutex.h> -- for struct mutex, which this header's types do not
 * mention at all and which is the reason it is included anyway.
 *
 * mainline's own notifier.h reaches the type by a chain rather than naming
 * it: notifier.h -> <linux/rwsem.h> -> <linux/ww_mutex.h> ->
 * <linux/mutex.h>. DCL has all four of those headers and ww_mutex.h:21 does
 * include mutex.h, but DCL's rwsem.h does not pull ww_mutex.h in, so the
 * chain stops one link short here and the last one is named directly.
 *
 * The consumer is <drm/drm_connector.h>, which embeds `struct mutex` in
 * struct drm_connector at five sites (:1891, :1960, :1977, :2043, :2289) and
 * includes this header at :30. A field of an incomplete type is an error at
 * the point of declaration, so the definition has to be in scope before that
 * header is parsed -- it cannot be deferred to the .c that uses it.
 *
 * Not a leaf, unlike everything else in this header: mutex.h brings
 * kernel.h, cleanup.h, atomic.h and lockdep.h, and kernel.h is the
 * aggregator -- its subtree runs on through virtio.h and completion.h to
 * <linux/sched.h>, which redefines the word `current` as a macro. That is
 * the trap <linux/fcntl.h> documented at the bottom of its own file, and
 * the exposure is narrower here than it sounds: this header is included by
 * exactly one other file in the tree, <drm/drm_connector.h> (checked by
 * grep), and every source that parses that header is a drm one. None of
 * them includes <Fs/vfs.h>, whose `uint64_t current` member is the only
 * thing the macro has ever broken.
 */
#include <linux/mutex.h>

struct notifier_block;

typedef int (*notifier_fn_t)(struct notifier_block *nb,
			     unsigned long action, void *data);

struct notifier_block {
	notifier_fn_t notifier_call;
	struct notifier_block *next;
	int priority;
};

#define NOTIFY_DONE		0x0000	/* Don't care */
#define NOTIFY_OK		0x0001	/* Suits me */
#define NOTIFY_STOP_MASK	0x8000	/* Don't call further */
#define NOTIFY_BAD		(NOTIFY_STOP_MASK|0x0002)	/* Bad/Veto action */
#define NOTIFY_STOP		(NOTIFY_OK|NOTIFY_STOP_MASK)

/* Encapsulate (negative) errno value (in particular, NOTIFY_BAD <=> EPERM). */
static inline int notifier_from_errno(int err)
{
	if (err)
		return NOTIFY_STOP_MASK | (NOTIFY_OK - err);
	return NOTIFY_OK;
}

/* Restore (negative) errno value from notify return value. */
static inline int notifier_to_errno(int ret)
{
	ret &= ~NOTIFY_STOP_MASK;
	return ret > NOTIFY_OK ? NOTIFY_OK - ret : 0;
}

#endif /* _LINUX_NOTIFIER_H */
