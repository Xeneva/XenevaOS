#ifndef __WORKQUEUE_H__
#define __WORKQUEUE_H__

#include <linux/timer.h>

/*
 * might_alloc(gfp) -- mainline keeps this annotation in <linux/sched/mm.h>:
 * it asserts the flags allow blocking, then might_sleep()s if they do.  DCL's
 * might_sleep() is already the no-op at kernel.h:216 for the same reason
 * (nothing here can sleep), so the whole annotation is nothing.
 *
 * It is defined here, of all places, because of who calls it: xarray.h's
 * static inline bodies do, at :594 and thirteen more sites through :1059.
 * With no definition in scope the build's -Wno-implicit-function-declaration
 * made each call an external reference, which is the undefined `might_alloc`
 * lld-link reported for drm_minor_alloc() (drm_drv.c) and drm_syncobj.c.
 * mainline's home for it, <linux/sched/mm.h>, is a stand-in with nothing in it
 * (xarray.h:21 includes it and gets no more than the include), and the callers
 * are xarray.h bodies, so the definition has to come from a header xarray.h
 * itself already includes: this one, through bitmap.h (:12) -> kernel.h ->
 * here (kernel.h:128), which is before any of those bodies is parsed on every
 * include path.
 *
 * A macro and not a typed inline like mainline's: gfp_t is a kernel.h:55
 * typedef, and this header is parsed from headers that have their own reason
 * to be included before kernel.h; the annotation is worth neither the
 * dependency nor a type the body does not read.
 */
#define might_alloc(gfp)		do { (void)(gfp); } while (0)

/*
 * Forward declaration *before* the typedef. Without it, `struct work_struct`
 * inside the parameter list is first declared in that prototype's scope, so it
 * is a different type from the one defined four lines down -- and every later
 * use reports "incompatible pointer types passing 'struct work_struct *' to
 * parameter of type 'struct work_struct *'", two identically-spelled types
 * that are not the same type. It surfaces only in translation units that
 * reach this header before anything else has named the tag, which is exactly
 * what the tty headers do (linux/tty.h -> workqueue.h).
 */
struct work_struct;
typedef void (*work_func_t)(struct work_struct* work);

struct work_struct {
    work_func_t     func;
    void* data;          /* your private context */
    int             pending;
};

struct workqueue_struct {
    const char* name;
    /* stub � single threaded on bare metal */
};

/* Init */
#define INIT_WORK(_work, _func) do {    \
    (_work)->func    = (_func);         \
    (_work)->pending = 0;               \
    (_work)->data    = NULL;            \
} while (0)

/*
 * INIT_WORK_ONSTACK() / destroy_work_on_stack() -- mainline's ONSTACK pair.
 * It differs from INIT_WORK only in lockdep/debug-object bookkeeping:
 * init_work(work, true) marks the object stack-allocated, and
 * destroy_work_on_stack() frees the debug object at the end of scope
 * (include/linux/workqueue.h:312 and :271). DCL's lockdep is inert and there
 * is no debug-object table, so the pair is INIT_WORK and nothing. The pair
 * exists because drm_framebuffer.c:464 and :819 bracket
 * drm_mode_rmfb_work_fn() with it.
 */
#define INIT_WORK_ONSTACK(_work, _func) INIT_WORK(_work, _func)
#define destroy_work_on_stack(work) do { (void)(work); } while (0)

/*
 * work_pending(work) -- mainline tests WORK_STRUCT_PENDING_BIT in the packed
 * work word; DCL's struct carries the flag directly. Nothing sets it: a
 * queue is a synchronous run (below), so a work that was queued is never
 * still pending when something can ask. vkms_crtc.c:84/:98 WARN if a
 * vkms_crtc_state is torn down with its composer work pending, and inline
 * execution is what makes that structurally false rather than lucky.
 */
static inline bool work_pending(struct work_struct* work)
{
    return work->pending != 0;
}

/*
 * dcl_current_work -- the work whose func is on the stack right now, saved
 * and restored around every inline run below. current_work() returns it so
 * mainline's "am I running on a work item?" tests answer the way they do on
 * a real kworker; drm_probe_helper.c:868 compares ->func against
 * output_poll_execute, and with the poll work executed inline from
 * mod_delayed_work() that comparison must still see the poll work.
 *
 * One pointer rather than per-task state, like the rest of DCL (there is one
 * task). Defined in DCL/linux_kthread_shim.c:259, next to current_work().
 */
extern struct work_struct* dcl_current_work;

struct work_struct* current_work(void);

static inline bool __dcl_run_work(struct work_struct* work)
{
    struct work_struct* prev = dcl_current_work;

    dcl_current_work = work;
    if (work->func)
        work->func(work);
    dcl_current_work = prev;
    return true;
}

/*
 * Work runs inline: there is no kworker to run it later.
 *
 * These are functions, not statement-expressions, because mainline's callers
 * use their return values -- tty_buffer.c:66 is literally
 * `return queue_work(flip_wq ?: system_dfl_wq, &buf->work);`, which does not
 * parse against a do{}while(0) macro. The bool is "work was queued"; since
 * it runs to completion before we return, true is always the right answer.
 *
 * Inline execution is a real deviation from mainline and it is worth being
 * precise about why it is safe here: tty_flip_buffer_push() calls this from
 * interrupt context, so flush_to_ldisc() runs before tty_flip_buffer_push()
 * returns rather than on a worker later. Its only lock is the buffer mutex,
 * which in DCL is a no-op (<linux/mutex.h> says so plainly), so there is
 * nothing to deadlock on. This is also what Linux itself did before 2.6.24,
 * when tty_flip_buffer_push() called the flush directly. The behaviour that
 * changes is latency, not correctness: a byte arrives at n_tty before the
 * interrupt handler returns instead of after.
 */
static inline bool schedule_work(struct work_struct* work)
{
    __dcl_run_work(work);
    return true;
}

static inline bool queue_work(struct workqueue_struct* wq,
                              struct work_struct* work)
{
    (void)wq;
    __dcl_run_work(work);
    return true;
}

/*
 * flush_work() answers true: the work has run (it always has -- it ran
 * inline). cancel_work_sync() answers false: there was never anything
 * pending to cancel. tty_buffer_cancel_work() returns that straight through,
 * and false is the truthful value for "did this cancel something?".
 */
static inline bool flush_work(struct work_struct* work)
{
    (void)work;
    return true;
}

static inline bool cancel_work_sync(struct work_struct* work)
{
    (void)work;
    return false;
}

static inline void flush_workqueue(struct workqueue_struct* wq) { (void)wq; }
static inline void destroy_workqueue(struct workqueue_struct* wq) { (void)wq; }

/*
 * system_dfl_wq -- mainline's "default" workqueue, the one queue_work() falls
 * back to when a tty port has not been linked to a driver-specific one. DCL
 * has exactly one workqueue, so this is a named pointer whose identity exists
 * only so `flip_wq ?: system_dfl_wq` (tty_buffer.c:66) has something to
 * choose.
 *
 * It is a *pointer*, not an object, for a mechanical reason worth recording:
 * `E1 ?: E2` applies the usual arithmetic conversions to its operands and does
 * not decay a struct lvalue. Declaring it as `struct workqueue_struct
 * system_dfl_wq` (the object spelling) makes line 66 an error -- "incompatible
 * operand types ('struct workqueue_struct *' and 'struct workqueue_struct')" --
 * because the second operand has no way to become a pointer. mainline declares
 * every system_*wq the same way, as `extern struct workqueue_struct *`.
 *
 * Defined in DCL/tty_shim.c.
 */
extern struct workqueue_struct* system_dfl_wq;

/*
 * system_percpu_wq -- mainline's per-cpu queue, and the @wq argument of the
 * two drm call sites that name one (drm_probe_helper.c:629's output poll,
 * drm_self_refresh_helper.c:221's self-refresh entry). Same situation as
 * system_dfl_wq above, one step further removed: nothing here dereferences it
 * either, because mod_delayed_work() ignores the pointer just as
 * queue_delayed_work() does. mainline keeps it in kernel/workqueue.c; there is
 * no such file to keep it in, since <linux/workqueue.h> has no implementation
 * behind it.
 *
 * Defined in DCL/tty_shim.c, beside system_dfl_wq.
 */
extern struct workqueue_struct* system_percpu_wq;

/*
 * system_long_wq -- mainline's queue for work that may run long, which is
 * where drm_writeback.c:570 puts a writeback job's cleanup_work. The
 * distinction mainline draws (a bounded pool, versus per-cpu, versus
 * unbound) is about CPU affinity and how long a worker may be held; none of
 * it survives a runner that executes work inline on the caller, which is
 * what makes this the third name for the same object and not a fourth queue.
 * It is declared last because it was the last of the three asked for.
 *
 * Defined in DCL/tty_shim.c, beside the other two.
 */
extern struct workqueue_struct* system_long_wq;

static inline struct workqueue_struct*
alloc_ordered_workqueue(const char* name, int flags) {
    static struct workqueue_struct wq;
    (void)flags;	/* no priority/ordering on a single inline queue */
    wq.name = name;
    return &wq;
}

#define create_singlethread_workqueue(name) \
    alloc_ordered_workqueue(name, 0)

/* Get owning struct back from work pointer */
#define container_of_work(ptr, type, member) \
    container_of(ptr, type, member)

struct delayed_work {
    struct work_struct  work;
    struct timer_list   timer;
    struct workqueue_struct* wq;
};

#define INIT_DELAYED_WORK(_dwork, _func) do {   \
    INIT_WORK(&(_dwork)->work, _func);           \
    timer_setup(&(_dwork)->timer, NULL, 0);      \
    (_dwork)->wq = NULL;                         \
} while (0)

/* On bare metal -- execute immediately for bring-up */
static inline bool schedule_delayed_work(struct delayed_work* dwork,
    unsigned long delay) {
    (void)delay;	/* no timer wheel: the delay is not honoured, see below */
    __dcl_run_work(&dwork->work);
    return true;
}

static inline bool queue_delayed_work(struct workqueue_struct* wq,
    struct delayed_work* dwork,
    unsigned long delay) {
    (void)wq;
    (void)delay;
    __dcl_run_work(&dwork->work);
    return true;
}

/*
 * mod_delayed_work() -- mainline re-arms work that may already be pending and
 * reports whether it found it that way. There is nothing to re-arm: no timer
 * wheel to move (see schedule_delayed_work above), so "run this, possibly
 * later" and "run this now" are the only two states this tree has, and both
 * are the second one. It forwards to queue_delayed_work() rather than
 * duplicating the inline run, so the two spellings cannot drift apart.
 *
 * The true return is mainline's "yes, it was already pending". Both callers in
 * the tree discard it, and it is the answer that matches what has just
 * happened -- the work has run, which is the state a caller checking for true
 * would go on to expect. Returning false would mean "nothing was queued", for
 * a call that did queue something.
 */
static inline bool mod_delayed_work(struct workqueue_struct* wq,
    struct delayed_work* dwork,
    unsigned long delay) {
    return queue_delayed_work(wq, dwork, delay);
}

static inline bool cancel_delayed_work(struct delayed_work* dwork) {
    del_timer(&dwork->timer);
    dwork->work.pending = 0;
    return true;
}

static inline bool cancel_delayed_work_sync(struct delayed_work* dwork) {
    return cancel_delayed_work(dwork);
}

static inline bool flush_delayed_work(struct delayed_work* dwork) {
    (void)dwork;	/* work already ran inline; nothing pending to flush */
    return true;
}

/* Get owning struct from delayed_work pointer */
#define to_delayed_work(ptr) \
    container_of(ptr, struct delayed_work, work)
#endif
