/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#include <stdint.h>
#include <_null.h>
#include <process.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Serv/sysserv.h>
#include <Sync/spinlock.h>
#include <Sync/futex.h>

/* Take futex_lock before the scheduler lock. Waiter publication, timeout
 * handling, and saved-frame updates share the scheduler lock, so a wake
 * cannot patch a frame after another core has resumed the waiter. */
static Spinlock futex_lock = { 0 };

static AuProcess* futex_current_process(AA64Thread* thr) {
	if (!thr)
		return NULL;
	AuProcess* proc = (AuProcess*)thr->procSlot;
	if (!proc)
		proc = AuProcessFindThread(thr);
	if (!proc)
		proc = AuProcessFindSubThread(thr);
	return proc;
}

static int futex_collect(AuProcess* proc, AA64Thread** list, int max) {
	int n = 0;
	if (!proc || !list)
		return 0;
	if (proc->main_thread && n < max)
		list[n++] = proc->main_thread;
	for (int i = 0; i < proc->num_thread && n < max; i++) {
		if (proc->threads[i])
			list[n++] = proc->threads[i];
	}
	return n;
}

static int futex_parse_timeout(void* timeout, int* timed, uint64_t* ms) {
	*timed = 0;
	*ms = 0;
	if (!timeout)
		return 0;
	int64_t sec = ((int64_t*)timeout)[0];
	int64_t nsec = ((int64_t*)timeout)[1];
	if (sec < 0 || nsec < 0 || nsec >= 1000000000LL)
		return -AU_FUTEX_EINVAL;
	if (sec == 0 && nsec == 0) {
		*timed = 1;
		*ms = 0;
		return 0;
	}
	uint64_t m = (uint64_t)sec * 1000ULL;
	m += ((uint64_t)nsec + 999999ULL) / 1000000ULL;
	if (m == 0)
		m = 1;
	*timed = 1;
	*ms = m;
	return 0;
}

/* Caller holds futex_lock and the scheduler lock. */
static void futex_wake_one(AA64Thread* thr) {
	if (!thr)
		return;
	if (__atomic_exchange_n(&thr->futex_waiting, 0, __ATOMIC_ACQ_REL) == 0)
		return;
	thr->futex_result = 0;
	/* If the waiter has already stored its SVC frame, patching the saved
	 * x0/x6 here is what makes it return 0 from the syscall.  A wake that
	 * wins the race before AuScheduleThread stores the frame is handled by
	 * the futex_active hook in AuScheduleThread. */
	if (thr->futex_frame_valid && thr->sp) {
		AA64Registers* frame = (AA64Registers*)thr->sp;
		frame->x0 = 0;
		frame->x6 = 0;
		thr->futex_frame_valid = 0;
	}
	AuThreadMakeReady(thr);
}

int AuFutexWait(AuProcess* proc, AA64Thread* thr, uint32_t* uaddr,
				uint32_t val, uint32_t bitset, int timed,
				uint64_t timeout_ms) {
	if (!proc || !thr || !uaddr || ((uint64_t)uaddr & 3))
		return -AU_FUTEX_EINVAL;
	if (bitset == 0)
		return -AU_FUTEX_EINVAL;
	if (timed && timeout_ms == 0)
		return -AU_FUTEX_ETIMEDOUT;

	AA64Registers* regs = AA64GetCurrentRegCtx();
	if (!regs)
		return -AU_FUTEX_EINVAL;

	/* The Linux ABI reports its own ETIMEDOUT (110), while the native PE
	 * ABI uses 116.  The value is published through the saved frame, so
	 * choose it here rather than relying on the syscall wrapper. */
	int timeout_result = -AU_FUTEX_ETIMEDOUT;
	if (proc->type_flags & PROCESS_TYPE_LINUX)
		timeout_result = -110;

	AuAcquireSpinlock(&futex_lock);
	uint64_t sched_daif = AuSchedLock();

	/* Re-check the user word under the lock.  A wake that raced us will
	 * have changed it, so we return EAGAIN instead of sleeping forever. */
	if (*(volatile uint32_t*)uaddr != val) {
		AuSchedUnlock(sched_daif);
		AuReleaseSpinlock(&futex_lock);
		return -AU_FUTEX_EAGAIN;
	}

	thr->futex_uaddr = (uint64_t)uaddr;
	thr->futex_bitset = bitset;
	thr->futex_result = timed ? timeout_result : 0;
	thr->futex_active = 1;
	thr->futex_frame_valid = 0;
	__atomic_store_n(&thr->futex_waiting, 1, __ATOMIC_RELEASE);

	if (timed)
		AuSleepThread(thr, timeout_ms);
	else
		AuBlockThread(thr);

	/* Release before scheduling: a wake racing this path will find
	 * futex_waiting set and move the thread to the ready list.  IRQs are
	 * still masked in the syscall path, so no timer can intervene between
	 * the release and AuScheduleThread. */
	AuSchedUnlock(sched_daif);
	AuReleaseSpinlock(&futex_lock);

	/* Does not return for a real user thread: AuScheduleThread restores the
	 * saved exception frame and resumes at EL0 with futex_result in x6. */
	AuScheduleThread(regs);

	/* Only reached when the scheduler is not running (early boot tests). */
	__atomic_store_n(&thr->futex_waiting, 0, __ATOMIC_RELEASE);
	thr->futex_active = 0;
	return timed ? -AU_FUTEX_ETIMEDOUT : 0;
}

int AuFutexWake(AuProcess* proc, uint32_t* uaddr, int nwake, uint32_t bitset) {
	if (!proc || !uaddr || ((uint64_t)uaddr & 3) || bitset == 0)
		return -AU_FUTEX_EINVAL;
	if (nwake <= 0)
		return 0;

	AA64Thread* list[MAX_THREADS_PER_PROCESS + 1];
	int n = 0;
	int woke = 0;

	AuAcquireSpinlock(&futex_lock);
	uint64_t sched_daif = AuSchedLock();
	n = futex_collect(proc, list, MAX_THREADS_PER_PROCESS + 1);
	for (int i = 0; i < n && woke < nwake; i++) {
		AA64Thread* thr = list[i];
		if (!thr->futex_waiting || thr->futex_uaddr != (uint64_t)uaddr)
			continue;
		if ((thr->futex_bitset & bitset) == 0)
			continue;
		futex_wake_one(thr);
		woke++;
	}
	AuSchedUnlock(sched_daif);
	AuReleaseSpinlock(&futex_lock);
	return woke;
}

int AuFutexRequeue(AuProcess* proc, uint32_t* uaddr, int nwake,
				   int nrequeue, uint32_t* uaddr2, uint32_t cmpval,
				   int has_cmp) {
	if (!proc || !uaddr || !uaddr2 || ((uint64_t)uaddr & 3) ||
		((uint64_t)uaddr2 & 3) || nrequeue < 0)
		return -AU_FUTEX_EINVAL;

	AA64Thread* list[MAX_THREADS_PER_PROCESS + 1];
	int n = 0;
	int woke = 0;
	int moved = 0;

	AuAcquireSpinlock(&futex_lock);
	uint64_t sched_daif = AuSchedLock();
	if (has_cmp && *(volatile uint32_t*)uaddr != cmpval) {
		AuSchedUnlock(sched_daif);
		AuReleaseSpinlock(&futex_lock);
		return -AU_FUTEX_EAGAIN;
	}
	n = futex_collect(proc, list, MAX_THREADS_PER_PROCESS + 1);
	for (int i = 0; i < n; i++) {
		AA64Thread* thr = list[i];
		if (!thr->futex_waiting || thr->futex_uaddr != (uint64_t)uaddr)
			continue;
		if (woke < nwake) {
			futex_wake_one(thr);
			woke++;
		} else if (moved < nrequeue) {
			thr->futex_uaddr = (uint64_t)uaddr2;
			moved++;
		} else {
			break;
		}
	}
	AuSchedUnlock(sched_daif);
	AuReleaseSpinlock(&futex_lock);
	return woke;
}

int64_t AuFutexSyscall(int64_t uaddr, int64_t op, int64_t val,
					   int64_t timeout, int64_t uaddr2, int64_t val3) {
	AA64Thread* thr = AuGetCurrentThread();
	AuProcess* proc = futex_current_process(thr);
	if (!proc || !thr)
		return -AU_FUTEX_EINVAL;

	uint32_t* addr = (uint32_t*)uaddr;
	int cmd = (int)op & AU_FUTEX_CMD_MASK;

	if (cmd == AU_FUTEX_WAKE || cmd == AU_FUTEX_WAKE_BITSET) {
		uint32_t bitset = (cmd == AU_FUTEX_WAKE) ? 0xffffffffu : (uint32_t)val3;
		if (cmd == AU_FUTEX_WAKE_BITSET && bitset == 0)
			return -AU_FUTEX_EINVAL;
		return AuFutexWake(proc, addr, (int)val, bitset);
	}

	if (cmd == AU_FUTEX_REQUEUE || cmd == AU_FUTEX_CMP_REQUEUE) {
		int nrequeue = (int)timeout;
		return AuFutexRequeue(proc, addr, (int)val, nrequeue,
							  (uint32_t*)uaddr2, (uint32_t)val3,
							  cmd == AU_FUTEX_CMP_REQUEUE);
	}

	if (cmd == AU_FUTEX_WAIT || cmd == AU_FUTEX_WAIT_BITSET) {
		uint32_t bitset = (cmd == AU_FUTEX_WAIT) ? 0xffffffffu : (uint32_t)val3;
		if (cmd == AU_FUTEX_WAIT_BITSET && bitset == 0)
			return -AU_FUTEX_EINVAL;
		int timed = 0;
		uint64_t ms = 0;
		int r = futex_parse_timeout((void*)timeout, &timed, &ms);
		if (r < 0)
			return r;
		return AuFutexWait(proc, thr, addr, (uint32_t)val, bitset, timed, ms);
	}

	return -AU_FUTEX_ENOSYS;
}
