/**
* @file sched_edf.c
*
* Earliest-Deadline-First real-time class for the AA64 scheduler.
*
* Model: implicit-deadline periodic tasks. An EDF thread publishes
* (period, wcet); each job released every period has absolute deadline
* release + period. Admission control enforces total utilization <= 1
* (Q16 fixed point), so admitted tasks are schedulable on one CPU.
*
* Two-class design: EDF threads strictly preempt best-effort threads,
* which keep the existing round-robin behavior untouched. Threads that
* never call AuEDFSetParams see zero behavior change.
*
* No WCET enforcement (cooperative): an overrunning job is charged a
* deadline miss and re-armed, never killed. Timebase is the CNTV
* microsecond counter (AuGetCurrentUS), tick granularity for wakeups.
**/

#include <stdint.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <timer.h>
#include <_null.h>

extern AA64Thread* thread_list_head;
extern AA64Thread* blocked_thr_head;
extern AA64Thread* _idle_thr;

/* admitted utilization, Q16 (0x10000 == 100%) */
static uint64_t edf_util_q16;

static uint64_t edf_job_util_q16(uint64_t period_us, uint64_t wcet_us) {
	return ((uint64_t)wcet_us << 16) / period_us;
}

/**
 * @brief AuEDFSetParams -- admit a thread to the EDF class
 * @param thread -- target thread (usually self)
 * @param period_us -- period == relative deadline, must be > 0
 * @param wcet_us -- worst-case execution time, 0 < wcet <= period
 * @return true if admitted (deadline armed at now + period)
 */
bool AuEDFSetParams(AA64Thread* thread, uint64_t period_us, uint64_t wcet_us) {
	if (!thread || thread == _idle_thr)
		return false;
	if (period_us == 0 || wcet_us == 0 || wcet_us > period_us)
		return false;
	uint64_t add = edf_job_util_q16(period_us, wcet_us);
	uint64_t d = AuSchedLock();
	uint64_t old = thread->edf_enabled ?
		edf_job_util_q16(thread->edf_period_us, thread->edf_wcet_us) : 0;
	if (edf_util_q16 - old + add > 0x10000ULL) {
		AuSchedUnlock(d);
		return false;
	}
	edf_util_q16 = edf_util_q16 - old + add;
	uint64_t now = AuGetCurrentUS();
	thread->edf_period_us = period_us;
	thread->edf_wcet_us = wcet_us;
	thread->edf_release_us = now;
	thread->edf_deadline_us = now + period_us;
	thread->edf_misses = 0;
	thread->edf_waiting = 0;
	thread->edf_enabled = 1;
	AuSchedUnlock(d);
	return true;
}

/**
 * @brief AuEDFRemove -- drop a thread's EDF reservation
 */
void AuEDFRemove(AA64Thread* thread) {
	if (!thread || !thread->edf_enabled)
		return;
	uint64_t d = AuSchedLock();
	edf_util_q16 -= edf_job_util_q16(thread->edf_period_us, thread->edf_wcet_us);
	thread->edf_enabled = 0;
	thread->edf_waiting = 0;
	AuSchedUnlock(d);
}

/**
 * @brief AuEDFWaitPeriod -- end current job, sleep until next release
 * Must be called by the EDF thread itself (thread context, not IRQ).
 * Counts a deadline miss when the job overran, catches up whole missed
 * periods, then blocks until the next release and yields the CPU.
 */
void AuEDFWaitPeriod(AA64Thread* thread) {
	if (!thread || !thread->edf_enabled)
		return;
	uint64_t now = AuGetCurrentUS();
	if (now > thread->edf_deadline_us)
		thread->edf_misses++;
	thread->edf_release_us += thread->edf_period_us;
	if (thread->edf_release_us <= now) {
		uint64_t skipped =
			(now - thread->edf_release_us) / thread->edf_period_us + 1;
		thread->edf_misses += skipped;
		thread->edf_release_us += skipped * thread->edf_period_us;
	}
	thread->edf_deadline_us = thread->edf_release_us + thread->edf_period_us;
	thread->edf_waiting = 1;
	AuBlockThread(thread);
	AuScheduleNext();
	/* AuScheduleNext resumes us via aa64_restore_sp + ret, which never
	 * restores PSTATE (only eret does), so we come back with IRQs still
	 * masked from the tick handler. Unmask or the timer can never
	 * preempt us again and tick-time freezes forever. */
	enable_irqs();
}

/**
 * @brief AuEDFHandleReleases -- wake EDF threads whose release arrived
 * Called once per timer tick from AuScheduleThread. Accepts BLOCKED and
 * LEFT_IN_KERNEL: AuScheduleNext() (used to yield after WaitPeriod)
 * always parks as LEFT_IN_KERNEL, mirroring AuHandleSleepThreads which
 * likewise wakes sleepers out of that state -- the cooperative resume
 * path in AuScheduleThread restores them correctly.
 */
void AuEDFHandleReleases(uint64_t now_us) {
	AA64Thread* thr = blocked_thr_head;
	while (thr != NULL) {
		AA64Thread* next = thr->next;
		if (thr->edf_enabled && thr->edf_waiting &&
			(thr->state == THREAD_STATE_BLOCKED ||
				thr->state == THREAD_STATE_LEFT_IN_KERNEL) &&
			now_us >= thr->edf_release_us) {
			thr->edf_waiting = 0;
			AuUnblockThread(thr);
		}
		thr = next;
	}
}

/**
 * @brief AuEDFPickNext -- earliest absolute deadline among runnable EDF threads
 * Accepts READY and LEFT_IN_KERNEL (parked by AuScheduleNext): the
 * LEFT_IN_KERNEL resume branch in AuScheduleThread restores parked
 * threads from their cooperative frame either way.
 * @return winner, or NULL when no EDF thread is runnable
 */
AA64Thread* AuEDFPickNext(void) {
	AA64Thread* best = NULL;
	for (AA64Thread* t = thread_list_head; t != NULL; t = t->next) {
		if (t == _idle_thr || !t->edf_enabled)
			continue;
		if (t->state != THREAD_STATE_READY &&
			t->state != THREAD_STATE_LEFT_IN_KERNEL)
			continue;
		if (!best || t->edf_deadline_us < best->edf_deadline_us ||
			(t->edf_deadline_us == best->edf_deadline_us &&
				t->thread_id < best->thread_id))
			best = t;
	}
	return best;
}

uint64_t AuEDFGetMisses(AA64Thread* thread) {
	return thread ? thread->edf_misses : 0;
}

/* Boot self-test: one periodic RT thread (200ms / 5ms), five jobs of
 * ~2ms spin, then log PASS and park forever. Fail-safe: if releases
 * never fire it stays blocked and the rest of the system is unaffected. */
static void AuEDFSelfTestEntry(uint64_t ctx) {
	(void)ctx;
	/* kthreads start with IRQs masked (spsr 0x3C5); idle unmasks
	 * explicitly and so must we, or the timer tick can never preempt
	 * the spin below and time freezes forever. */
	enable_irqs();
	AA64Thread* self = AuGetCurrentThread();
	if (!AuEDFSetParams(self, 200000ULL, 5000ULL)) {
		UARTDebugOut("[edf-test]: admission FAILED\n");
		AuBlockThread(self);
		AuScheduleNext();
		while (1) {}
	}
	for (int i = 0; i < 5; i++) {
		uint64_t t0 = AuGetCurrentUS();
		volatile uint64_t spin = 0;
		while (AuGetCurrentUS() - t0 < 2000ULL)
			spin++;
		(void)spin;
		UARTDebugOut("[edf-test]: job %d done misses=%d\n", i, (int)self->edf_misses);
		AuEDFWaitPeriod(self);
	}
	UARTDebugOut("[edf-test]: PASS misses=%d\n", (int)self->edf_misses);
	AuEDFRemove(self);
	AuBlockThread(self);
	AuScheduleNext();
	while (1) {}
}

void AuEDFSelfTestStart(void) {
	AA64Thread* idle = AuGetIdleThread();
	if (!idle)
		return;
	AuCreateKthread(AuEDFSelfTestEntry, (uint64_t*)idle->pml, "edftest");
}
