#ifndef __LINUX_WAIT_H__
#define __LINUX_WAIT_H__

/*
 * DCL <linux/wait.h> -- wait queues, with an honest answer to the one thing
 * this port cannot do: sleep.
 *
 * There is no native equivalent in Xeneva and no scheduler to block in, so
 * the queue is built for real -- the lists, the entries, the wake functions
 * and the bookkeeping are all genuine -- while the *waiting* is done by
 * polling, bounded. The distinction matters: what must not happen is a driver
 * that believes it slept and then loops on a condition that only ever changes
 * while it is spinning.
 *
 * Three call sites decide the design (the measured blocking surface of the
 * nine files: 7 wait-queue ops, 5 schedule()):
 *
 *   n_tty.c:2232 / :2354 -- blocking read and write, via
 *       DEFINE_WAIT_FUNC(wait, woken_wake_function) + add_wait_queue + a loop
 *       that ends in wait_woken(). Both are stage 5's EAGAIN-first cut; until
 *       that lands, an O_NONBLOCK fd returns -EAGAIN before reaching here
 *       (n_tty checks tty_io_nonblock() first), and a blocking fd with no
 *       data spins in wait_woken() rather than sleeping. The queue itself
 *       must therefore be correct -- it is what the cut will hang off.
 *
 *   serial_core.c:3194 -- wait_event(state->remove_wait,
 *       !atomic_read(&state->refcount)) during port removal. The condition is
 *       normally already true (refcount drops to zero on the line above), so
 *       the bounded poll sees it on the first test and returns.
 *
 *   serial_core.c:1205 / tty_io.c:3118 -- DECLARE_WAITQUEUE and
 *       init_waitqueue_head at construction, no waiting at all.
 *
 * wait_event() polls for DCL_WAIT_EVENT_MS and, if the condition still has
 * not come true, says so through dcl_wait_event_timeout() before falling
 * through. Mainline would block indefinitely there; falling through is the
 * wrong answer dressed as the right one, which is exactly why it is logged
 * instead of being quiet -- an unconditional block is a hung kernel and an
 * unconditional fall-through is a corrupt one, and neither should look like
 * a normal boot.
 *
 * add_wait_queue/remove_wait_queue are real list operations with no lock
 * held across them (DCL's spinlocks are no-ops -- see <linux/spinlock.h>),
 * so a wake arriving concurrently with an add is not excluded. That matches
 * the locking contract of everything else here.
 */

#include <linux/kernel.h>	/* mdelay (bounded polling) */
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/poll.h>		/* EPOLL* keys passed to wake_up_*_poll */
#include <linux/sched.h>	/* current, TASK_*, task_struct */

/*
 * Forward-declared before its first use in a parameter list: a struct tag
 * first named inside a function-pointer typedef's parameters has scope no
 * wider than that declarator, so the definition below would have become a
 * *different* struct and every assignment of a wake function to ->func would
 * have been a type mismatch. The compiler said so; this is the fix, not a
 * cast.
 */
struct wait_queue_entry;
struct wait_queue_head;

typedef int (*wait_queue_func_t)(struct wait_queue_entry* wq_entry,
				unsigned mode, int flags, void* key);

struct wait_queue_entry {
	unsigned int flags;
	void* private;
	wait_queue_func_t func;
	struct list_head entry;
};

struct wait_queue_head {
	spinlock_t lock;
	struct list_head head;
};

/*
 * wait_queue_head_t -- mainline's spelling. struct tty_struct carries two of
 * them (write_wait, read_wait) by value, and every ported header that names
 * a tty field has to see the typedef, not just the tag.
 */
typedef struct wait_queue_head wait_queue_head_t;

#define WQ_FLAG_EXCLUSIVE 0x01
#define WQ_FLAG_WOKEN     0x02

/* Poll budget for wait_event(): long enough that a device settling during
 * probe is caught, short enough that a condition that will never come cannot
 * hold the kernel. */
#define DCL_WAIT_EVENT_MS 5000

static inline void init_waitqueue_head(struct wait_queue_head* wq_head)
{
	spin_lock_init(&wq_head->lock);
	INIT_LIST_HEAD(&wq_head->head);
}

#define DECLARE_WAIT_QUEUE_HEAD(name)					\
	struct wait_queue_head name = { 0, LIST_HEAD_INIT(name.head) }

#define INIT_WAIT_QUEUE_HEAD(name) init_waitqueue_head(&(name))

int default_wake_function(struct wait_queue_entry* wq_entry,
				unsigned mode, int flags, void* key);
int woken_wake_function(struct wait_queue_entry* wq_entry,
				unsigned mode, int flags, void* key);

#define __WAITQUEUE_INITIALIZER(name, tsk) {				\
	.private	= (tsk),					\
	.func		= default_wake_function,			\
	.entry		= LIST_HEAD_INIT((name).entry),			\
}

#define DECLARE_WAITQUEUE(name, tsk)					\
	struct wait_queue_entry name = __WAITQUEUE_INITIALIZER(name, tsk)

/* n_tty.c:2189 and :2339 -- the form the tty read/write loops use. */
#define DEFINE_WAIT_FUNC(name, function)				\
	struct wait_queue_entry name = {				\
		.private	= current,				\
		.func		= (function),				\
		.entry		= LIST_HEAD_INIT((name).entry),		\
	}

static inline void init_waitqueue_entry(struct wait_queue_entry* wq_entry,
					struct task_struct* tsk)
{
	wq_entry->flags = 0;
	wq_entry->private = tsk;
	wq_entry->func = default_wake_function;
	INIT_LIST_HEAD(&wq_entry->entry);
}

static inline void init_waitqueue_func_entry(struct wait_queue_entry* wq_entry,
						wait_queue_func_t func)
{
	wq_entry->flags = 0;
	wq_entry->private = 0;
	wq_entry->func = func;
	INIT_LIST_HEAD(&wq_entry->entry);
}

void add_wait_queue(struct wait_queue_head* wq_head,
			struct wait_queue_entry* wq_entry);
void remove_wait_queue(struct wait_queue_head* wq_head,
			struct wait_queue_entry* wq_entry);

/* nr_exclusive == 0 wakes everyone; >0 stops after that many. */
void __wake_up(struct wait_queue_head* wq_head, unsigned int mode,
			int nr_exclusive, void* key);

#define wake_up(x)			__wake_up((x), TASK_NORMAL, 1, 0)
#define wake_up_all(x)			__wake_up((x), TASK_NORMAL, 0, 0)
#define wake_up_interruptible(x)	__wake_up((x), TASK_INTERRUPTIBLE, 1, 0)
#define wake_up_interruptible_all(x)	__wake_up((x), TASK_INTERRUPTIBLE, 0, 0)
#define wake_up_interruptible_sync(x)	__wake_up((x), TASK_INTERRUPTIBLE, 1, 0)
#define wake_up_poll(x, m)						\
	__wake_up((x), TASK_NORMAL, 1, (void*)(uintptr_t)(m))
#define wake_up_interruptible_poll(x, m)					\
	__wake_up((x), TASK_INTERRUPTIBLE, 1, (void*)(uintptr_t)(m))
#define wake_up_interruptible_sync_poll(x, m)				\
	__wake_up((x), TASK_INTERRUPTIBLE, 1, (void*)(uintptr_t)(m))

/*
 * Returns the entry's flags; n_tty discards the result (n_tty.c:2407) and
 * mainline's contract is "waited up to timeout, returns when woken or the
 * timeout expires". Here the timeout expires immediately, so the caller's
 * surrounding loop re-tests its own condition on the next pass -- which is
 * why that loop must be bounded elsewhere (see <linux/sched.h> on TIOCMWAIT,
 * and stage 5's cut on the two n_tty sites).
 */
unsigned int wait_woken(struct wait_queue_entry* wq_entry,
			unsigned int state, long timeout);

/* Called when a bounded wait_event() gives up, so falling through is visible
 * in the log instead of looking like a normal boot. */
void dcl_wait_event_timeout(const char* func);

/*
 * dcl_wait_pump() -- run pending device completions while the bounded poll
 * spins. Declared here, called from _dcl_wait_core below, defined in
 * DCL/virtio_shim.c beside virtio_poll_vqs().
 *
 * Why a wait has to pump at all: completions on this port are reaped by
 * polling -- the cdev read loop, or the caller's own loop -- and a driver's
 * *probe-time* waits have no such loop above them. virtio_gpu_init()'s
 * display-info wait is the first time it bites: the reply only moves when
 * the control queue is polled, nothing else polls before userspace runs,
 * and without this the condition could never come true inside the wait. The
 * call is idempotent and cheap (one used->idx read per live queue), so
 * running it under every wait_event is safe on the same terms the cdev
 * paths already pump.
 */
void dcl_wait_pump(void);

/*
 * _dcl_wait_core(wq_head, condition) -- the one polling loop every
 * wait_event* macro below is built from: DCL_WAIT_EVENT_MS passes of
 * mdelay(1) while the condition is false, dcl_wait_event_timeout() on the
 * way out, then the condition read one last time. Each pass first runs
 * dcl_wait_pump() (declared above), so a condition fed by a device
 * completion can arrive during the wait rather than only being re-tested.
 *
 * Spelled once because the four macros that use it differ only in what they
 * make of an expiry -- nothing, -ERESTARTSYS, 0, or the caller's own
 * timeout -- which is what each of their comments is about. Four copies of
 * the loop would let a change to the budget or the tick reach one variant
 * and not the others, and a caller cannot see that until a port stops
 * draining.
 *
 * Evaluates to 1 when the condition held and 0 when the budget ran out, so
 * the caller picks its own result instead of re-testing the condition: it is
 * read once per millisecond and once here, which is the side-effect-free
 * contract the note on wait_event() describes.
 */
#define _dcl_wait_core(wq_head, condition)				\
	({									\
		unsigned int _dcl_i = DCL_WAIT_EVENT_MS;			\
		int _dcl_ok;							\
		(void)(wq_head);						\
		while (!(condition) && _dcl_i) {				\
			_dcl_i--;						\
			dcl_wait_pump();					\
			mdelay(1);						\
		}								\
		_dcl_ok = !!(condition);					\
		if (!_dcl_ok)							\
			dcl_wait_event_timeout(__func__);			\
		_dcl_ok;							\
	})

/*
 * wait_event(wq, condition) -- poll, bounded.
 *
 * The condition is evaluated once per millisecond and once at the end; it
 * must therefore be side-effect free, which mainline's own wait_event()
 * requires too (it evaluates it on every wake as well).
 */
#define wait_event(wq_head, condition)					\
	do {									\
		(void)_dcl_wait_core(wq_head, condition);			\
	} while (0)


/*
 * wait_event_interruptible(wq, condition) -- tty_ioctl.c:484, which does
 *
 *     retval = wait_event_interruptible(tty->write_wait, !tty_chars_in_buffer(tty));
 *     if (retval < 0)
 *         return retval;
 *
 * so the value is the whole contract: 0 means "the buffer drained", negative
 * means "give up and unwind", and a 0 on timeout would let the caller proceed
 * to write with bytes still queued -- the exact thing the wait was for.
 *
 * DCL has no signal delivery to make a wait *interruptible* (signal_pending()
 * is 0, kernel.h), so the failure it can report is the one wait_event() above
 * already reports: DCL_WAIT_EVENT_MS expired with the condition still false.
 * That is spelled -ERESTARTSYS because that is the value this call site's
 * `retval < 0` branch is written against and the value mainline returns when
 * an interrupt does what a timeout does here.  The polling shape -- one
 * millisecond per pass, a bounded number of them, dcl_wait_event_timeout() on
 * the way out -- is wait_event()'s, deliberately: two different wait loops in
 * one header that disagreed about when they gave up would be worse than two
 * that agreed and differed only in their result type.
 */
#define wait_event_interruptible(wq_head, condition)			\
	({									\
		_dcl_wait_core(wq_head, condition)				\
			? 0 : -ERESTARTSYS;					\
	})

/*
 * wait_event_interruptible_timeout(wq, condition, timeout) --
 * tty_ioctl.c:184 in tty_wait_until_sent(), and its result is read three
 * different ways on the lines after:
 *
 *     timeout = wait_event_interruptible_timeout(tty->write_wait,
 *                             !tty_chars_in_buffer(tty), timeout);
 *     if (timeout <= 0)      return;      -- gave up
 *     if (timeout == MAX_SCHEDULE_TIMEOUT)
 *             timeout = 0;               -- the caller passed "wait forever"
 *
 * Hence returning the *original* timeout on success rather than a remaining
 * one.  mainline returns jiffies left, and here that would matter only if a
 * caller compared for equality -- which this one does, against
 * MAX_SCHEDULE_TIMEOUT, where the true remaining time is so close to the
 * original that mainline's own arithmetic lands on the same value.  Returning
 * the input preserves that equality for the infinite case, and for the finite
 * case the caller only tests `<= 0`, so an optimistic remainder cannot be
 * observed.  0 is the timeout, and -ERESTARTSYS never happens: there is no
 * signal, so the two exits are "condition met" and "bounded wait expired".
 *
 * The `timeout` argument is in jiffies on mainline; DCL has no jiffy clock on
 * this path and mdelay() is what wait_event() already spends, so the bounded
 * wait is DCL_WAIT_EVENT_MS like every other one here, and timeout's value
 * only ever travels through to the caller's own comparison.
 */
#define wait_event_interruptible_timeout(wq_head, condition, timeout)	\
	({									\
		long _dcl_t = (timeout);					\
		_dcl_wait_core(wq_head, condition)				\
			? _dcl_t : 0;						\
	})

/*
 * wait_event_timeout(wq_head, condition, timeout) -- the two drm vblank
 * waits, drm_atomic_helper.c:1916 and drm_vblank.c:1325.
 *
 * Same bounded poll and the same result convention as
 * wait_event_interruptible_timeout() above: the caller's timeout back when
 * the condition came true, 0 when the budget expired first.  Both call
 * sites test only `!ret` / `ret == 0` -- drm_crtc_wait_one_vblank() maps the
 * 0 onto -ETIMEDOUT -- so no remainder is ever read.  mainline floors a
 * success at 1 (`timeout ?: 1`); returning the input unchanged is what the
 * sibling macro above chose, so this one follows it rather than giving two
 * answers for one argument in the same header.
 */
#define wait_event_timeout(wq_head, condition, timeout)			\
	({									\
		long _dcl_t = (timeout);					\
		_dcl_wait_core(wq_head, condition)				\
			? _dcl_t : 0;						\
	})

/*
 * wait_event_lock_irq(wq_head, condition, lock) -- drm_vblank_work.c:229
 * and :250, which hold dev->event_lock across the wait and pass it to the
 * macro so mainline's __wait_event_lock_irq() can drop it around each
 * schedule() and take it again on wake.  DCL's spinlocks are no-ops with
 * nothing to drop (see <linux/spinlock.h>), so `lock` is unused; what
 * remains is wait_event()'s bounded poll.  The contract mainline states --
 * the lock is held on entry and still held on return -- is then trivially
 * kept, because the lock was never actually released.
 */
#define wait_event_lock_irq(wq_head, condition, lock)			\
	do {									\
		(void)(lock);						\
		(void)_dcl_wait_core(wq_head, condition);			\
	} while (0)


/*
 * DEFINE_WAIT(name) -- mainline's spelling for the waiter declared *inside*
 * the function that waits, as opposed to DECLARE_WAITQUEUE's (which takes the
 * task as an argument).  One user so far: tty_port.c:504, the `wait` that
 * tty_port_block_til_ready() brackets its whole loop with.
 *
 * The initialiser is __WAITQUEUE_INITIALIZER with `current` filled in, which
 * is the entire difference from DECLARE_WAITQUEUE -- and it is safe to write
 * as a static initialiser only because the declaration is a local one: a
 * block-scope struct may be initialised with a non-constant expression, and
 * `current` is dcl_current(), a call.  The same macro used at file scope
 * would not compile; nothing does.
 *
 * mainline: include/linux/wait.h.
 */
#define DEFINE_WAIT(name)						\
	struct wait_queue_entry name = __WAITQUEUE_INITIALIZER(name, current)

/*
 * prepare_to_wait() / finish_wait() -- the two calls that bracket a wait
 * loop, at tty_port.c:542 and :569.
 *
 * The queue work is the part that has to be real, and it is: the entry goes
 * on the queue only if it is not already there, which is what mainline does
 * and what stops a loop that re-prepares each pass from queueing itself a
 * second time -- and it comes off in finish_wait(), which is what stops a
 * later wake_up_interruptible() from walking an entry whose stack frame has
 * already been returned through.  Both reuse the add/remove this header
 * already declares (bodies in DCL/linux_irq_shim.c) rather than open-coding
 * list operations, so there is one answer for where on the queue an entry
 * goes instead of two that could disagree.
 *
 * The state work is the no-op set_current_state from <linux/sched.h>, and
 * that is the same bargain wait_event() above strikes: there is no task state
 * to change because no task blocks here.  The queue is genuine so the wake-up
 * is genuine; the *waiting* is the bounded poll the caller does after this
 * returns.  What would be wrong is an entry left queued and a caller that
 * believed it had slept.
 *
 * mainline: kernel/sched/wait.c.
 */
static inline void prepare_to_wait(struct wait_queue_head* wq_head,
				   struct wait_queue_entry* wq_entry,
				   int state)
{
	if (list_empty(&wq_entry->entry))
		add_wait_queue(wq_head, wq_entry);
	set_current_state(state);
}

static inline void finish_wait(struct wait_queue_head* wq_head,
			       struct wait_queue_entry* wq_entry)
{
	if (!list_empty(&wq_entry->entry))
		remove_wait_queue(wq_head, wq_entry);
	__set_current_state(TASK_RUNNING);
}

/*
 * wait_event_freezable(wq_head, condition) -- the freezer-aware wait.
 *
 * virtio_console.c:752 and :784 both write it, inside wait_port_readable()
 * and wait_port_writable(), where mainline's version additionally lets a
 * suspend in progress abort the wait with -ERESTARTSYS.
 *
 * DCL has no refrigerator: nothing freezes a task, so the half of mainline's
 * contract that responds to freezing has nothing to respond *to*. The other
 * half -- poll the condition, give up after a bounded budget -- is what
 * wait_event() above already does, and this is that loop and nothing else,
 * deliberately: two waits in one header that gave up at different times would
 * be a difference a caller could not see until a port stopped draining.
 *
 * The shape (one millisecond per pass, DCL_WAIT_EVENT_MS of them,
 * dcl_wait_event_timeout() on the way out) is wait_event()'s, so a reader who
 * knows one of them knows both.
 *
 * It returns an int, which is the part the previous note here got wrong when
 * it said "returns nothing, because mainline's does not either" -- mainline's
 * __wait_event_freezable() does return one, and both call sites treat the
 * value as the whole contract:
 *
 *     ret = wait_event_freezable(port->waitqueue, !will_read_block(port));
 *     if (ret < 0)
 *         return ret;
 *
 * A `do {} while (0)` there is not a value to assign, so the call would not
 * compile at all, which is how the mistake surfaced. The two arms are the
 * ones wait_event_interruptible() above already chose and for the same
 * reason: 0 when the condition came true, -ERESTARTSYS when the bounded
 * budget expired first. A 0 on the second arm would walk past the wait with
 * the port still empty and hand read() a success with nothing in it; the
 * callers' `ret < 0` branch is written for exactly the case that produces.
 */
#define wait_event_freezable(wq_head, condition)				\
	({									\
		_dcl_wait_core(wq_head, condition)				\
			? 0 : -ERESTARTSYS;					\
	})
#endif /* __LINUX_WAIT_H__ */
