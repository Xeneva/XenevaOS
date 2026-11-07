/*
 * DCL/linux_kthread_shim.c -- kthread workers over the DCL workqueue.
 *
 * The vendored drm core runs its vblank and atomic-helper deferred work on
 * mainline's kthread_worker: drm_vblank_work.c creates one per CRTC with
 * kthread_run_worker() and queues/flushes/cancels through the four externs
 * below.  Xeneva has no scheduler to host a kthread, and DCL's workqueue
 * already runs work->func synchronously where it is queued (see
 * linux/workqueue.h) -- which collapses the worker to a trampoline:
 * kthread_queue_work() is the dispatch, and flush means "the work already
 * ran, there is nothing pending" because a pending state never existed.
 *
 * That is a real semantic narrowing and it is safe for one reason: mainline's
 * pending-work list (kthread_work.node) is checked under dev->event_lock by
 * drm_vblank_work_cancel_sync(), and with queueing synchronous the node is
 * always empty there -- the cancel answers "nothing was pending", which is
 * exactly true.  The vblank work itself therefore runs inside whatever
 * context queued it (the hrtimer callback or the flip handler), under spin
 * locks that are no-ops on this build.
 *
 * The rest of the file is the three primitives that belong beside it rather
 * than in a shim of their own:
 *
 *   completions -- the drm atomic helpers wait on commit milestones with
 *   complete_all()/try_wait_for_completion()/wait_for_completion*().  The
 *   state is the one `done` word <linux/completion.h> defines and the waits
 *   are the two observable states described there; the bodies are below,
 *   beside complete()/wait_for_completion_killable() in
 *   DCL/linux_kmod_shim.c which treat the same word the same way.
 *
 *   sched_set_fifo() -- drm_vblank_work.c promotes its worker to a
 *   real-time scheduling class; there is one task and no policy to set, so
 *   the call is the symbol and nothing else.
 *
 *   dcl_current_work/current_work() -- <linux/workqueue.h> declares the pair
 *   its inline runner maintains and drm_probe_helper.c reads; both were
 *   undefined at link, and the definitions are at the bottom with the note on
 *   why the live pointer is the only honest answer.
 */
#include <linux/kthread.h>	/* the worker structs and the macro surface */
#include <linux/workqueue.h>	/* current_work()/dcl_current_work -- bottom */
#include <linux/completion.h>	/* the waiters below */
#include <linux/limits.h>	/* UINT_MAX -- complete_all()'s saturation */
#include <linux/err.h>		/* ERR_PTR */
#include <linux/list.h>	/* INIT_LIST_HEAD */
#include <linux/slab.h>	/* kmalloc, kfree */
#include <linux/string.h>	/* memset */

/*
 * __kthread_init_worker(): mainline sets the name, lockdep class and the
 * two lists.  The name and key are lockdep currency and DCL's lockdep is
 * inert (lockdep.h); the lists are zeroed by the memset and then pointed
 * at themselves, which is the state mainline's queue walk expects even
 * though nothing here walks them.
 */
void __kthread_init_worker(struct kthread_worker *worker, const char *name,
			   struct lock_class_key *key)
{
	(void)name;
	(void)key;
	memset(worker, 0, sizeof(*worker));
	INIT_LIST_HEAD(&worker->work_list);
	INIT_LIST_HEAD(&worker->delayed_work_list);
}

/*
 * kthread_create_worker_on_node(): allocate the worker and leave it
 * "stopped".  worker->task stays NULL -- the kthread_run_worker() macro
 * calls wake_up_process() on it, and DCL's wake_up_process() takes any
 * pointer including NULL and does nothing (sched.h:139).  The return is a
 * plain pointer, never ERR_PTR(-ENOMEM) unless the allocation failed,
 * which is the only arm the macro tests.
 */
struct kthread_worker *kthread_create_worker_on_node(unsigned int flags,
						     int node,
						     const char namefmt[], ...)
{
	struct kthread_worker *worker;

	(void)flags;
	(void)node;
	worker = (struct kthread_worker *)kmalloc(sizeof(*worker), GFP_KERNEL);
	if (!worker)
		return ERR_PTR(-ENOMEM);
	__kthread_init_worker(worker, namefmt, NULL);
	return worker;
}

/*
 * kthread_queue_work(): the dispatch.  work->worker doubles as the
 * "already running" guard mainline uses against a second queue of the same
 * work, and current_work is set around the call so a work that inspects
 * its own worker (mainline's kthread_current_work()) sees itself.  Returns
 * true when the work ran, which is the bool mainline returns for "queued".
 */
bool kthread_queue_work(struct kthread_worker *worker,
			struct kthread_work *work)
{
	if (!worker || !work || !work->func)
		return false;
	if (work->worker)
		return false;

	work->worker = worker;
	worker->current_work = work;
	work->func(work);
	worker->current_work = NULL;
	work->worker = NULL;
	return true;
}

/*
 * Flush/cancel: the work has already run by the time anything asks, so
 * flush is a no-op and cancel reports "nothing was pending" (false).
 * drm_vblank_work.c:205 takes that false as "no queued work to drop" and
 * leaves its own event-list bookkeeping -- which already found the list
 * empty -- alone.
 */
void kthread_flush_work(struct kthread_work *work)
{
	(void)work;
}

void kthread_flush_worker(struct kthread_worker *worker)
{
	(void)worker;
}

bool kthread_cancel_work_sync(struct kthread_work *work)
{
	(void)work;
	return false;
}

/*
 * kthread_destroy_worker() -- mainline flushes, stops the kthread and frees
 * the worker.  Here the worker has no kthread (->task is NULL by
 * construction -- see kthread_create_worker_on_node()) and no pending work
 * (queueing runs the work synchronously), so the kmalloc() made at creation
 * is the only thing left to release.  mainline's WARN_ON(!task) is not
 * copied because task-less is the normal state on this build, not an error.
 */
void kthread_destroy_worker(struct kthread_worker *worker)
{
	kfree(worker);
}

/*
 * ── completions ──────────────────────────────────────────────────────────
 *
 * mainline's kernel/sched/completion.c over this tree's one-field struct.
 * complete()/complete_all() set the word; the waiters consume it or report
 * the failure path mainline's callers are written against (see
 * <linux/completion.h> for why that is the right pair of exits here).
 * The spinlock mainline takes around the word is not missed: DCL's
 * spinlocks are no-ops and nothing else can run concurrently, which is the
 * same argument complete() in DCL/linux_kmod_shim.c relies on.
 */

void complete_all(struct completion *x)
{
	if (!x)
		return;
	/* mainline's saturation: `done` stays done, so every later waiter
	 * (try_wait_for_completion() included) keeps succeeding. */
	x->done = UINT_MAX;
}

/*
 * Consume one pending completion. The saturated complete_all() count is never
 * decremented, so every later waiter keeps succeeding (mainline's rule); a
 * NULL or exhausted completion is the failure path.
 */
static bool dcl_completion_consume(struct completion *x)
{
	if (!x || !x->done)
		return false;
	if (x->done != UINT_MAX)
		x->done--;
	return true;
}

bool try_wait_for_completion(struct completion *x)
{
	return dcl_completion_consume(x);
}

unsigned long wait_for_completion_timeout(struct completion *x,
					  unsigned long timeout)
{
	/* A miss is mainline's timeout path -- drm turns it into -ETIMEDOUT
	 * and a drm_err(). */
	if (!dcl_completion_consume(x))
		return 0;
	/* No time passes here, so the remaining timeout is the one passed
	 * in; mainline's `timeout ?: 1` floor keeps "at least 1 on
	 * success". */
	return timeout ? timeout : 1;
}

int wait_for_completion_interruptible(struct completion *x)
{
	/* The failure this wait can report: Xeneva delivers no signal, so
	 * the value is mainline's interrupted one, which is what the
	 * caller's `if (ret) return ret;` unwinds on. Blocking instead
	 * would spin forever -- nothing in this context completes the
	 * commit being waited for. */
	return dcl_completion_consume(x) ? 0 : -ERESTARTSYS;
}

long wait_for_completion_interruptible_timeout(struct completion *x,
					       unsigned long timeout)
{
	if (!dcl_completion_consume(x))
		return 0;
	return timeout ? (long)timeout : 1;
}

/*
 * ── scheduling ───────────────────────────────────────────────────────────
 */

/*
 * sched_set_fifo() -- mainline (kernel/sched/core.c) puts the task on
 * SCHED_FIFO at priority 1.  DCL has a single task and no policy bit, so
 * there is nothing to set; the symbol exists because drm_vblank_work.c:290
 * hands it the vblank worker's task.  That task is NULL by construction
 * (kthread_create_worker_on_node() leaves ->task unset), which a real
 * sched_set_fifo() would reject -- another reason the DCL body must not
 * try to model one.
 */
void sched_set_fifo(struct task_struct *p)
{
	(void)p;
}

/*
 * ── workqueue identity ───────────────────────────────────────────────────
 */

/*
 * dcl_current_work / current_work() -- <linux/workqueue.h> declares the pair
 * and its __dcl_run_work() saves and restores the pointer around every inline
 * run, but nothing in the tree defined either, so both were undefined at
 * link: the variable from tty_io_shim.o (tty_hangup() -> schedule_work()) and
 * the function from drm_probe_helper.o (drm_kms_helper_is_poll_worker()).
 *
 * current_work() is what makes that one caller's question answerable here.
 * drm_kms_helper_is_poll_worker() asks "is the work running right now the
 * output poll work?" by comparing ->func against output_poll_execute, and the
 * poll work is run inline by mod_delayed_work() (drm_probe_helper.c:629):
 * while it runs, __dcl_run_work() has the poll work_struct installed, so the
 * comparison matches exactly as it does on mainline's kworker, and from any
 * other context the pointer is NULL so the answer is false.  A definition
 * that returned NULL always would make the poll path unable to recognise
 * itself; one that returned the last-run work would make every later caller
 * think it was that work.  This is the live value or nothing.
 *
 * One global rather than per-task state, like the rest of DCL (there is one
 * task); zero-initialised, which is the "no work on the stack" answer.
 */
struct work_struct* dcl_current_work;

struct work_struct* current_work(void)
{
	return dcl_current_work;
}
