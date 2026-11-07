#ifndef __LINUX_KREF_H__
#define __LINUX_KREF_H__

/*
 * DCL <linux/kref.h> -- reference counting for tty_struct and tty_driver,
 * and now for every drm object that embeds one.
 *
 * Both tty types are handed around as bare pointers by tty_io.c (a cdev open
 * finds a driver, a driver finds a port, a port finds a tty) and freed from
 * whichever path happens to run last. kref is the contract that says "last
 * one out turns the lights off".
 *
 * Xeneva has no real concurrency in these paths -- <linux/mutex.h> records
 * that plainly -- so the counter is atomic for mainline's reasons (the
 * arithmetic must be indivisible on SMP) rather than because anyone here can
 * actually race it. Keeping the operations atomic anyway costs a ldxr/stxr on
 * aarch64 and avoids a header whose semantics differ from mainline's the day
 * the second core turns on.
 *
 * The field is a refcount_t rather than the atomic_t it used to be. That was
 * mainline's own change (commit "kref: add refcount_t"), and this tree could
 * not skip it: drm_connector.c:1070 does not go through a kref helper, it
 * reaches inside the struct and writes
 *
 *     if (!refcount_dec_and_test(&conn->base.refcount.refcount))
 *
 * so the field has to be the type refcount_dec_and_test() takes. An
 * atomic_t there would be the same width and would happen to work, but only
 * by coincidence of layout -- and -Wno-error=incompatible-pointer-types is
 * set for this build, so the mistake would be a warning nobody reads rather
 * than a compile failure. refcount_t also buys saturation, which atomic_t
 * does not have: a kref that underflows wraps to a large positive value and
 * looks like a healthy object that will never be freed.
 *
 * kref_get_unless_zero() is included because tty_port_get() (tty_port.h:164)
 * is written around it: it must decline to take a reference on an object
 * already at zero, or a hangup path resurrects a port being destroyed. It now
 * resolves to refcount_inc_not_zero(), which is mainline's spelling and adds
 * the saturation check the atomic version lacked.
 */

#include <linux/refcount.h>

struct kref {
	refcount_t refcount;
};

#define KREF_INIT(n) { REFCOUNT_INIT(n) }

static inline void kref_init(struct kref* kref)
{
	refcount_set(&kref->refcount, 1);
}

static inline void kref_get(struct kref* kref)
{
	refcount_inc(&kref->refcount);
}

static inline int kref_get_unless_zero(struct kref* kref)
{
	return refcount_inc_not_zero(&kref->refcount);
}

static inline unsigned int kref_read(const struct kref* kref)
{
	return refcount_read(&kref->refcount);
}

/*
 * kref_put_unless_zero() is deliberately absent. mainline implements it on
 * refcount_dec_not_one(), a primitive whose whole job is refusing to take a
 * count from 0 to -1. The only call site in this tree was in
 * drm_gem_shmem_helper.c, which is not vendored (see Vendored/README.md);
 * add both together, where the zero case can be tested against a real driver
 * rather than against a stub.
 */

/*
 * kref_put() -- mainline returns int (1 if this call released) because
 * tty_release_struct() and friends branch on it. The release function takes
 * the kref, so callers derive their container with container_of().
 */
static inline int kref_put(struct kref* kref, void (*release)(struct kref* kref))
{
	if (refcount_dec_and_test(&kref->refcount)) {
		release(kref);
		return 1;
	}
	return 0;
}

#endif /* __LINUX_KREF_H__ */
