#ifndef __LINUX_COMPLETION_H
#define __LINUX_COMPLETION_H

/*
 * DCL <linux/completion.h> -- struct completion and its two initialisers.
 *
 * virtio-rng.c reaches this header without naming it: mainline's
 * <linux/virtio.h> includes <linux/completion.h> directly, and the driver
 * declares `struct completion have_data` as a struct member. Getting the type
 * from somewhere other than its own include is the point -- if DCL required
 * the driver to include a header mainline does not, the vendored file would
 * stop being byte-identical to upstream.
 *
 * ### The struct is one field, on purpose
 *
 * mainline's is:
 *
 *     struct completion { unsigned int done; struct swait_queue_head wait; };
 *
 * `wait` is the wait-queue the blocked tasks queue on. DCL has no scheduler
 * waiting to be woken: DCL/linux_kmod_shim.c's wait_for_completion_killable()
 * polls, spinning on `done` while calling virtio_poll_vqs() so the virtio
 * rings keep draining (which is also what makes the wait terminate -- the
 * completion is signalled by random_recv_done(), a virtqueue callback, and
 * that callback only runs from a poll). A wait-queue nobody blocks on would
 * be a member every definition has to lay out and every field offset has to
 * stay ahead of.
 *
 * The load-bearing part is that `done` is the FIRST member: the shim does
 *
 *     *(unsigned int*)x = 1;          -- complete()
 *     while (!*(unsigned int*)x) ...  -- wait_for_completion_killable()
 *
 * -- it treats the pointer as a bare `unsigned int`. That is exactly
 * `&completion->done`, which is only true while `done` is at offset 0. Move
 * it and every complete()/wait pair in the tree starts racing on whatever
 * object happens to follow.
 *
 * ### init/reinit are static inlines, as upstream has them
 *
 * (mainline: `static inline void init_completion(struct completion *x)`).
 * They could be `((x)->done = 0)` macros; as functions they keep the
 * argument typed, so passing anything but a `struct completion*` is an error
 * instead of a quietly-wrong dereference. Being `static inline` they also do
 * not drag an unused-function warning along when a file includes this header
 * and uses neither.
 */

#include <linux/kernel.h>	/* size_t */
/* mainline's chain is completion.h -> swait.h -> wait.h; DCL has no swait.h
 * (completion is the one-field struct above), so the wait queue type
 * drm_file.h:359 embeds comes in directly. */
#include <linux/wait.h>

struct completion {
	/* Must stay first -- see the note above. */
	unsigned int done;
};

/**
 * init_completion - initialise a dynamically allocated completion
 * @x: the completion
 */
static inline void init_completion(struct completion* x)
{
	x->done = 0;
}

/**
 * reinit_completion - reinitialise a completion that has been waited on
 * @x: the completion
 *
 * done is set to 0, never to anything else: wait_for_completion_killable()
 * in the shim *consumes* the signal (it zeroes `done` before returning), so
 * a reuse needs re-zeroing and not a decrement. mainline's does the same.
 */
static inline void reinit_completion(struct completion* x)
{
	x->done = 0;
}

/*
 * Both are defined in DCL/linux_kmod_shim.c. Declared with these types rather
 * than `void*` so the definition and every caller agree -- an earlier
 * arrangement had the shim taking `void*`, which links fine (C has no name
 * mangling) and silently lets a caller pass something that is not a
 * completion.
 *
 * wait_for_completion_killable() returns 0 on success or -ERESTARTSYS. It
 * cannot block forever: the shim bounds the spin and polls virtio while it
 * goes, so an object that is never completed gives up rather than hanging
 * the boot.
 */
extern void complete(struct completion* x);
extern int wait_for_completion_killable(struct completion* x);

/*
 * The rest of mainline's surface, which the drm atomic helpers and the event
 * path use: complete_all(), try_wait_for_completion(), the timeout wait and
 * the interruptible pair. Bodies in DCL/linux_kthread_shim.c.
 *
 * They all share one honest shape. The completion is the counter above and
 * there is no scheduler to block in, so a wait answers for the two states it
 * can actually observe: "already done" -- consume one and report success, as
 * mainline does -- and "not done", where it reports the failure mainline's
 * own callers are written against instead of pretending to sleep. A
 * completion that is not done when the wait is entered cannot become done
 * while control stays here, because whatever completes it (a flip handler,
 * the commit path below) runs in this same context.
 *
 * The failure values therefore matter and are mainline's: 0 from the timeout
 * variants and -ERESTARTSYS from the interruptible one. drm_crtc_commit_wait()
 * turns a 0 from wait_for_completion_timeout() into -ETIMEDOUT with a
 * drm_err(), and drm_atomic_helper_swap_state() unwinds on any nonzero from
 * wait_for_completion_interruptible(); success-without-completion would walk
 * those callers into hardware that was never programmed.
 *
 * complete_all() sets `done` to UINT_MAX (saturating, mainline's "permanently
 * done"), and try_wait_for_completion() does not consume that value, so every
 * later waiter keeps succeeding -- which is what DRM's fake commits and the
 * flip_done waiters rely on.
 */
extern void complete_all(struct completion* x);
extern bool try_wait_for_completion(struct completion* x);
extern unsigned long wait_for_completion_timeout(struct completion* x,
						unsigned long timeout);
extern int wait_for_completion_interruptible(struct completion* x);
extern long wait_for_completion_interruptible_timeout(struct completion* x,
						unsigned long timeout);

/**
 * DECLARE_COMPLETION - define a completion at file scope
 * @name: the name of the completion
 *
 * mainline's is `struct completion name = { 0 }` (or the ONSTACK twin, which
 * differs only in that mainline's ONSTACK runs a lockdep ctor). virtio_console.c:61
 * writes `static DECLARE_COMPLETION(early_console_added);`, and with no macro
 * the line parses as an untyped function declaration -- the same pair of
 * errors DEFINE_SPINLOCK produces -- so early_console_added() has no variable
 * behind it by the time :1571 does complete(&early_console_added).
 *
 * `= { 0 }` rather than `= { }` because a braceless initialiser on a struct
 * whose first member is unsigned int is a GNU extension, and this target is
 * built with clang.
 */
#define DECLARE_COMPLETION(name) struct completion name = { 0 }
#define DECLARE_COMPLETION_ONSTACK(name) struct completion name = { 0 }

#endif /* __LINUX_COMPLETION_H */
