/**
* @file sched.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/sched.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <_null.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <Hal/AA64/gic.h>
#include <aucon.h>
#include <process.h>
#include <aurora.h>
#include <signal.h>
#include <timer.h>
#include <Sync/spinlock.h>
#include <Serv/sysserv.h>

extern void aa64_store_context(AA64Thread* thr);
extern void store_syscall(AA64Thread* thr);
extern bool aa64_restore_context(AA64Thread* thr);
extern void aa64_restore_sp(AA64Thread* thr);
extern void aa64_schedule_init(AA64Thread* current, AA64Thread* init, uint64_t va);
extern void ret_from_syscall(AA64Thread* thr);
extern void aa64_resume_exception_frame(AA64Registers* regs, uint64_t elr_el1, uint64_t spsr_el1)
	__attribute__((noreturn));
bool AuSchedValidateLists(void);

/* Bounds every scheduler list walk so a corrupted/cyclic list can never
 * wedge a tick path in an unbounded traversal. --axiss */
#define SCHED_VALIDATE_STEPS 8192

extern void first_time_sex(AA64Thread* thr);
extern void first_time_sex2(AA64Thread* thr);
extern uint64_t read_sp();

AA64Thread* thread_list_head;
AA64Thread* thread_list_last;
AA64Thread* blocked_thr_head;
AA64Thread* blocked_thr_last;
AA64Thread* trash_thr_head;
AA64Thread* trash_thr_last;
AA64Thread* sleep_thr_head;
AA64Thread* sleep_thr_last;

AA64Thread* current_thread;
AA64Thread* _idle_thr;
uint64_t thread_id;
bool _scheduler_initialized;
uint64_t scheduler_tick;
static uint64_t _global_idle_time;

/* Serializes all scheduler list mutations. UP rule: thread context must
 * hold this with IRQs masked (SchedLock does both); the tick path runs
 * masked already. DAIF is save/restored so masked callers (fault path,
 * scheduler tail) are not wrongly unmasked. */
static Spinlock* s_sched_lock;

uint64_t AuSchedLock(void) {
	uint64_t d = 0;
	if (s_sched_lock) {
		d = read_daif();
		mask_irqs();
		AuAcquireSpinlock(s_sched_lock);
	}
	return d;
}

void AuSchedUnlock(uint64_t d) {
	if (s_sched_lock) {
		AuReleaseSpinlock(s_sched_lock);
		restore_daif(d);
	}
}

/* Unlocked cores: caller must hold the sched lock (or run masked in
 * the tick path, which is atomic against masked thread-side holders). */
static void schedLink(AA64Thread** head, AA64Thread** last, AA64Thread* t) {
	t->next = NULL;
	t->prev = NULL;
	if (*head == NULL) {
		*last = t;
		*head = t;
	} else {
		(*last)->next = t;
		t->prev = *last;
	}
	*last = t;
}

static void schedUnlink(AA64Thread** head, AA64Thread** last, AA64Thread* thread) {
	if (!thread || !head || !last || *head == NULL)
		return;
	if (thread->prev)
		thread->prev->next = thread->next;
	else if (*head == thread)
		*head = thread->next;
	if (thread->next)
		thread->next->prev = thread->prev;
	else if (*last == thread)
		*last = thread->prev;
	/* do NOT null out thread->next/prev here: AuHandleSleepThreads walks
	 * sleep_thr_head with `for (...; sleep_thr = sleep_thr->next)` and calls
	 * AuThreadDeleteSleep() mid-walk without saving next first -- clearing
	 * next here would truncate that traversal and skip threads later in the
	 * list on the same tick, same as the old per-list delete functions left
	 * these fields untouched --axiss */
}

void AuThreadInsert(AA64Thread* new_task) {
	uint64_t d = AuSchedLock();
	schedLink(&thread_list_head, &thread_list_last, new_task);
	AuSchedUnlock(d);
}

void AuThreadDelete(AA64Thread* thread) {
	uint64_t d = AuSchedLock();
	schedUnlink(&thread_list_head, &thread_list_last, thread);
	AuSchedUnlock(d);
}

void AuThreadInsertBlock(AA64Thread* new_task) {
	uint64_t d = AuSchedLock();
	schedLink(&blocked_thr_head, &blocked_thr_last, new_task);
	AuSchedUnlock(d);
}

void AuThreadDeleteBlock(AA64Thread* thread) {
	uint64_t d = AuSchedLock();
	schedUnlink(&blocked_thr_head, &blocked_thr_last, thread);
	AuSchedUnlock(d);
}

void AuThreadInsertTrash(AA64Thread* new_task) {
	uint64_t d = AuSchedLock();
	schedLink(&trash_thr_head, &trash_thr_last, new_task);
	AuSchedUnlock(d);
}

void AuThreadDeleteTrash(AA64Thread* thread) {
	uint64_t d = AuSchedLock();
	schedUnlink(&trash_thr_head, &trash_thr_last, thread);
	AuSchedUnlock(d);
}

void AuThreadInsertSleep(AA64Thread* new_task) {
	uint64_t d = AuSchedLock();
	schedLink(&sleep_thr_head, &sleep_thr_last, new_task);
	AuSchedUnlock(d);
}

void AuThreadDeleteSleep(AA64Thread* thread) {
	uint64_t d = AuSchedLock();
	schedUnlink(&sleep_thr_head, &sleep_thr_last, thread);
	AuSchedUnlock(d);
}

void AA64NextThread() {
	/* EDF real-time class strictly preempts best-effort round-robin.
	 * No EDF threads admitted -> AuEDFPickNext returns NULL and the
	 * legacy behavior below is untouched. */
	AA64Thread* edf = AuEDFPickNext();
	if (edf) {
		current_thread = edf;
		return;
	}
	/* Best-effort round-robin from current. Only READY and LEFT_IN_KERNEL
	 * threads may run (the latter resumes via its dedicated path in
	 * AuScheduleThread): resuming SLEEP/BLOCKED/KILLABLE threads here
	 * breaks whatever parked them. The walk is bounded so a cyclic list
	 * can never wedge the tick; with nothing runnable we park on idle. --axiss */
	AA64Thread* start =
		(current_thread && current_thread->next) ? current_thread->next : thread_list_head;

	AA64Thread* cursor = start; //current_thread ? current_thread->next : NULL;
	for (int i = 0; i < SCHED_VALIDATE_STEPS && cursor; i++) {
		if (cursor != _idle_thr &&
			(cursor->state == THREAD_STATE_READY || cursor->state == THREAD_STATE_LEFT_IN_KERNEL)) {
			current_thread = cursor;
			return;
		}

		cursor = cursor->next ? cursor->next : thread_list_head;
		if (cursor == start)
			break;
	}
	current_thread = _idle_thr;
}

/**
 * @brief AuCreateKthread -- create kernel thread 
 * @param entry -- Pointer to entry point
 * @param pml -- Pointer to Page directory
 * @param name -- Name of the thread
 * @return Pointer to newly created thread
 */
AA64Thread* AuCreateKthread(void (*entry)(uint64_t), uint64_t* pml, char* name) {
	AA64Thread* t = (AA64Thread*)kmalloc(sizeof(AA64Thread));
	memset(t, 0, sizeof(AA64Thread));
	strncpy(t->name, name, 8);
	t->name[7] = '\0';
	t->elr_el1 = (uint64_t)entry;
	t->x30 = (uint64_t)entry;
	/* running kernel trampolines at EL1h on their own kernel SP. EL1t wouldve
	 * run on SP_EL0 and aa64_enter_user would yank that stack out from
	 * under it before eret --axiss */
	/* spsr 0x345: EL1h with IRQ unmasked (D/A/F still masked). Kthreads
	 * used to start fully masked (0x3C5), which froze the timer for any
	 * thread that spun before explicitly enabling IRQs; list locking now
	 * makes preemption safe --axiss */
	t->spsr_el1 = 0x345;
	//t->sp = stack;
	t->pml = (uint64_t)pml;
	t->sp = AuCreateKernelStack((uint64_t*)t->pml);
	uint64_t kstack = t->sp;
	t->sp = ((uint64_t)kstack & ~(uint64_t)0xF);
	t->sp -= 64;
	t->originalKSp = t->sp;
	t->state = THREAD_STATE_READY;
	t->thread_id = thread_id++;
	t->fpsr = 0;
	t->fpcr = 0;
	t->waitlist = initialize_list();
	t->wait_ref_count = 0;
	AuSignalInitializeTrampoline(t);
	AuThreadInsert(t);
	return t;
}

/**
 * @brief AuCreateSubKthread -- create sub kernel thread of parent
 * kthread
 * @param entry -- Pointer to entry point
 * @param pml -- Pointer to Page directory
 * @param name -- Name of the thread
 * @return Pointer to newly created thread
 */
AA64Thread* AuCreateSubKthread(void (*entry)(uint64_t), uint64_t stack, uint64_t* pml, char* name) {
	AA64Thread* t = (AA64Thread*)kmalloc(sizeof(AA64Thread));
	memset(t, 0, sizeof(AA64Thread));
	strncpy(t->name, name, 8);
	t->name[7] = '\0';
	t->elr_el1 = (uint64_t)entry;
	t->x30 = (uint64_t)entry;
	/* EL1h, DAIF masked. This entry runs AuProcessEntSubThread and then
	 * aa64_enter_user, which installs the EL0 PSTATE. Leaving IRQs unmasked
	 * here (0x345) let the timer preempt a thread that had no exception
	 * frame yet. */
	t->spsr_el1 = 0x3C5;
	//t->sp = stack;
	t->pml = (uint64_t)pml;
	t->sp = stack;
	t->originalKSp = t->sp;
	t->state = THREAD_STATE_READY;
	t->thread_id = thread_id++;
	t->fpsr = 0;
	t->fpcr = 0;
	t->waitlist = initialize_list();
	//AuSignalInitializeTrampoline(t);
	AuThreadInsert(t);
	return t;
}

extern void PrintThreadInfo() {
	AA64Thread* thr = current_thread;
	UARTDebugOut("Saving thread : spsr %x \r\n", thr->spsr_el1);
	UARTDebugOut("ELR_EL1 : %x \r\n", thr->elr_el1);
	UARTDebugOut("SP : %x \r\n", thr->sp);
}

/* temporary freeze diagnostics: runs in idle thread context */
static void AuSchedHeartbeat(void) {
	static uint32_t idle_iters;
	if ((++idle_iters % 20000) != 0)
		return;
	int ready = 0, sleep = 0, blocked = 0, leftk = 0, other = 0;
	int sleeplist = 0, edfready = 0;
	char names[5][8];
	int states[5];
	int quantas[5];
	int shown = 0;
	uint64_t d = AuSchedLock();
	for (AA64Thread* t = thread_list_head; t != NULL; t = t->next) {
		if (t == _idle_thr)
			continue;
		switch (t->state) {
		case THREAD_STATE_READY:
			ready++;
			break;
		case THREAD_STATE_SLEEP:
			sleep++;
			break;
		case THREAD_STATE_BLOCKED:
			blocked++;
			break;
		case THREAD_STATE_LEFT_IN_KERNEL:
			leftk++;
			break;
		default:
			other++;
			break;
		}
		if (t->edf_enabled &&
			(t->state == THREAD_STATE_READY || t->state == THREAD_STATE_LEFT_IN_KERNEL))
			edfready++;
		if (shown < 5) {
			for (int i = 0; i < 8; i++)
				names[shown][i] = t->name[i];
			states[shown] = (int)t->state;
			quantas[shown] = (int)t->sleepQuanta;
			shown++;
		}
	}
	for (AA64Thread* t = sleep_thr_head; t != NULL; t = t->next)
		sleeplist++;
	AuSchedUnlock(d);
	/* Self-check: silent list corruption strands threads exactly the way a
	 * stuck home menu presented (runnable thread, never picked). Print only
	 * on failure; healthy boots stay quiet. --axiss */
	if (!AuSchedValidateLists())
		UARTDebugOut("[sched-dbg]: LISTS CORRUPT tick=%d\n", (int)scheduler_tick);
	UARTDebugOut(
		"[sched-dbg]: tick=%d ms=%d ready=%d sleep=%d blocked=%d leftk=%d other=%d sleeplist=%d \n",
		(int)scheduler_tick,
		(int)AuGetCurrentMS(),
		ready,
		sleep,
		blocked,
		leftk,
		other,
		sleeplist);
	if (edfready)
		UARTDebugOut("[sched-dbg]: edf_ready=%d\n", edfready);
	for (int i = 0; i < shown; i++)
		UARTDebugOut("[sched-dbg]: thr %s state=%d quanta=%d \n", names[i], states[i], quantas[i]);
}

/* the idle loop body, factored out of AuIdleThread so aa64_schedule_init can
 * jump straight back into it on originalKSp instead of restoring idle's
 * thread->sp: idle gets preempted by every timer tick, which constantly
 * overwrites thread->sp with a 256-byte eret-style exception-frame pointer
 * (see AuScheduleThread). aa64_schedule_init's own handoff format is a
 * completely different 208-byte callee-saved/ret-style frame. Idle was the
 * target of both, so whichever one last wrote thread->sp left the other
 * reading it as the wrong shape -- misinterpreting an eret frame as saved
 * x19-x30/ELR/SPSR and `ret`-ing to garbage. Idle has no meaningful
 * mid-loop state to preserve across a cooperative handoff, so the fix is to
 * stop trying to restore it at all and just re-enter fresh --axiss */
void AuIdleLoop(void) {
	while (1) {
		enable_irqs();
		AuSchedHeartbeat();
		_wfi();
	}
}

void AuIdleThread(uint64_t ctx) {
	mask_irqs();
	UARTDebugOut("Idle thread running \r\n");
	AuTextOut("Starting up Xeneva please wait...\r\n");
	_idle_thr->start_time_us = AuGetCurrentUS();
	enable_irqs();
	AuIdleLoop();
}

extern void resume_user(AA64Thread* thr, void* ksp);

void AuResumeUserThread() {
	AA64Thread* thr = current_thread;
	thr->x30 = thr->elr_el1;
	resume_user(thr, (void*)thr->sp);
	//aa64_enter_user(thr->sp, thr->elr_el1);
	while (1) {}
}

extern uint64_t read_x30();

extern void settimerdebug();

bool debug = 0;

void enscheddebug() {
	debug = 1;
}

void AuHandleSleepThreads() {
	uint64_t d = AuSchedLock();
	AA64Thread* sleep_thr;
	for (sleep_thr = sleep_thr_head; sleep_thr != NULL;) {
		/* schedLink below clears ->next: stash it first or the walk ends
		 * here and every later sleeper misses this tick. A zero quantum
		 * must wake, not wrap to ~forever. --axiss */
		AA64Thread* next = sleep_thr->next;
		/* sleepers wake on an absolute deadline: counting scheduler events made
		 * a "4000 ms" sleep last anywhere from 1 s to 20 s depending on load */
		if (sleep_thr->sleepQuanta > 0 && AuGetCurrentUS() >= sleep_thr->sleep_until_us)
			sleep_thr->sleepQuanta = 0;
		if (sleep_thr->sleepQuanta == 0) {
			//settimerdebug();
			if (sleep_thr->state != THREAD_STATE_LEFT_IN_KERNEL)
				sleep_thr->state = THREAD_STATE_READY;
			schedUnlink(&sleep_thr_head, &sleep_thr_last, sleep_thr);
			schedLink(&thread_list_head, &thread_list_last, sleep_thr);
		}
		sleep_thr = next;
	}
	AuSchedUnlock(d);
}

void PrintThrIn() {
	if (debug) {
		UARTDebugOut("Till here \r\n");
	}
}

void AuThreadSafeReturn(uint64_t rcx) {
	UARTDebugOut("Inside thread safe return \r\n");
	mask_irqs();
	AA64Thread* thr = current_thread;
	UARTDebugOut("Executing the first time sex again %s\r\n", thr->name);
	UARTDebugOut("Current EL : %d \r\n", _getCurrentEL());
	first_time_sex(thr);
	while (1) {}
}

/**
 * @brief AuScheduleThread -- the core of multi-tasking. It schedules
 * threads next to be runned
 * @param regs -- Passed by Timer ISR
 */
void AuScheduleThread(AA64Registers* regs) {
	if (_scheduler_initialized == 0 || !regs) {
		return;
	}
	mask_irqs();
	AA64Thread* runThr = current_thread;

	/* the vector wrapper already captured the full interrupted register set,
	 * so i treat this frame as the only valid resume point for a preempted
	 * thread, not a C call frame and not originalKSp --axiss */
	runThr->sp = (uint64_t)regs;
	runThr->elr_el1 = read_elr_el1();
	runThr->spsr_el1 = read_spsr_el1();
	runThr->x0 = regs->x0;
	runThr->x1 = regs->x1;
	runThr->x2 = regs->x2;
	runThr->x3 = regs->x3;
	runThr->x4 = regs->x4;
	runThr->x5 = regs->x5;
	runThr->x6 = regs->x6;
	runThr->x7 = regs->x7;
	runThr->x8 = regs->x8;
	runThr->x19 = regs->x19;
	runThr->x20 = regs->x20;
	runThr->x21 = regs->x21;
	runThr->x22 = regs->x22;
	runThr->x23 = regs->x23;
	runThr->x24 = regs->x24;
	runThr->x25 = regs->x25;
	runThr->x26 = regs->x26;
	runThr->x27 = regs->x27;
	runThr->x28 = regs->x28;
	runThr->x29 = regs->x29;
	runThr->x30 = regs->x30;
	runThr->linux_tls = aa64_read_tpidr_el0();
	runThr->justStored = true;

	/* The wrapper captured this state before entering C. Saving live q
	 * registers here is too late: IRQ dispatch and timer bookkeeping are
	 * compiled with NEON enabled and may already have clobbered them. */
	AA64FpFrame* interrupted_fp = AA64ExceptionFpFrame(regs);
	memcpy(runThr->fp_regs, interrupted_fp->q, sizeof(runThr->fp_regs));
	runThr->fpcr = interrupted_fp->fpcr;
	runThr->fpsr = interrupted_fp->fpsr;

	uint64_t now = AuGetCurrentUS();
	uint64_t delta = now - runThr->start_time_us;

	if (runThr->procSlot != NULL) {
		AuProcess* proc = (AuProcess*)runThr->procSlot;
		proc->total_runtime_us += delta;
		proc->window_runtime_us += delta;
	} else {
		_global_idle_time += delta;
	}

	/** change thread's running state to ready state */
	if (runThr->state == THREAD_STATE_RUNNING)
		runThr->state = THREAD_STATE_READY;

	scheduler_tick++;
	AuHandleSleepThreads();
	AuEDFHandleReleases(now);
	AA64NextThread();

	current_thread->start_time_us = now;

	/* mark this thread as running */
	if (current_thread->state == THREAD_STATE_READY)
		current_thread->state = THREAD_STATE_RUNNING;

	write_both_ttbr(V2P(current_thread->pml));

	//tlb_flush_vmalle1is();
	aa64_restore_fp(current_thread->fp_regs,
					(uint64_t*)&current_thread->fpcr,
					(uint64_t*)&current_thread->fpsr);
	dsb_sy_barrier();

	/** check if the thread was left somewhere in kernel space **/
	if (current_thread->state == THREAD_STATE_LEFT_IN_KERNEL) {
		current_thread->state = THREAD_STATE_READY;
		aa64_restore_sp(current_thread);
		/* should not reach here */
		for (;;)
			;
	}

	AuSignalDeliver(current_thread);
	aa64_set_tpidr_el0(current_thread->linux_tls);

	if (!current_thread->justStored) {
		/* first_time_sex (lol) installs the initial kernel entry, stack and
		 * PSTATE and never returns. user threads go through
		 * AuProcessEntUser instead, which builds their EL0 stack before
		 * its own eret --axiss */
		first_time_sex(current_thread); //unskippable function name holy shit --axiss
		__builtin_unreachable();
	}

	AA64Registers* return_frame = (AA64Registers*)current_thread->sp;
	current_thread->data = return_frame;
	aa64_resume_exception_frame(return_frame, current_thread->elr_el1, current_thread->spsr_el1);
	__builtin_unreachable();
}

int AuSchedYieldMs(uint64_t ms) {
	AA64Thread* thr;

	if (_scheduler_initialized == 0)
		return -1;
	thr = current_thread;
	if (!thr || thr == _idle_thr)
		return -1;
	if (ms == 0)
		ms = 1;
	AuSleepThread(thr, ms);
	AuScheduleNext();
	return 0;
}

/**
 * @brief AuScheduleNext -- forcefully schedule
 * to idle thread
 */
void AuScheduleNext() {
	if (_scheduler_initialized == 0)
		return;

	current_thread->state = THREAD_STATE_LEFT_IN_KERNEL;
	AA64Thread* storeThr = current_thread;
	aa64_store_fp(storeThr->fp_regs, &storeThr->fpcr, &storeThr->fpsr);
	__asm__ volatile("msr cntv_tval_el0, %0\n isb" ::"r"(1ULL));
	current_thread = _idle_thr;
	aa64_schedule_init(storeThr, current_thread, V2P(current_thread->pml));
}

static int ke_stack_idx;

/**
 * @brief AuCreateKernelStack -- maps kernel stack and return the top
 * of the stack, it only maps 4KiB of stack
 * @param pml -- Pointer to page directory
 * @return kernel stack address
 */
uint64_t AuCreateKernelStack(uint64_t* pml) {
	uint64_t location = KERNEL_STACK_LOCATION;
	location += (uint64_t)ke_stack_idx * KERNEL_STACK_SIZE;
	for (int i = 0; i < (KERNEL_STACK_SIZE) / 0x1000; i++) {
		uint64_t addr = (uint64_t)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		memset((void*)addr, 0, PAGE_SIZE);
		AuMapPage(V2P(addr), (location + (uint64_t)i * 4096), PTE_AP_RW | PTE_NORMAL_MEM);
	}
	ke_stack_idx += 2;
	return (location + KERNEL_STACK_SIZE);
}

/**
 * @brief AuCreateSubKernelStack -- maps sub kernel stack and return the top
 * of the stack, it only maps 4KiB of stack
 * @param pml -- Pointer to page directory
 * @return kernel stack address
 */
uint64_t AuCreateSubKernelStack(AuProcess* proc, uint64_t* pml) {
	uint64_t location = KERNEL_STACK_LOCATION;
	location += proc->_kstack_index_ * KERNEL_STACK_SIZE;
	for (int i = 0; i < (KERNEL_STACK_SIZE) / 0x1000; i++) {
		uint64_t addr = (uint64_t)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		memset((void*)addr, 0, PAGE_SIZE);
		AuMapPage(V2P(addr), (location + i * 4096), PTE_AP_RW | PTE_NORMAL_MEM);
	}

	proc->_kstack_index_++;

	return (location + KERNEL_STACK_SIZE);
}
/**
 *	@brief AuSchedulerInitialize -- initialize the scheduler
 */
void AuSchedulerInitialize() {
	s_sched_lock = AuCreateSpinlock(true);
	thread_list_head = NULL;
	thread_list_last = NULL;
	thread_id = 0;
	ke_stack_idx = 0;
	uint64_t* idle_pd = AuCreateVirtualAddressSpace();
	AA64Thread* idle_ = AuCreateKthread(AuIdleThread, idle_pd, "Idle");
	//idle_->elr_el1 = (uint64_t)AuIdleThread;
	_idle_thr = idle_;
	current_thread = idle_;
	_scheduler_initialized = false;
	scheduler_tick = 0;
}

/**
 * @brief AuSchedulerStart -- start the scheduler
 */
void AuSchedulerStart() {
	mask_irqs();
	AA64Thread* idle = current_thread;
	_scheduler_initialized = true;
#ifndef __TARGET_BOARD_RPI3__
	GICClearPendingIRQ(27);
#endif
	tlb_flush_vmalle1is();
	write_both_ttbr(V2P(idle->pml));
	aa64_restore_fp(idle->fp_regs, (uint64_t*)&idle->fpcr, (uint64_t*)&idle->fpsr);
	suspendTimer();
	setupTimerIRQ();
	first_time_sex(idle);
}

AA64Thread* AuGetIdleThread() {
	return _idle_thr;
}

AA64Thread* AuGetCurrentThread() {
	return current_thread;
}

/**
 * @brief AuForceScheduler -- force the scheduler
 * to switch next thread
 */
void AuForceScheduler() {
	AuScheduleNext();
}

/**
 * @brief AuBlockThread -- blocks a running thread 
 * @param thread -- Pointer to AA64 Thread
 */
void AuBlockThread(AA64Thread* thread) {
	uint64_t d = AuSchedLock();
	thread->state = THREAD_STATE_BLOCKED;
	schedUnlink(&thread_list_head, &thread_list_last, thread);
	schedLink(&blocked_thr_head, &blocked_thr_last, thread);
	AuSchedUnlock(d);
}

/**
* @brief AuUnblockThread -- unblocks a thread and insert it to
* ready list
* @param t -- pointer to thread
*/
void AuUnblockThread(AA64Thread* thread) {
	uint64_t d = AuSchedLock();
	if (thread->state != THREAD_STATE_LEFT_IN_KERNEL)
		thread->state = THREAD_STATE_READY;
	bool found_ = false;
	AA64Thread* thr = NULL;
	for (thr = blocked_thr_head; thr != NULL; thr = thr->next) {
		if (thr == thread) {
			schedUnlink(&blocked_thr_head, &blocked_thr_last, thr);
			found_ = 1;
			break;
		}
	}
	if (found_) {
		schedLink(&thread_list_head, &thread_list_last, thread);
	}
	AuSchedUnlock(d);
}

/**
 * @brief AuThreadWakePoller -- wake up a thread that was left
 * in sleep/block while polling
 * @param t -- Pointer to thread
 */
void AuThreadWakePoller(AA64Thread* t) {
	if (!t || !t->blocked_on_poll)
		return;
	uint64_t d = AuSchedLock();
	bool moved = false;
	for (AA64Thread* x = blocked_thr_head; x; x = x->next)
		if (x == t) {
			schedUnlink(&blocked_thr_head, &blocked_thr_last, t);
			moved = true;
			break;
		}
	if (!moved) {
		for (AA64Thread* x = sleep_thr_head; x; x = x->next) {
			if (x == t) {
				t->sleepQuanta = 0;
				schedUnlink(&sleep_thr_head, &sleep_thr_last, t);
				moved = true;
				break;
			}
		}
	}
	if (moved) {
		if (t->state != THREAD_STATE_LEFT_IN_KERNEL)
			t->state = THREAD_STATE_READY;
		schedLink(&thread_list_head, &thread_list_last, t);
	}
	AuSchedUnlock(d);
}

/**
 * @brief AuSleepThread -- block a running thread
 * and put it into sleep list
 * @param thread -- Pointer to AA64 Thread
 */
void AuSleepThread(AA64Thread* thread, uint64_t ms) {
	uint64_t d = AuSchedLock();
	thread->state = THREAD_STATE_SLEEP;
	thread->sleepQuanta = ms;
	thread->sleep_until_us = AuGetCurrentUS() + ms * 1000ULL;
	schedUnlink(&thread_list_head, &thread_list_last, thread);
	schedLink(&sleep_thr_head, &sleep_thr_last, thread);
	AuSchedUnlock(d);
}

/**
 * @brief AuThreadMakeReady -- make a thread forcefully
 * ready for next
 * @param thread -- pointer to thread struct
 */
void AuThreadMakeReady(AA64Thread* thread) {
	if (thread->state == THREAD_STATE_SLEEP) {
		uint64_t d = AuSchedLock();
		thread->sleepQuanta = 0;
		schedUnlink(&sleep_thr_head, &sleep_thr_last, thread);
		schedLink(&thread_list_head, &thread_list_last, thread);
		thread->state = THREAD_STATE_READY;
		AuSchedUnlock(d);
	} else if (thread->state == THREAD_STATE_BLOCKED)
		AuUnblockThread(thread);
}
/**
 * @brief AuThreadFindByID -- finds a thread by its id from
 * ready queue
 * @param id -- id of the thread
 */
AA64Thread* AuThreadFindByID(uint64_t id) {
	uint64_t d = AuSchedLock();
	AA64Thread* ready_queue_ = NULL;
	AA64Thread* found = NULL;
	for (ready_queue_ = thread_list_head; ready_queue_ != NULL; ready_queue_ = ready_queue_->next) {
		if (ready_queue_->thread_id == id) {
			found = ready_queue_;
			break;
		}
	}
	AuSchedUnlock(d);
	return found;
}

/**
 * @brief AuThreadFindByIDBlockList -- finds a thread by its id from
 * the block queue
 * @param id -- id of the thread
 */
AA64Thread* AuThreadFindByIDBlockList(uint64_t id) {
	uint64_t d = AuSchedLock();
	AA64Thread* block_queue = NULL;
	AA64Thread* found = NULL;
	for (block_queue = blocked_thr_head; block_queue != NULL; block_queue = block_queue->next) {
		if (block_queue->thread_id == id) {
			found = block_queue;
			break;
		}
	}
	AuSchedUnlock(d);
	return found;
}

/**
 * @brief AuThreadFindByIDSleepList -- finds a thread by its id from
 * the sleep queue
 * @param id -- thread id
 */
AA64Thread* AuThreadFindByIDSleepList(uint64_t id) {
	uint64_t d = AuSchedLock();
	AA64Thread* sleep_queue = NULL;
	AA64Thread* found = NULL;
	for (sleep_queue = sleep_thr_head; sleep_queue != NULL; sleep_queue = sleep_queue->next) {
		if (sleep_queue->thread_id == id) {
			found = sleep_queue;
			break;
		}
	}
	AuSchedUnlock(d);
	return found;
}
/**
 * @brief AuThreadMoveToTrash -- move given thread to
 * trash
 * @param t -- Thread to move to trash
 */
void AuThreadMoveToTrash(AA64Thread* t) {
	if (!t)
		return;
	if (t->state == THREAD_STATE_KILLABLE)
		return;

	AuEDFRemove(t);
	t->state = THREAD_STATE_KILLABLE;

	uint64_t d = AuSchedLock();
	AA64Thread* ready_queue_ = NULL;
	/* search the thread in ready queue*/
	for (ready_queue_ = thread_list_head; ready_queue_ != NULL; ready_queue_ = ready_queue_->next) {
		if (ready_queue_ == t) {
			schedUnlink(&thread_list_head, &thread_list_last, t);
			break;
		}
	}

	AA64Thread* block_queue_ = NULL;
	/* search the thread in block queue*/
	for (block_queue_ = blocked_thr_head; block_queue_ != NULL; block_queue_ = block_queue_->next) {
		if (block_queue_ == t) {
			schedUnlink(&blocked_thr_head, &blocked_thr_last, t);
			break;
		}
	}

	AA64Thread* sleep_queue_ = NULL;
	for (sleep_queue_ = sleep_thr_head; sleep_queue_ != NULL; sleep_queue_ = sleep_queue_->next) {
		if (sleep_queue_ == t) {
			schedUnlink(&sleep_thr_head, &sleep_thr_last, t);
			break;
		}
	}

	/* insert it in the trash list */
	schedLink(&trash_thr_head, &trash_thr_last, t);
	AuSchedUnlock(d);
}

/**
 * @brief AuThreadCleanTrash -- clean a thread from
 * trash list
 */
void AuThreadCleanTrash(AA64Thread* t) {
	AuThreadDeleteTrash(t);
}

static bool schedValidateOne(
	AA64Thread* head, AA64Thread* last, uint8_t s0, uint8_t s1, uint8_t s2, int* count) {
	int steps = 0;
	AA64Thread* prev = NULL;
	for (AA64Thread* t = head; t != NULL; t = t->next) {
		if (++steps > SCHED_VALIDATE_STEPS)
			return false;
		if (t->prev != prev)
			return false;
		if (t->state != s0 && t->state != s1 && t->state != s2)
			return false;
		prev = t;
		(*count)++;
	}
	if (prev != last)
		return false;
	if (last && last->next != NULL)
		return false;
	return true;
}

/**
 * @brief AuSchedValidateLists -- consistency-check all scheduler lists
 * @return true when every list is well-formed (linkage, tail, states)
 */
bool AuSchedValidateLists(void) {
	uint64_t d = AuSchedLock();
	int count = 0;
	bool ok = schedValidateOne(thread_list_head,
							   thread_list_last,
							   THREAD_STATE_READY,
							   THREAD_STATE_RUNNING,
							   THREAD_STATE_LEFT_IN_KERNEL,
							   &count);
	ok = schedValidateOne(blocked_thr_head,
						  blocked_thr_last,
						  THREAD_STATE_BLOCKED,
						  THREAD_STATE_LEFT_IN_KERNEL,
						  THREAD_STATE_LEFT_IN_KERNEL,
						  &count) &&
		 ok;
	ok = schedValidateOne(sleep_thr_head,
						  sleep_thr_last,
						  THREAD_STATE_SLEEP,
						  THREAD_STATE_LEFT_IN_KERNEL,
						  THREAD_STATE_LEFT_IN_KERNEL,
						  &count) &&
		 ok;
	ok = schedValidateOne(trash_thr_head,
						  trash_thr_last,
						  THREAD_STATE_KILLABLE,
						  THREAD_STATE_KILLABLE,
						  THREAD_STATE_KILLABLE,
						  &count) &&
		 ok;
	AuSchedUnlock(d);
	return ok && count > 0;
}

/**
 * @brief AuGetSystemTimerTick -- return the current system
 * timer tick
 */
uint64_t AuGetSystemTimerTick() {
	return scheduler_tick;
}

/** 
*  @brief AuSetIdleThread -- change the idle thread pointer
 * @param thr -- Pointer to idle thread
 */
void AuSetIdleThread(AA64Thread* thr) {
	_idle_thr = thr;
}

/**
 * @brief AuThreadWaitForTermination -- wait for desired thread
 * termination
 * @param thread_id -- desired thread id
 */
int AuThreadWaitForTermination(int thread_id) {
	AA64Registers* regs = AA64GetCurrentRegCtx();
	AA64Thread* curr = AuGetCurrentThread();
	AA64Thread* desired = AuThreadFindByID(thread_id);
	if (!desired) {
		desired = AuThreadFindByIDBlockList(thread_id);
		if (!desired)
			desired = AuThreadFindByIDSleepList(thread_id);
	}
	if (!desired) {
		return 1;
	}

	if (!desired->waitlist)
		return 1;

	AuProcess* proc = AuProcessFindThread(curr);
	if (!proc) {
		UARTDebugOut(
			"[aurora]: current thread %s wait for termination failed, no process slot \r\n",
			curr->name);
		return 1;
	}
	/** we mark it's belonging process non killable because, untill the child thread
	 * is terminated, the process should not be terminated anyhow, or else dead thread-dead
	 * process
	 */
	proc->state |= PROCESS_STATE_BUSY_WAIT;
	list_add(desired->waitlist, curr);
	curr->wait_ref_count += 1;
	proc->waiting_threads += 1;
	AuBlockThread(curr);
	AuScheduleThread(regs);
	/** explicitly call pause thread on user space */
	return 0;
}

/**
 * @brief AuThreadAwakeWaiters -- unblock all awaiting
 * threads for the current thread
 * @praram thread -- current thread which waitlist
 * needs to be freed
 */
void AuThreadAwakeWaiters(AA64Thread* thread) {
	if (!thread)
		return;
	if (!thread->waitlist)
		return;

	UARTDebugOut("AuThreadAwakeWaiters ======= \r\n");
	/** remove all waiters */
	while (thread->waitlist->pointer > 0) {
		AA64Thread* unblockable = (AA64Thread*)list_remove(thread->waitlist, 0);
		if (!unblockable)
			continue;
		UARTDebugOut("unblocking thread : %s \r\n", unblockable->name);
		unblockable->wait_ref_count -= 1;
		AuProcess* proc = AuProcessFindThread(unblockable);
		if (!proc)
			return;
		/* what if process if not found, kernel will get lot's of garbage 
		 * thread vs process allocation be carefull, it should get
		 * on thread's procSlot -- security concern */
		if (proc)
			AuProcessReapWaitcount(proc, 1);

		AuUnblockThread(unblockable);
	}
}
