#ifndef __LINUX_TIMER_H__
#define __LINUX_TIMER_H__

#include <linux/container_of.h>	/* container_of -- timer_container_of below */

struct timer_list {
    void          (*function)(struct timer_list* t);
    unsigned long   expires;    /* in jiffies */
    void* data;       /* private context */
    int             pending;
};

/*
 * timer_container_of(ptr, timer_member, field) -- recover the struct owning
 * a timer_list from the timer callback's argument. mainline spells it the
 * same way, in include/linux/timer.h.
 *
 * The three arguments look like a plain container_of() with an extra one,
 * and the difference matters: container_of()'s first argument is the pointer
 * *to the member*, whereas here it is the pointer *to the container* -- and
 * the container is the variable being declared. So `vblank` in
 *
 *   struct drm_vblank_crtc *vblank = timer_container_of(vblank, t,
 *                                                       disable_timer);
 *
 * is read before it exists, but only for its type. That is why the expansion
 * takes __typeof__(*ptr) rather than deriving the type from the member, and
 * why the second argument is what gets passed through to container_of():
 * there the static_assert checks that the pointer points at a `field`, and
 * `t` does.
 *
 * __typeof__ rather than typeof because this target is not in GNU mode by
 * default.
 */
#define timer_container_of(ptr, timer_member, field)			\
	container_of(timer_member, __typeof__(*(ptr)), field)

/*
 * Jiffies -- derived from the ARM generic timer, not counted by a tick.
 *
 * Nothing in Xeneva increments a tick counter: there is no timer IRQ calling
 * a scheduler tick, so an `extern volatile unsigned long jiffies` would link
 * to a variable that never moves, and every `time_after(jiffies, timeout)` a
 * driver writes would then be false forever -- a polling loop that never
 * notices its deadline, which reads as a hang rather than a timeout. Reading
 * the same counter that udelay() burns against keeps comparisons honest with
 * no tick to service, at HZ (250) resolution.
 *
 * It is an object-like macro, so `jiffies++` and `&jiffies` do not compile.
 * Nothing in the tree does either -- the uses are reads (8250_port.c,
 * n_tty.c, serial_core.c) -- and a compile error is the right answer if that
 * changes, rather than a silently wrong increment.
 */
unsigned long dcl_jiffies_now(void);
#define jiffies (dcl_jiffies_now())

#define HZ              250              /* ticks per second � tune to your timer IRQ */
#define msecs_to_jiffies(ms)  ((ms) * HZ / 1000)
#define jiffies_to_msecs(j)   ((j)  * 1000 / HZ)
#define time_after(a, b)      ((long)((b) - (a)) < 0)

/*
 * nsecs_to_jiffies(n) -- the conversion uart_poll_timeout() makes
 * (serial_core.h:1315) to turn a frame time into a polling deadline.
 *
 * Truncating division by the nanoseconds-per-tick, which is div_u64()'s
 * behaviour upstream: mainline rounds only in nsecs_to_jiffies64(), and
 * matching the 64-bit one here would change a deadline by a tick in a
 * function whose caller already does `max(..., 1UL)` to keep it from hitting
 * zero.  The round trip is exact at HZ 250: NSEC_PER_SEC / HZ is 4,000,000,
 * an exact divisor of the second, so nothing accumulates.
 */
/*
 * A macro, and the reason is the include order in <linux/kernel.h>: it pulls
 * this header in at line 100 and only defines NSEC_PER_SEC at line 281, so a
 * function body written here would be checked against a NSEC_PER_SEC that
 * does not exist yet -- including through this file's own include of
 * kernel.h, which the guard has already emptied.  Expanding at the use site
 * (serial_core.h:1315) instead, where kernel.h has finished, is the whole
 * difference; `n` appears exactly once below, so there is no double
 * evaluation for the macro form to introduce.
 */
#define nsecs_to_jiffies(n) ((unsigned long)((n) / (NSEC_PER_SEC / HZ)))

/*
 * nsecs_to_jiffies64(n) -- the 64-bit form drm_syncobj.c waits with (:442, a
 * submit timeout, and :1220, an absolute deadline) and mainline keeps in
 * <linux/jiffies.h>.  A macro, and for the same include-order reason
 * nsecs_to_jiffies above is one: <linux/kernel.h> pulls this header in before
 * it defines NSEC_PER_SEC, so the division has to be spelled at the call
 * site, where kernel.h has finished.  At HZ 250 the divisor is exact
 * (NSEC_PER_SEC / HZ == 4000000), which is mainline's own common case
 * (`div_u64(n, NSEC_PER_SEC / HZ)` in kernel/time/time.c) with an ordinary
 * 64-bit division; `n` is read once.
 */
#define nsecs_to_jiffies64(n) ((u64)((n) / (NSEC_PER_SEC / HZ)))

#define time_before(a, b)     time_after(b, a)
#define time_after_eq(a, b)   ((long)((a) - (b)) >= 0)

#define timer_setup(t, fn, flags) do { \
    (t)->function = (fn);              \
    (t)->pending  = 0;                 \
    (t)->data     = NULL;              \
    (t)->expires  = 0;                 \
} while (0)

/*
 * No setup_timer() here, on purpose.
 *
 * It was in this header as a "legacy init form DWC2 may use", and mainline
 * has not had the macro since timer_setup() replaced it -- so nothing here
 * needed it, and keeping it broke a file that did: mainline's
 * serial_8250.h:131 declares a *field* named setup_timer
 *
 *     void (*setup_timer)(struct uart_8250_port *);
 *
 * and a function-like macro named setup_timer turns that declaration, and
 * every call of it (`up->ops->setup_timer(up)` at 8250_port.c:2322), into a
 * macro invocation with too few arguments. The fix is not to rename the
 * field -- that would edit a vendored file -- it is for DCL not to invent a
 * macro mainline removed.
 *
 * If something genuinely needs the legacy form, it will ask for it by name
 * and can have it back then, under a name that does not shadow driver fields.
 */

/* On bare metal � you need a real timer queue for these */
/* Stub for bring-up: call immediately */
static inline void add_timer(struct timer_list* t) {
    t->pending = 1;
    /* TODO: insert into your timer queue */
}

static inline int mod_timer(struct timer_list* t, unsigned long expires) {
    t->expires = expires;
    t->pending = 1;
    /* TODO: update in your timer queue */
    return 0;
}

static inline int del_timer(struct timer_list* t) {
    int was_pending = t->pending;
    t->pending = 0;
    return was_pending;
}

static inline int del_timer_sync(struct timer_list* t) {
    return del_timer(t);
}

/*
 * timer_delete_sync() -- the name v7.2 gives that operation (mainline:
 * include/linux/timer.h declares it, kernel/time/timer.c owns it).  The one
 * user here is drm_vblank.c:533, which discards the result; forwarding to
 * del_timer_sync() keeps a single body for one operation rather than letting
 * the old and new spelling drift.  There is no handler to synchronise with,
 * because no timer IRQ runs one -- see the note on add_timer() below.
 */
static inline int timer_delete(struct timer_list* t) {
    return del_timer(t);
}

static inline int timer_delete_sync(struct timer_list* t) {
    return del_timer_sync(t);
}

static inline int timer_pending(const struct timer_list* t) {
    return t->pending;
}

/* Helper to get owning struct from timer pointer */
#define from_timer(var, callback_timer, timer_fieldname) \
    container_of(callback_timer, typeof(*var), timer_fieldname)

#endif /* __LINUX_TIMER_H__ */