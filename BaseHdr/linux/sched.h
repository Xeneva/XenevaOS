#ifndef __LINUX_SCHED_H__
#define __LINUX_SCHED_H__

/*
 * DCL <linux/sched.h> -- task state, `current`, and schedule().
 *
 * Xeneva has no sleeping scheduler for kernel context: there is nothing to
 * switch away to, so `schedule()` cannot do what mainline's does. Pretending
 * otherwise would be the worst kind of wrongness here -- a driver that
 * believes it slept would loop forever on a condition that never arrives.
 * Instead the honest shape is kept and the sleeps are made *short and
 * bounded*, which is what the tty code actually needs from them:
 *
 *   schedule()
 *       Sleeps a millisecond. tty_open() retries on -EAGAIN with
 *       `schedule(); goto retry_open;` (tty_io.c:2127 and :2153) -- with a
 *       no-op that becomes a hard spin against a condition only an
 *       interrupt can change, so a millisecond of yield is the difference
 *       between a retry loop and a locked-up console.
 *
 *   signal_pending() / fatal_signal_pending()
 *       Always 0. Xeneva has no signals to deliver, so no wait can be broken
 *       out of this way. That is recorded rather than papered over, because
 *       it has one real consequence: uart_wait_modem_status() (serial_core.c:
 *       1200) loops until a modem-status interrupt or a signal, so TIOCMWAIT
 *       on a port that never raises one never returns. Closing that needs a
 *       bounded wait at the serial_core call site -- stage 4's list, not a
 *       lie in this header.
 *
 *   current
 *       One task_struct per kernel context, with `comm` filled from the
 *       running thread. `current` is an object-like macro, exactly as on
 *       mainline, which means a struct member literally named `current`
 *       would not compile against this header. Nothing in the tty/serial
 *       set has one (the only `file->current` in the tree is Xeneva's own
 *       loader and FAT code, which do not include DCL headers).
 *
 *       `signal->tty` is NULL: it is read by tty_io.c:2280 in tiocsti(), the
 *       TIOCSTI gate, and only there. NULL makes TIOCSTI answer -EPERM for a
 *       non-admin, which is the safe direction -- TIOCSTI injects keystrokes
 *       into another terminal and is disabled by default upstream for
 *       exactly that reason.
 *
 * task_lock()/task_unlock() are the one guard tty_io.c takes directly
 * (guard(task_lock)(p) at :3046), so the class lives here with the primitive.
 */

#include <linux/kernel.h>
#include <linux/cleanup.h>
/*
 * linux/kthread.h (drm_vblank_work.c) includes this header and nothing
 * else, and mainline's sched.h is wide enough to already carry all of
 * this: the lock types and class key its worker API names, NUMA_NO_NODE
 * for kthread_create_worker, and forward declarations so `const struct
 * cpumask *` parameters do not get prototype-scope tags.  timer/list/
 * memset come through kernel.h above.
 */
#include <linux/spinlock.h>	/* raw_spinlock_t in struct kthread_worker */
#include <linux/lockdep.h>	/* lock_class_key in kthread_init_worker */
#include <linux/nodemask_types.h>	/* NUMA_NO_NODE -- the type header, not
					 * nodemask.h: the bitmap API it carries is
					 * nothing this header uses */

/*
 * <linux/pid.h> -- task_tgid() and task_pid_nr(), the two task-identity
 * accessors drm_auth.c:236 and drm_ioctl.c:883/:922/:930 call on `current`.
 * mainline's fs.h carries pid.h and DCL's file.h does the same (file.h:68),
 * but neither drm file includes file.h, so without it here those calls were
 * implicit declarations -- permitted by the build's
 * -Wno-implicit-function-declaration -- and surfaced at link time as
 * undefined task_tgid (drm_auth.o) and task_pid_nr (drm_ioctl.o).
 *
 * Riding on sched.h is safe in either include order, which is what the two
 * headers have to agree on: pid.h includes nothing, so no cycle back to this
 * file is possible, and its own guard makes the include a no-op in the drm
 * files that get it from file.h.  The accessors are static inlines over
 * forward-declared types (pid.h:25-26), so no struct is needed before this
 * point either.
 */
#include <linux/pid.h>

struct cpumask;

#define TASK_RUNNING		0
#define TASK_INTERRUPTIBLE	1
#define TASK_UNINTERRUPTIBLE	2
#define TASK_NORMAL		(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)
#define TASK_WAKEKILL		4
#define __TASK_STOPPED		8
#define TASK_PARKED		16

/* Nothing ever waits this long, because nothing ever really waits at all. */
#define MAX_SCHEDULE_TIMEOUT ((long)(~0UL >> 1))

/*
 * PF_KTHREAD / ->worker_private: tsk_is_kthread() in <linux/kthread.h>
 * tests both, and kthread_create_worker() publishes the worker on the
 * creating (kernel) task.  DCL's task is the one shared task (see below),
 * so the flag starts set -- every DCL thread is a kernel thread -- and
 * worker_private is the slot kthread.h expects it to live in.
 */
#define PF_KTHREAD	0x00200000U

struct tty_struct;
struct task_struct;

struct signal_struct {
	struct tty_struct* tty;
};

/*
 * mainline spells this as an enum (sched.h:325) so BPF can see it; DCL has
 * no BPF, but the spelling is what drm_device.h:48 reaches for when it sizes
 * the wedge task's `comm`, and sizing task_struct's own below with the same
 * name keeps the two from drifting apart.
 */
enum {
	TASK_COMM_LEN = 16,
};

struct task_struct {
	char comm[TASK_COMM_LEN];
	int pid;
	struct signal_struct* signal;
	unsigned int flags;		/* PF_* -- PF_KTHREAD is set (see above) */
	void* worker_private;		/* kthread.h: struct kthread * owner */
};

/*
 * The one DCL task. It is shared rather than per-thread: `current` is read
 * for its `comm` (two deprecation warnings) and its `signal->tty` (one
 * permission gate), never written by any of the nine files, so a single
 * context whose `comm` is refreshed on each read answers truthfully without
 * a task table that nothing else needs.
 */
struct task_struct* dcl_current(void);
/*
 * #undef first: <linux/mm.h> carries a fallback `current ((void*)0)` for the
 * headers that need the name without a scheduler. Which of the two lands
 * second depends on the include order of the translation unit, and an
 * unconditional redefinition is a -Wmacro-redefined warning at best and, at
 * worst, `((void*)0)->comm` if mm.h wins. Redefining unconditionally is the
 * right behaviour -- dcl_current() is the true one and mm.h's is only ever a
 * fallback -- so drop whatever is there and put this one in, in both orders.
 */
#undef current
#define current (dcl_current())

/*
 * task_pid_vnr() -- drm_ioctl.c:199's drm_getclient() stores it into
 * struct drm_client and hands that back to userspace.
 *
 * mainline answers "the task's pid as current's namespace sees it"; DCL has
 * no pid table, and <linux/pid.h> records what follows from that for the
 * accessors it owns (task_tgid() is NULL, task_pid_nr() is 0 for a debug line
 * whose text is compiled away).  A value userspace reads back is the other
 * case, and the task is not anonymous: dcl_current() refreshes ->pid from the
 * running Xeneva thread on every call (DCL/current_ident.c), so that real
 * thread id is what this reports.  The NULL arm cannot come from any caller
 * (they all pass `current`); 0 is the one answer that touches nothing.
 *
 * A static inline in the shape of pid.h's accessors rather than a shim
 * symbol, for the same reason those are: the body is one field read, and a
 * link symbol for it would be a call to a function a header could have
 * compiled away.
 */
static inline int task_pid_vnr(struct task_struct* tsk)
{
	return tsk ? tsk->pid : 0;
}

/*
 * get_task_comm(buf, tsk) -- mainline's accessor for a task's comm, which
 * mainline also declares in <linux/sched.h> (there as a macro over
 * __get_task_comm()). One caller, the virtio-gpu debug-name ioctl, and the
 * copy is bounded by the struct's own `comm` array so the terminator
 * always fits. `comm` is exactly what dcl_current() refreshes from the
 * running thread on every call, so a live task's name is real text; the
 * shape is this header's static-inline style rather than a shim symbol,
 * for the task_pid_vnr() reason above.
 */
static inline char* get_task_comm(char* buf, struct task_struct* tsk)
{
	unsigned int i;

	for (i = 0; i + 1 < sizeof(tsk->comm) && tsk->comm[i]; i++)
		buf[i] = tsk->comm[i];
	buf[i] = 0;
	return buf;
}

void schedule(void);

/*
 * wake_up_process(tsk) -- tty_ldsem.c:97/:124/:245, where a waiter that has
 * parked on the semaphore is told its turn came.
 *
 * mainline takes the task off the runqueue's sleep state and returns whether
 * it found one.  Here it returns 1 and does nothing else, and the reason it
 * is safe for the semaphore to work at all is what schedule() is on this
 * build: DCL/linux_irq_shim.c:350 parks in mdelay(1) and returns, so no task
 * is ever *asleep* waiting for this -- the waiter in tty_ldsem.c's
 * down_read_failed() loop re-evaluates its condition every millisecond
 * whether or not anyone wakes it, and `wake_up_process` on a task that is
 * already runnable is a no-op upstream too.  A real wake-up would need a
 * scheduler with a sleep state to wake out of.
 *
 * Returning 1 rather than void is mainline's signature: the callers discard
 * it, but a decl that answered a different type would be a different symbol
 * to a reader comparing this against tty_ldsem.c.
 */
static inline int wake_up_process(struct task_struct* tsk)
{
	(void)tsk;
	return 1;
}

/*
 * wake_up_state() -- dma-fence.c:796's dma_fence_default_wait_cb(), the
 * callback a signaled fence runs to wake the task mainline parked in
 * dma_fence_wait_timeout().  mainline's `state` names the sleep states the
 * wake is allowed to clear; DCL's one task is never asleep, so there is no
 * state to test against and the mask selects nothing.
 *
 * The 1 is wake_up_process()'s answer above and is chosen the same way: a
 * caller that does read it gets "the wake was carried out" instead of a state
 * test this kernel cannot make.  dma_fence_default_wait_cb() discards it, and
 * the wait the callback serves is the bounded one dma_fence_wait_timeout()
 * runs, not a sleep this could end early.
 */
static inline int wake_up_state(struct task_struct* tsk, unsigned int state)
{
	(void)tsk;
	(void)state;
	return 1;
}

long schedule_timeout(long timeout);
long schedule_timeout_killable(long timeout);

#define set_current_state(s)		do { (void)(s); } while (0)
#define __set_current_state(s)		do { (void)(s); } while (0)
#define set_task_state(t, s)		do { (void)(t); (void)(s); } while (0)
#define __set_task_state(t, s)		do { (void)(t); (void)(s); } while (0)

/*
 * signal_pending() -- always 0; Xeneva has no signals (see the definition in
 * DCL/linux_irq_shim.c, which returns 0 and records the consequence for
 * uart_wait_modem_status()).
 *
 * The declaration is guarded, and the reason is worth writing down because it
 * is invisible from either side: <linux/mm.h> also provides this name, as a
 * macro -- `#define signal_pending(p) 0` -- and in a translation unit where
 * mm.h arrives first (DCL/linux_mm_shim.c includes it at line 32, then
 * <linux/tty.h> at line 36, which reaches this file through tty_ldisc.h ->
 * wait.h), an unguarded `int signal_pending(struct task_struct* tsk);` is
 * expanded into `int 0(struct task_struct* tsk);` and clang reports
 * "expected identifier or '('" at this line with no mention of the macro that
 * did it. Guarding means: mm.h first, take its macro; this file first, take
 * the declaration. Either order compiles, and both answer the same value.
 */
#ifndef signal_pending
int signal_pending(struct task_struct* tsk);
#endif
int fatal_signal_pending(struct task_struct* tsk);

/*
 * cond_resched() -- mainline declares it in this header and defines it in
 * kernel/sched/core.c as a real preemption point.
 *
 * In DCL there is no preemption point to take, so it is the same no-op
 * <linux/mm.h> already provides, spelled identically and guarded the same way
 * so the two headers can be included in either order without a redefinition.
 *
 * It has to be visible from here rather than only from mm.h: tty_buffer.c
 * calls it (line 509, inside the flush_to_ldisc loop -- mainline's "yield if
 * the consumer has been running a while") and includes <linux/sched.h> but
 * never <linux/mm.h>. Without this, the call became an implicit declaration
 * -- the build flags permit those -- and surfaced as `lld-link: undefined
 * symbol: cond_resched` at link time instead of an error at the call site.
 */
#ifndef cond_resched
#define cond_resched() ((void)0)
#endif

/* tty_io.c:3046 -- the only task lock the tty core takes directly. */
void task_lock(struct task_struct* p);
void task_unlock(struct task_struct* p);

DCL_LOCK_GUARD(task_lock, struct task_struct, task_lock(_T), task_unlock(_T))


/*
 * schedule_timeout_interruptible() -- the declaration beside schedule_timeout
 * (mainline has both in this header), one user: tty_port.c:596, the yield in
 * the block_til_ready() loop.
 *
 * The body is in DCL/linux_irq_shim.c next to schedule_timeout()'s and simply
 * calls it, because the distinction the suffix carries does not exist here:
 * mainline's version differs only in that a SIGINT could end the wait early,
 * and <linux/sched.h> records at the top of this file that signal_pending() is
 * always 0.  With nothing to be interrupted by, two implementations would be
 * two copies of one behaviour -- and one that let them disagree would be a
 * caller waiting on a timeout the other had already clamped.
 */
long schedule_timeout_interruptible(long timeout);
#endif /* __LINUX_SCHED_H__ */
