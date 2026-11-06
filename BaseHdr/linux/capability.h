#ifndef __LINUX_CAPABILITY_H__
#define __LINUX_CAPABILITY_H__

/* bool -- checkpoint_restore_ns_capable() returns one.  This header had no
 * includes at all, so the type came from nowhere. */
#include <stdbool.h>

/*
 * DCL <linux/capability.h> -- Xeneva does not model Linux capabilities yet.
 * capable() answers yes so mainline driver entry points that gate themselves
 * (open_port() on /dev/mem is the mem.c case) stay reachable; the devfs node
 * permissions remain the real access gate, as they are for every other
 * Xeneva device node.
 */

#define CAP_SYS_RAWIO 17

static inline int capable(int cap) {
	(void)cap;
	return 1;
}


/*
 * CAP_SYS_ADMIN -- the capability three of serial_core.c's gates name:
 * uart_set_options() (:349), uart_get_lsr_info() (:1136) and the
 * TIOCSSERIAL path (:938).  Numbered 21 because that is mainline's value in
 * include/linux/capability.h and a capability number is an ABI constant if
 * anything ever reads it back from userspace.
 *
 * capable() ignores its argument and answers yes (see its own comment at the
 * top of this file), so the *number* only ever reaches `(void)cap` -- it is
 * defined for the same reason the named error codes are: the source spells
 * it, and an undefined identifier there would otherwise be read as 0 by
 * IS_ENABLED-style macros or fail outright as an enum member.
 */
#define CAP_SYS_ADMIN 21

/*
 * CAP_SYS_MODULE 16 -- tty_ldisc.c:153, the gate in front of
 * request_module("tty-ldisc-%d"): `!capable(CAP_SYS_MODULE) &&
 * !tty_ldisc_autoload`.  16 is mainline's number in
 * include/linux/capability.h, for the reason CAP_SYS_ADMIN's comment gives:
 * it may never be compared with anything, but it is spelled as a decimal
 * literal and an undefined identifier there would be read as 0 -- which here
 * would mean CAP_CHOWN rather than an unknown capability.
 *
 * capable() ignores its argument (top of this file), so the branch resolves to
 * `0 && !tty_ldisc_autoload` and the -EPERM arm is skipped unconditionally.
 */
#define CAP_SYS_MODULE 16

/*
 * struct user_namespace / init_user_ns -- and checkpoint_restore_ns_capable(),
 * called once, at tty_ioctl.c:843, to gate TIOCSLCKTRMIOS (locking the line
 * settings of a terminal) behind a capability check.
 *
 * mainline keeps the type and the object in <linux/user_namespace.h> and the
 * predicate in <linux/capability.h> here; DCL has no user_namespace.h, and
 * the predicate is the only thing that needs either, so both live beside it.
 * The struct is a placeholder member and nothing more: the object exists so
 * that `&init_user_ns` has an address to take, and it is never dereferenced
 * -- mainline's is roughly sixty fields of uid/gid mapping, all of which
 * answer a question this build has no way to ask.
 *
 * The predicate answers yes for the same reason capable() does: DCL's access
 * gate is the devfs node's permissions, not the capability bitmap.  mainline's
 * body is `ns_capable(ns, CAP_CHECKPOINT_RESTORE) || ns_capable(ns,
 * CAP_SYS_ADMIN)`; there is no ns_capable() here to call and no namespace
 * table to look the caller up in, so the argument is dropped rather than
 * consulted -- which is why the struct can be this empty.
 *
 *   upstream  include/linux/user_namespace.h (type + object),
 *             include/linux/capability.h:200 (the predicate)
 */
struct user_namespace {
	void* placeholder;
};

extern struct user_namespace init_user_ns;

static inline bool checkpoint_restore_ns_capable(struct user_namespace* ns)
{
	(void)ns;
	return true;
}

#endif /* __LINUX_CAPABILITY_H__ */
