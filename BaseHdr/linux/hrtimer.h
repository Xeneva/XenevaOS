#ifndef __LINUX_HRTIMER_H__
#define __LINUX_HRTIMER_H__

/*
 * DCL <linux/hrtimer.h> -- the two RS485 turnaround timers 8250_port.c owns.
 *
 * This header exists because struct serial8250_em485 embeds two struct
 * hrtimer BY VALUE (serial_8250.h:126-127), so the type has to be complete at
 * the point of declaration -- and mainline reaches it there by accident, not
 * by listing it: upstream's serial_8250.h includes errno/serial_core/
 * serial_reg/platform_device and nothing that names hrtimer, so struct
 * hrtimer is complete only because serial_core.h -> sched.h happens to drag it
 * in. DCL's serial_core.h is assembled from mainline but its include list is
 * the subset BaseHdr actually has, so the dependency is written down in
 * serial_8250.h instead of arriving by side effect.
 *
 * WHAT THESE DO, AND WHAT THEY DO NOT
 * -----------------------------------
 * hrtimer_start() records the target time and sets a pending flag. It does
 * NOT queue anything, and no callback ever fires from it.
 *
 * That is not a placeholder chosen because it was quick: Xeneva runs no timer
 * interrupt at all, which is already documented at the top of
 * <linux/timer.h> -- there is no tick to count jiffies, so `extern jiffies`
 * would link to a word that never moves, and DCL reads the same counter
 * udelay() burns against instead. With no tick there is no place to run a
 * timer queue from, and <linux/timer.h>'s add_timer()/mod_timer() have the
 * same contract today: set pending, do not schedule, with the queue left as a
 * TODO for whoever brings up real deferred work. These functions are the same
 * contract, written in ktime rather than jiffies, so the two halves of DCL's
 * timer story do not disagree.
 *
 * The consequence, stated plainly because it is a real limitation and not a
 * formality: serial8250_em485_handle_stop_tx()/..._handle_start_tx() never
 * run, so the delay between "shift from TX to RX" and "shift back" is never
 * applied. That delay exists to let the last stop bit leave the wire before
 * the driver flips an RS485 transceiver's direction pin.
 *
 * Why that costs nothing today: these two timers are armed only by
 * serial8250_em485_init(), which is called only from
 * serial8250_em485_config() -- the generic ->rs485_config() callback, i.e.
 * the handler for TIOCSRS485. No port DCL opens is put into RS485 mode, so
 * p->em485 is NULL, the em485 code paths are not entered, and nothing is
 * waiting for a callback that will not come. If a stage ahead does bring up
 * RS485 (an iMX8MP or RPi transceiver is exactly the hardware this is for),
 * this header is the thing to replace -- not the drivers, which are already
 * calling the right functions with the right arguments.
 *
 * Struct layout is DCL's own: 8250_port.c never touches a field of struct
 * hrtimer, it only calls hrtimer_setup/start/cancel/try_to_cancel and returns
 * HRTIMER_NORESTART from the callbacks, so nothing here needs to agree with
 * mainline's node/expires layout. See <linux/serial_core.h> for why the same
 * freedom is NOT taken with struct uart_port -- that one is laid out by
 * designated initialisers in the driver, field by field.
 */

#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/timerqueue.h>	/* struct timerqueue_node -- hrtimer.node below */

/* mainline: uapi/asm-generic/posix_types.h gives clockid_t as an int. DCL has
 * no posix_types.h, so the typedef lands here, next to the clock IDs it
 * indexes. */
typedef int clockid_t;

/*
 * Callback result. HRTIMER_NORESTART is 0 so a callback that simply returns
 * does the safe thing; 8250_port.c's two handlers choose between them on
 * whether more turnaround work is pending.
 */
enum hrtimer_restart {
	HRTIMER_NORESTART = 0,
	HRTIMER_RESTART   = 1,
};

/*
 * ABS/REL is what 8250_port.c passes (REL throughout: "wait n nanoseconds
 * from now"). PINNED and the _SOFT variants are mainline's set, kept because
 * they are part of the same value space -- an arm of hrtimer_mode is read
 * back by nothing today, but a half-populated enum invites the next port to
 * pass a number that means nothing.
 */
enum hrtimer_mode {
	HRTIMER_MODE_ABS      = 0x00,
	HRTIMER_MODE_REL      = 0x01,
	HRTIMER_MODE_PINNED   = 0x02,
	HRTIMER_MODE_SOFT     = 0x04,
	HRTIMER_MODE_ABS_SOFT = HRTIMER_MODE_ABS | HRTIMER_MODE_SOFT,
	HRTIMER_MODE_REL_SOFT = HRTIMER_MODE_REL | HRTIMER_MODE_SOFT,
};

#define HRTIMER_STATE_INACTIVE 0
#define HRTIMER_STATE_ENQUEUED 1

struct hrtimer {
	/*
	 * The expiry node, first because that is where mainline puts it.
	 *
	 * It was not in this struct until drm_vblank.c:2311 needed it: that
	 * line reads `vtimer->timer.node.expires` to re-read an expiry until
	 * two reads agree, which is a member access and cannot be answered by
	 * a stub -- the value is the point of the loop. `expires` is at the
	 * position and of the type mainline uses, so a real queue could be
	 * dropped in underneath without that read site changing.
	 */
	struct timerqueue_node node;
	/* HRTIMER_MODE_* the arm used; recorded so the (future) queue can
	 * decide absolute vs relative without the caller repeating it. */
	enum hrtimer_mode mode;
	/* Absolute target time for ABS, delta for REL -- as mainline stores
	 * it, so a real queue could be dropped in without touching callers. */
	ktime_t _softexpires;
	enum hrtimer_restart (*function)(struct hrtimer* timer);
	int state;
	int pending;
};

/*
 * All four are static inline rather than defined in a shim .c: there is no
 * shared state to put in a shim (no queue, no lock, no clock base), and
 * inlining keeps a driver TU from gaining an unresolved symbol it did not
 * need. If a queue ever appears, they move to a .c and these disappear.
 */
static inline void hrtimer_setup(
    struct hrtimer* timer,
    enum hrtimer_restart (*function)(struct hrtimer* timer),
    clockid_t clock_id, enum hrtimer_mode mode)
{
	(void)clock_id;		/* CLOCK_MONOTONIC only; see header comment */
	timer->mode = mode;
	timer->function = function;
	timer->_softexpires = 0;
	timer->state = HRTIMER_STATE_INACTIVE;
	timer->pending = 0;
}

static inline void hrtimer_start(struct hrtimer* timer, ktime_t tim,
				 enum hrtimer_mode mode)
{
	timer->mode = mode;
	timer->_softexpires = tim;
	/*
	 * node.expires is the absolute expiry, as mainline stores it: for REL
	 * that is now + tim.  drm_crtc_vblank_get_vblank_timeout()
	 * (drm_vblank.c:2296) reads this member back as the next vblank
	 * timestamp, so leaving it at zero would hand the timestamping path
	 * a 1970 timestamp and a nonsense subtraction; ktime_get() is the
	 * same generic-timer clock that stamps the vblank counts, which is
	 * what makes that subtraction meaningful.
	 */
	timer->node.expires = (mode & HRTIMER_MODE_REL)
			      ? ktime_add(ktime_get(), tim) : tim;
	timer->state = HRTIMER_STATE_ENQUEUED;
	timer->pending = 1;
}

/*
 * -1 means "running, do not delete" on mainline and is what hrtimer_cancel()
 * retries on. DCL never runs a callback, so nothing can be running and 0/1
 * (was it armed) is always the whole answer -- the return shape is kept
 * because hrtimer_cancel()'s callers test it.
 */
static inline int hrtimer_try_to_cancel(struct hrtimer* timer)
{
	int was_pending = timer->pending;
	timer->pending = 0;
	timer->state = HRTIMER_STATE_INACTIVE;
	return was_pending;
}

static inline int hrtimer_cancel(struct hrtimer* timer)
{
	return hrtimer_try_to_cancel(timer);
}

/*
 * hrtimer_active() -- mainline (kernel/time/hrtimer.c) runs a seqcount-
 * protected read of "state != INACTIVE || the callback is running".  No
 * callback can be running here, so the state is the whole answer, and
 * hrtimer_try_to_cancel() above clears it -- the loop at drm_vblank.c:2238
 * (`while (hrtimer_active()) hrtimer_try_to_cancel();`) therefore exits
 * after one pass, which is the outcome its comment asks for ("wait for the
 * previous cancel operation to finish" -- the cancel has taken effect).
 */
static inline bool hrtimer_active(const struct hrtimer* timer)
{
	return timer->state != HRTIMER_STATE_INACTIVE;
}

/*
 * hrtimer_forward_now() -- "expire interval from now", mainline's wrapper
 * around hrtimer_forward() with the clock base's current time.  DCL has no
 * base and never invokes a callback (the only caller is
 * drm_vblank_timer_function(), the vblank timer's own callback), so this
 * re-arms the stored expiry the way the REL arm of hrtimer_start() above
 * does and returns 1.  The return is mainline's overrun count, and 1 is the
 * normal-path answer its caller expects -- it logs only when the value is
 * something else.
 */
static inline u64 hrtimer_forward_now(struct hrtimer* timer, ktime_t interval)
{
	timer->node.expires = ktime_add(ktime_get(), interval);
	return 1;
}

#endif /* __LINUX_HRTIMER_H__ */
