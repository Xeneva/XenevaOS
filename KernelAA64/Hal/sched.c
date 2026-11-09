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
#include <Hal/AA64/aa64percpu.h>
#include <Sync/bkl.h>
#include <Sync/ticketlock.h>
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
#include <Sync/futex.h>
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

/* The running thread lives in this core's per cpu block (tpidr_el1);
 * every use of current_thread below reads or writes the calling core's
 * slot. AuGetCurrentThread is the NULL safe accessor for code that may
 * run before per cpu setup. */
#define current_thread (AA64GetPerCPU()->currentThread)

/* boot core's idle thread; secondary cores have their own in
 * AA64PerCPU::idleThread, see schedIdleThread */
AA64Thread* _idle_thr;
uint64_t thread_id;
bool _scheduler_initialized;
uint64_t scheduler_tick;
static uint64_t _global_idle_time;

/* idle thread of the calling core */
static AA64Thread* schedIdleThread(void) {
	AA64PerCPU* p = AA64GetPerCPU();
	if (p && p->idleThread)
		return p->idleThread;
	return _idle_thr;
}

static bool schedIsIdle(AA64Thread* t) {
	return t == _idle_thr || t == schedIdleThread();
}

/* charge the time since this core's last switch to busy or idle, and
 * note who runs next. Owning core only, IRQs masked */
static void schedAccount(AA64PerCPU* p, AA64Thread* outgoing, AA64Thread* incoming) {
	uint64_t now = get_cntpct_el0();
	uint64_t last = p->acct_last;
	p->acct_last = now;
	if (last && now > last) {
		if (schedIsIdle(outgoing))
			p->idle_cnt += now - last;
		else
			p->busy_cnt += now - last;
	}
	memcpy(p->cur_name, incoming->name, sizeof(p->cur_name));
}

/* does resuming t continue kernel code (as opposed to eret to EL0)?
 * first runs, AuScheduleNext parks and EL1 exception frames all do */
static bool schedResumesInKernel(AA64Thread* t) {
	if (t->state == THREAD_STATE_LEFT_IN_KERNEL || !t->justStored)
		return true;
	if (t->elr_el1 >= 0xFFFF000000000000ULL)
		return true;
	return (t->spsr_el1 & 0xC) != 0; /* SPSR.M[3:2] = EL */
}

/**
 * @brief AuSchedCanRunHere -- may the calling core resume this thread now.
 * Call with the scheduler lock held.
 *
 * - on_cpu: another core still executes on its context (the outgoing
 *   thread of this very core is fine, it may simply continue)
 * - kernel contexts resume on the boot core only. All device interrupts
 *   are delivered there, so a kernel thread that holds the BKL with IRQs
 *   enabled always gets its interrupts nested on its own core instead of
 *   deadlocking against an interrupt handler spinning on another core.
 *   Secondary cores run user mode, and syscalls (IRQs masked) from it.
 */
/* may cpu resume t's context at all (ignoring on_cpu)? Kernel contexts of
 * user threads (a parked syscall, a first run) run IRQs masked, or hand
 * any interrupt they wait for over through the BKL deferral, so they go
 * anywhere. Pure kernel threads stay on the boot core for now: drivers
 * there still assume the core that set up their interrupt */
static bool schedCanRunOn(AA64Thread* t, uint32_t cpu) {
	/* a bound thread runs on its core only, whatever it is (an explicit
	 * binding opts kernel threads in too: interrupt work they wait for is
	 * deferred to the BKL owner core) */
	if (t->cpu_bind)
		return t->cpu_bind == cpu + 1;
	/* isolated cores take bound threads only */
	if (cpu < AA64_SMP_MAX_CPUS && _aa64_percpu[cpu].isolated)
		return false;
	if (cpu == 0 || !schedResumesInKernel(t))
		return true;
	return (t->threadType & THREAD_LEVEL_USER) != 0;
}

bool AuSchedCanRunHere(AA64Thread* t) {
	if (__atomic_load_n(&t->on_cpu, __ATOMIC_ACQUIRE) && t != current_thread)
		return false;
	return schedCanRunOn(t, AA64GetCPUNum());
}

/* t just became runnable: wake one idle core that may run it, instead
 * of letting it wait up to a tick. Called with the scheduler lock held */
static void schedKick(AA64Thread* t) {
	if (!_scheduler_initialized)
		return;
	if (t->cpu_bind) {
		/* only its own core can run it, and that core may be tickless:
		 * always poke it, busy or idle */
		uint32_t cpu = t->cpu_bind - 1;
		AA64PerCPU* p = &_aa64_percpu[cpu];
		if (cpu < AA64_SMP_MAX_CPUS && p->magic == AA64_PERCPU_MAGIC &&
			p->state == AA64_CPU_STATE_ONLINE && (cpu == 0 || p->sched_release) &&
			!__atomic_exchange_n(&p->ipi_pending, 1, __ATOMIC_ACQ_REL))
			GICSendSGI(cpu, AA64_IPI_RESCHED);
		return;
	}
	uint32_t self = AA64GetCPUNum();
	for (uint32_t k = 1; k <= AA64_SMP_MAX_CPUS; k++) {
		uint32_t cpu = (self + k) % AA64_SMP_MAX_CPUS;
		AA64PerCPU* p = &_aa64_percpu[cpu];
		if (p->magic != AA64_PERCPU_MAGIC || p->state != AA64_CPU_STATE_ONLINE || !p->idleThread)
			continue;
		if (cpu != 0 && !p->sched_release)
			continue;
		if (p->currentThread != p->idleThread || !schedCanRunOn(t, cpu))
			continue;
		if (__atomic_exchange_n(&p->ipi_pending, 1, __ATOMIC_ACQ_REL))
			continue; /* already on its way to reschedule */
		GICSendSGI(cpu, AA64_IPI_RESCHED);
		return;
	}
}

/* Serializes all scheduler list and thread state changes across cores.
 * Callers hold it with IRQs masked (AuSchedLock does both); DAIF is
 * save/restored so masked callers (fault path, scheduler tail) are not
 * wrongly unmasked.
 *
 * Recursive on the owning core: the tick path holds it across sleep
 * handling, EDF releases and the pick, and those helpers lock again on
 * their own. Other cores spin until the outermost unlock. Masking IRQs
 * first means the owner can never be interrupted into a second,
 * unrelated critical section on the same core. */
extern void aa64_spinlock_acquire(uint32_t* lock);
extern void aa64_spinlock_release(uint32_t* lock);
/* FIFO, every core's tick contends for it */
static AuTicketLock s_sched_lock_word __attribute__((aligned(8)));
static volatile uint32_t s_sched_owner; /* owning cpu_num + 1, 0 when free */
static uint32_t s_sched_depth;
static bool s_sched_lock_ready;

uint64_t AuSchedLock(void) {
	uint64_t d = 0;
	if (!s_sched_lock_ready)
		return d;
	d = read_daif();
	mask_irqs();
	/* only this core ever writes its own id into s_sched_owner, and it
	 * clears it before releasing, so a match means we already hold it */
	uint32_t me = AA64GetCPUNum() + 1;
	if (s_sched_owner == me) {
		s_sched_depth++;
		return d;
	}
	AuTicketLockAcquire(&s_sched_lock_word);
	s_sched_owner = me;
	s_sched_depth = 1;
	return d;
}

void AuSchedUnlock(uint64_t d) {
	if (!s_sched_lock_ready)
		return;
	if (s_sched_depth == 0 || s_sched_owner != AA64GetCPUNum() + 1) {
		/* unbalanced unlock, do not release someone else's lock */
		restore_daif(d);
		return;
	}
	if (--s_sched_depth == 0) {
		s_sched_owner = 0;
		AuTicketLockRelease(&s_sched_lock_word);
	}
	restore_daif(d);
}

/* Unlocked cores: caller must hold the sched lock (or run masked in
 * the tick path, which is atomic against masked thread-side holders). */
/* Every thread records which list it is linked on. schedUnlink only
 * patches the head/tail of the list it is handed: unlinking a thread from
 * a list it is not on leaves that list's head pointing at it and cuts off
 * the rest of the list it really is on (a whole run queue can vanish).
 * Mismatches are reported and redirected to the right list. */
#define SCHED_LIST_NONE	   0
#define SCHED_LIST_READY   1
#define SCHED_LIST_BLOCKED 2
#define SCHED_LIST_SLEEP   3
#define SCHED_LIST_TRASH   4

static uint32_t schedListId(AA64Thread** head) {
	if (head == &thread_list_head)
		return SCHED_LIST_READY;
	if (head == &blocked_thr_head)
		return SCHED_LIST_BLOCKED;
	if (head == &sleep_thr_head)
		return SCHED_LIST_SLEEP;
	if (head == &trash_thr_head)
		return SCHED_LIST_TRASH;
	return SCHED_LIST_NONE;
}

static void schedListPtrs(uint32_t id, AA64Thread*** head, AA64Thread*** last) {
	switch (id) {
	case SCHED_LIST_READY:
		*head = &thread_list_head;
		*last = &thread_list_last;
		break;
	case SCHED_LIST_BLOCKED:
		*head = &blocked_thr_head;
		*last = &blocked_thr_last;
		break;
	case SCHED_LIST_SLEEP:
		*head = &sleep_thr_head;
		*last = &sleep_thr_last;
		break;
	case SCHED_LIST_TRASH:
		*head = &trash_thr_head;
		*last = &trash_thr_last;
		break;
	default:
		*head = NULL;
		*last = NULL;
		break;
	}
}

static uint32_t s_sched_list_reports;
static void schedListReport(const char* what, AA64Thread* t, uint32_t want, void* caller) {
	if (s_sched_list_reports >= 32)
		return;
	s_sched_list_reports++;
	UARTDebugOut("[sched]: %s %s state %d: list %d, but it is on %d (cpu %d, caller %x)\r\n",
				 what, t->name, (int)t->state, want, t->sched_list, AA64GetCPUNum(), caller);
}

static void schedUnlinkRaw(AA64Thread** head, AA64Thread** last, AA64Thread* thread);

static void schedLink(AA64Thread** head, AA64Thread** last, AA64Thread* t) {
	uint32_t id = schedListId(head);
	if (t->sched_list != SCHED_LIST_NONE) {
		/* already linked: a second link would corrupt both lists */
		schedListReport("link", t, id, __builtin_return_address(0));
		AA64Thread **h, **l;
		schedListPtrs(t->sched_list, &h, &l);
		if (h)
			schedUnlinkRaw(h, l, t);
	}
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
	t->sched_list = id;
}

static void schedUnlink(AA64Thread** head, AA64Thread** last, AA64Thread* thread) {
	if (!thread || !head || !last)
		return;
	uint32_t id = schedListId(head);
	if (thread->sched_list != id) {
		if (thread->sched_list == SCHED_LIST_NONE)
			return; /* on no list: nothing to take it off */
		schedListReport("unlink", thread, id, __builtin_return_address(0));
		schedListPtrs(thread->sched_list, &head, &last);
	}
	schedUnlinkRaw(head, last, thread);
}

static void schedUnlinkRaw(AA64Thread** head, AA64Thread** last, AA64Thread* thread) {
	thread->sched_list = SCHED_LIST_NONE;
	if (*head == NULL)
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

/* link onto the ready list and wake an idle core for it */
static void schedLinkReady(AA64Thread* t) {
	schedLink(&thread_list_head, &thread_list_last, t);
	schedKick(t);
}

void AuThreadInsert(AA64Thread* new_task) {
	uint64_t d = AuSchedLock();
	schedLinkReady(new_task);
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

/* an isolated core needs its timer only while two or more bound threads
 * have to share it, or while one is runnable but could not be picked yet
 * (still leaving the core it ran on): then retry on the next tick.
 * Scheduler lock held */
static bool schedCoreWantsTick(AA64PerCPU* p, uint32_t cpu, AA64Thread* next) {
	if (!p->isolated)
		return true;
	int n = 0;
	for (AA64Thread* t = thread_list_head; t && n < 2; t = t->next) {
		if (t->sched_list == SCHED_LIST_READY && t->cpu_bind == cpu + 1 &&
			(t->state == THREAD_STATE_READY || t->state == THREAD_STATE_RUNNING ||
			 t->state == THREAD_STATE_LEFT_IN_KERNEL))
			n++;
	}
	return n > 1 || (n == 1 && schedIsIdle(next));
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
	/* Continue the round-robin after the current thread only while it is
	 * still on the ready list. A thread that just went to sleep or blocked
	 * sits on another list and its next pointer walks that list: the old
	 * code then picked a parked LEFT_IN_KERNEL sleeper straight off the
	 * sleep list, resumed it early while still linked there, and its next
	 * AuSleepThread unlinked it from the ready list it was not on, cutting
	 * the run queue (every thread behind it stopped being scheduled). */
	AA64Thread* cur = current_thread;
	AA64Thread* start = (cur && cur->sched_list == SCHED_LIST_READY && cur->next) ? cur->next
																			   : thread_list_head;

	AA64Thread* cursor = start; //current_thread ? current_thread->next : NULL;
	for (int i = 0; i < SCHED_VALIDATE_STEPS && cursor; i++) {
		if (cursor != _idle_thr && cursor->sched_list == SCHED_LIST_READY &&
			(cursor->state == THREAD_STATE_READY || cursor->state == THREAD_STATE_LEFT_IN_KERNEL) &&
			AuSchedCanRunHere(cursor)) {
			current_thread = cursor;
			return;
		}

		cursor = cursor->next ? cursor->next : thread_list_head;
		if (cursor == start)
			break;
	}
	current_thread = schedIdleThread();
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
	t->thread_id = __atomic_fetch_add(&thread_id, 1, __ATOMIC_RELAXED);
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
	t->thread_id = __atomic_fetch_add(&thread_id, 1, __ATOMIC_RELAXED);
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
	/* the console and the lists below are kernel state */
	AuBKLLock();
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
	AuBKLUnlock();
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
		/* aa64_schedule_init re-enters this loop on every core's idle */
		if (AA64GetCPUNum() == 0)
			AuSchedHeartbeat();
		_wfi();
	}
}

void AuIdleThread(uint64_t ctx) {
	mask_irqs();
	AuBKLLock();
	UARTDebugOut("Idle thread running \r\n");
	AuTextOut("Starting up Xeneva please wait...\r\n");
	AuBKLUnlock();
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
			/* A timed futex waiter that reaches its deadline must resume
			 * with its ABI's ETIMEDOUT.  The value was selected when the
			 * thread blocked (futex_result); claim futex_waiting atomically
			 * so a wake racing this tick either wins (and sets 0) or loses
			 * (and leaves the timeout result alone). */
			if (__atomic_exchange_n(&sleep_thr->futex_waiting, 0,
									__ATOMIC_ACQ_REL)) {
				if (sleep_thr->futex_result == 0)
					sleep_thr->futex_result = -AU_FUTEX_ETIMEDOUT;
				if (sleep_thr->futex_frame_valid && sleep_thr->sp) {
					AA64Registers* frame = (AA64Registers*)sleep_thr->sp;
					frame->x0 = (uint64_t)(int64_t)sleep_thr->futex_result;
					frame->x6 = (uint64_t)(int64_t)sleep_thr->futex_result;
					sleep_thr->futex_frame_valid = 0;
				}
			}
			if (sleep_thr->state != THREAD_STATE_LEFT_IN_KERNEL)
				sleep_thr->state = THREAD_STATE_READY;
			schedUnlink(&sleep_thr_head, &sleep_thr_last, sleep_thr);
			schedLinkReady(sleep_thr);
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

	/* Publish the outgoing frame, futex result, state changes and the pick
	 * in one critical section. Futex wake and timeout paths take this lock
	 * before patching the saved frame or making the thread runnable. */
	uint64_t sched_daif = AuSchedLock();

	/* the vector wrapper already captured the full interrupted register set,
	 * so i treat this frame as the only valid resume point for a preempted
	 * thread, not a C call frame and not originalKSp --axiss */
	runThr->sp = (uint64_t)regs;
	/* A futex waiter is resumed by aa64_resume_exception_frame straight to
	 * EL0, so the only chance to publish its syscall result is the saved
	 * SVC frame.  futex_active covers a wake that raced the store; later
	 * wakes patch the frame directly through futex_frame_valid. */
	if (runThr->futex_active) {
		regs->x0 = (uint64_t)(int64_t)runThr->futex_result;
		regs->x6 = (uint64_t)(int64_t)runThr->futex_result;
		runThr->futex_frame_valid = 1;
		runThr->futex_active = 0;
	}
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
		/* threads of one process may switch out on several cores at once */
		__atomic_fetch_add(&proc->total_runtime_us, delta, __ATOMIC_RELAXED);
		__atomic_fetch_add(&proc->window_runtime_us, delta, __ATOMIC_RELAXED);
	} else {
		__atomic_fetch_add(&_global_idle_time, delta, __ATOMIC_RELAXED);
	}

	/** change thread's running state to ready state */
	if (runThr->state == THREAD_STATE_RUNNING)
		runThr->state = THREAD_STATE_READY;

	/* system tick bookkeeping is the boot core's job, once per tick (not
	 * on IPI or syscall driven reschedules) */
	AA64PerCPU* this_cpu = AA64GetPerCPU();
	bool from_tick = this_cpu->sched_tick;
	this_cpu->sched_tick = 0;
	if (AA64GetCPUNum() == 0 && from_tick) {
		scheduler_tick++;
		AuHandleSleepThreads();
		AuEDFHandleReleases(now);
	}
	AA64NextThread();
	AA64Thread* next = current_thread;

	/* claim next for this core. runThr stays claimed until the switch
	 * path has moved off its stack (sched_finish_switch) */
	__atomic_store_n(&next->on_cpu, 1, __ATOMIC_RELAXED);
	next->start_time_us = now;
	bool left_in_kernel = (next->state == THREAD_STATE_LEFT_IN_KERNEL);
	bool resume_kernel = schedResumesInKernel(next);

	/* mark this thread as running */
	if (next->state == THREAD_STATE_READY)
		next->state = THREAD_STATE_RUNNING;
	else if (left_in_kernel)
		next->state = THREAD_STATE_READY;

	/* reserved (isolated) cores drop their tick while one bound thread
	 * owns them; foreign_runs proves nothing else ever lands there */
	uint32_t this_num = AA64GetCPUNum();
	bool want_tick = schedCoreWantsTick(this_cpu, this_num, next);
	/* runThr stays runnable but may not run here any more (it was just
	 * bound elsewhere, or this core got isolated): wake a core that may */
	if (next != runThr && runThr->sched_list == SCHED_LIST_READY &&
		(runThr->state == THREAD_STATE_READY || runThr->state == THREAD_STATE_LEFT_IN_KERNEL) &&
		!schedIsIdle(runThr) && !schedCanRunOn(runThr, this_num))
		schedKick(runThr);
	if (this_cpu->isolated && !schedIsIdle(next) && next->cpu_bind != this_num + 1)
		this_cpu->foreign_runs++;

	AuSchedUnlock(sched_daif);

	/* per core statistics, read by debuggers, the heartbeat and
	 * SYS_GET_CPU_STATS */
	schedAccount(this_cpu, runThr, next);
	if (next != runThr)
		this_cpu->context_switches++;
	if (schedIsIdle(next))
		this_cpu->idle_tick++;

#ifndef __TARGET_BOARD_RPI3__
	/* the virtual timer is banked per core */
	if (!want_tick && !this_cpu->tick_stopped) {
		suspendTimer();
		this_cpu->tick_stopped = 1;
	} else if (want_tick && this_cpu->tick_stopped) {
		setupTimerIRQ();
		this_cpu->tick_stopped = 0;
	}
#else
	(void)want_tick;
#endif

	/* BKL hand over, outside the scheduler lock (order is BKL -> sched).
	 * runThr keeps the depth it held for its next resume; next gets back
	 * what it needs: its saved depth if it continues kernel code (kernel
	 * threads hold at least one level whenever they run), none for EL0 */
	runThr->bkl_depth = AuBKLReleaseAll();
	this_cpu->switchRelease = (next != runThr) ? &runThr->on_cpu : NULL;
	if (resume_kernel) {
		/* a first run enters kernel code (AuProcessEntUser, a kthread
		 * body) and needs a level. A pure kernel thread holds at least
		 * one whenever it runs. A user thread's parked syscall gets back
		 * exactly what it held: 0 for a BKL free fast path syscall */
		uint32_t depth = next->bkl_depth;
		if (depth == 0 && !schedIsIdle(next) &&
			(!next->justStored || !(next->threadType & THREAD_LEVEL_USER)))
			depth = 1;
		AuBKLAcquireDepth(depth);
	}

	/* ASID tagged switch, no TLB flush */
	AuVmmActivate(next->pml);

	//tlb_flush_vmalle1is();
	aa64_restore_fp(next->fp_regs, (uint64_t*)&next->fpcr, (uint64_t*)&next->fpsr);
	dsb_sy_barrier();

	/** check if the thread was left somewhere in kernel space **/
	if (left_in_kernel) {
		aa64_restore_sp(next);
		/* should not reach here */
		for (;;)
			;
	}

	AuSignalDeliver(next);
	aa64_set_tpidr_el0(next->linux_tls);

	if (!next->justStored) {
		/* first_time_sex (lol) installs the initial kernel entry, stack and
		 * PSTATE and never returns. user threads go through
		 * AuProcessEntUser instead, which builds their EL0 stack before
		 * its own eret --axiss */
		first_time_sex(next); //unskippable function name holy shit --axiss
		__builtin_unreachable();
	}

	AA64Registers* return_frame = (AA64Registers*)next->sp;
	next->data = return_frame;
	aa64_resume_exception_frame(return_frame, next->elr_el1, next->spsr_el1);
	__builtin_unreachable();
}

int AuSchedYieldMs(uint64_t ms) {
	AA64Thread* thr;

	if (_scheduler_initialized == 0)
		return -1;
	thr = current_thread;
	if (!thr || thr == schedIdleThread())
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

	/* masked until the resume (aa64_restore_sp returns with IRQs still
	 * masked, as before): a tick between here and aa64_schedule_init
	 * would save this context as if it were the idle thread's */
	mask_irqs();
	AA64Thread* storeThr = current_thread;
	AA64Thread* idle = schedIdleThread();

	uint64_t d = AuSchedLock();
	/* an exiting thread stays KILLABLE on the trash list. idle never
	 * parks: aa64_schedule_init re-enters its loop fresh */
	if (storeThr != idle && storeThr->state != THREAD_STATE_KILLABLE)
		storeThr->state = THREAD_STATE_LEFT_IN_KERNEL;
	current_thread = idle;
	__atomic_store_n(&idle->on_cpu, 1, __ATOMIC_RELAXED);
	AuSchedUnlock(d);

	aa64_store_fp(storeThr->fp_regs, &storeThr->fpcr, &storeThr->fpsr);
	__asm__ volatile("msr cntv_tval_el0, %0\n isb" ::"r"(1ULL));

	/* idle holds no BKL. storeThr gets its depth back on the boot core,
	 * the only core that resumes parked kernel contexts */
	schedAccount(AA64GetPerCPU(), storeThr, idle);
	storeThr->bkl_depth = AuBKLReleaseAll();
	/* idle's address space, ASID tagged; aa64_schedule_init no longer
	 * touches TTBR0 */
	AuVmmActivate(idle->pml);
	AA64GetPerCPU()->switchRelease = (storeThr != idle) ? &storeThr->on_cpu : NULL;
	aa64_schedule_init(storeThr, idle, 0);
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
	int idx = __atomic_fetch_add(&ke_stack_idx, 2, __ATOMIC_RELAXED);
	location += (uint64_t)idx * KERNEL_STACK_SIZE;
	for (int i = 0; i < (KERNEL_STACK_SIZE) / 0x1000; i++) {
		uint64_t addr = (uint64_t)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		memset((void*)addr, 0, PAGE_SIZE);
		AuMapPage(V2P(addr), (location + (uint64_t)i * 4096), PTE_AP_RW | PTE_NORMAL_MEM);
	}
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
	s_sched_lock_ready = true;
	thread_list_head = NULL;
	thread_list_last = NULL;
	thread_id = 0;
	ke_stack_idx = 0;
	uint64_t* idle_pd = AuCreateVirtualAddressSpace();
	AA64Thread* idle_ = AuCreateKthread(AuIdleThread, idle_pd, "Idle");
	//idle_->elr_el1 = (uint64_t)AuIdleThread;
	_idle_thr = idle_;
	AA64GetPerCPU()->idleThread = idle_;
	current_thread = idle_;
	_scheduler_initialized = false;
	scheduler_tick = 0;
}

/**
 * @brief AuSchedulerStart -- start the scheduler
 */
void AuSchedulerStart() {
	/* secondary cores get their idle threads first, kmalloc and the
	 * kernel stack allocator only run on the boot core */
	AA64SMPStartSchedulers();
	mask_irqs();
	AA64Thread* idle = current_thread;
	__atomic_store_n(&idle->on_cpu, 1, __ATOMIC_RELAXED);
	AA64SMPIPILocalInit();
	AA64GetPerCPU()->acct_last = get_cntpct_el0();
	_scheduler_initialized = true;
#ifndef __TARGET_BOARD_RPI3__
	GICClearPendingIRQ(27);
#endif
	tlb_flush_vmalle1is();
	AuVmmActivate(idle->pml);
	aa64_restore_fp(idle->fp_regs, (uint64_t*)&idle->fpcr, (uint64_t*)&idle->fpsr);
	suspendTimer();
	setupTimerIRQ();
	first_time_sex(idle);
}

/**
 * @brief AuGetIdleThread -- idle thread of the calling core
 */
AA64Thread* AuGetIdleThread() {
	return schedIdleThread();
}

/**
 * @brief AuGetCurrentThread -- thread running on the calling core,
 * NULL before per cpu data or the scheduler is set up
 */
AA64Thread* AuGetCurrentThread() {
	AA64PerCPU* p = AA64GetPerCPU();
	return p ? p->currentThread : NULL;
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
		schedLinkReady(thread);
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
		schedLinkReady(t);
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
	/* state check and move are one step, another core may be changing
	 * the same thread's state */
	uint64_t d = AuSchedLock();
	if (thread->state == THREAD_STATE_SLEEP) {
		thread->sleepQuanta = 0;
		schedUnlink(&sleep_thr_head, &sleep_thr_last, thread);
		schedLinkReady(thread);
		thread->state = THREAD_STATE_READY;
	} else if (thread->state == THREAD_STATE_BLOCKED) {
		AuUnblockThread(thread);
	} else if (thread->state != THREAD_STATE_KILLABLE) {
		/* running (maybe on another core) on its way to a pause: do not
		 * lose this wakeup, see AuThreadPark */
		thread->wake_pending = 1;
	}
	AuSchedUnlock(d);
}

/**
 * @brief AuThreadPark -- block the calling thread for PauseThread, unless
 * a wakeup already arrived for it.
 *
 * User space waits in two syscalls: look for an event (postbox get), then
 * pause if there was none. A waker that runs in between, on another core,
 * finds the thread still running and used to drop the wakeup, leaving it
 * blocked with its event already queued. Wakers now leave a token instead
 * (AuThreadUnpark, AuThreadMakeReady) and the pause consumes it.
 * @param thread -- the calling thread
 * @return true if it blocked (the caller reschedules), false if a wakeup
 * was pending and it should return at once
 */
bool AuThreadPark(AA64Thread* thread) {
	uint64_t d = AuSchedLock();
	if (thread->wake_pending) {
		thread->wake_pending = 0;
		AuSchedUnlock(d);
		return false;
	}
	thread->state = THREAD_STATE_BLOCKED;
	schedUnlink(&thread_list_head, &thread_list_last, thread);
	schedLink(&blocked_thr_head, &blocked_thr_last, thread);
	AuSchedUnlock(d);
	return true;
}

/**
 * @brief AuThreadUnpark -- wake a paused thread, or leave it a wakeup
 * token if it is not blocked right now (see AuThreadPark)
 * @param thread -- thread to wake
 */
void AuThreadUnpark(AA64Thread* thread) {
	uint64_t d = AuSchedLock();
	if (thread->state == THREAD_STATE_BLOCKED)
		AuUnblockThread(thread);
	else if (thread->state != THREAD_STATE_KILLABLE)
		thread->wake_pending = 1;
	AuSchedUnlock(d);
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
	_aa64_percpu[0].idleThread = thr;
}

/* body of a secondary core's idle thread. The boot core's AuIdleThread
 * also prints the boot banner and the scheduler heartbeat, keep the
 * console off the other cores */
static void AuAPIdleThread(uint64_t ctx) {
	(void)ctx;
	while (1) {
		enable_irqs();
		_wfi();
	}
}

/**
 * @brief AuSchedCreateAPIdleThread -- create the idle thread of a
 * secondary core. Runs on the boot core: kmalloc and the kernel stack
 * allocator are not used from other cores yet. The thread is taken off
 * the shared ready list again, only its own core may ever run it.
 * @param cpu_num -- logical cpu number of the secondary core
 */
AA64Thread* AuSchedCreateAPIdleThread(uint32_t cpu_num) {
	char name[8] = {'I', 'd', 'l', 'e', (char)('0' + (cpu_num % 10)), 0, 0, 0};
	/* own address space like the boot idle: AuCreateKthread maps the
	 * signal trampoline into it at a fixed address */
	uint64_t* pd = AuCreateVirtualAddressSpace();
	if (!pd)
		return NULL;
	AA64Thread* t = AuCreateKthread(AuAPIdleThread, pd, name);
	AuThreadDelete(t);
	t->next = NULL;
	t->prev = NULL;
	return t;
}

/**
 * @brief AuSchedEnterAPIdle -- start the calling secondary core's timer
 * and switch onto its idle thread, never returns
 */
void AuSchedEnterAPIdle(void) {
	mask_irqs();
	AA64PerCPU* p = AA64GetPerCPU();
	AA64Thread* idle = p->idleThread;
	p->currentThread = idle;
	p->switchRelease = NULL;
	__atomic_store_n(&idle->on_cpu, 1, __ATOMIC_RELAXED);
	idle->state = THREAD_STATE_RUNNING;
	AuVmmActivate(idle->pml);
	aa64_restore_fp(idle->fp_regs, (uint64_t*)&idle->fpcr, (uint64_t*)&idle->fpsr);
#ifndef __TARGET_BOARD_RPI3__
	AA64SMPIPILocalInit();
	p->acct_last = get_cntpct_el0();
	/* the virtual timer and its PPI are banked per core */
	suspendTimer();
	GICEnablePPILocal(27);
	setupTimerIRQ();
#endif
	/* erets into AuAPIdleThread on the idle thread's own kernel stack,
	 * spsr 0x345 unmasks IRQs */
	first_time_sex(idle);
	__builtin_unreachable();
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

/**
 * @brief AuThreadWaitOffCPU -- wait until no core executes on the
 * thread's context any more. Call before freeing its kernel stack or its
 * address space. A thread killed while running on another core leaves
 * that core at its next tick.
 * @param t -- thread to wait for
 */
void AuThreadWaitOffCPU(AA64Thread* t) {
	if (!t || t == AuGetCurrentThread())
		return;
	if (!__atomic_load_n(&t->on_cpu, __ATOMIC_ACQUIRE))
		return;
	/* never spin on another core's progress holding the BKL: that core
	 * may need it to finish what it is doing and get off t's stack */
	uint32_t depth = AuBKLReleaseAll();
	while (__atomic_load_n(&t->on_cpu, __ATOMIC_ACQUIRE))
		__asm__ volatile("yield");
	AuBKLAcquireDepth(depth);
}

/* ---------------------------------------------------------------------
 * Core reservation (see sched.h)
 * --------------------------------------------------------------------- */
static volatile uint32_t s_isolated_mask;

static bool schedCpuUsable(uint32_t cpu) {
	if (cpu >= AA64_SMP_MAX_CPUS)
		return false;
	AA64PerCPU* p = &_aa64_percpu[cpu];
	return p->magic == AA64_PERCPU_MAGIC && p->state == AA64_CPU_STATE_ONLINE &&
		   (cpu == 0 || p->sched_release);
}

static void schedPokeCpu(uint32_t cpu) {
	AA64PerCPU* p = &_aa64_percpu[cpu];
	if (!__atomic_exchange_n(&p->ipi_pending, 1, __ATOMIC_ACQ_REL))
		GICSendSGI(cpu, AA64_IPI_RESCHED);
}

/**
 * @brief AuCpuSetIsolatedMask -- set the isolated cores (bit n = core n),
 * 0 returns every core to general use
 * @return 0, -1 if the mask names core 0, -2 if it names an offline core
 */
int AuCpuSetIsolatedMask(uint32_t mask) {
	if (mask & 1u)
		return -1; /* core 0 keeps device interrupts and the system tick */
	for (uint32_t c = 1; c < 32; c++)
		if ((mask & (1u << c)) && !schedCpuUsable(c))
			return -2;

	uint64_t d = AuSchedLock();
	uint32_t changed = s_isolated_mask ^ mask;
	s_isolated_mask = mask;
	for (uint32_t c = 1; c < AA64_SMP_MAX_CPUS; c++)
		_aa64_percpu[c].isolated = (mask >> c) & 1u;
	AuSchedUnlock(d);

	/* newly isolated cores push their general thread out, released ones
	 * restart their tick: both happen at their next reschedule */
	for (uint32_t c = 1; c < AA64_SMP_MAX_CPUS; c++)
		if ((changed & (1u << c)) && schedCpuUsable(c))
			schedPokeCpu(c);
	UARTDebugOut("[sched]: isolated cores mask %x \r\n", (uint64_t)mask);
	return 0;
}

/** @brief AuCpuGetIsolatedMask -- currently isolated cores */
uint32_t AuCpuGetIsolatedMask(void) {
	return s_isolated_mask;
}

/**
 * @brief AuThreadSetAffinity -- bind a thread to one core, -1 unbinds.
 * The thread moves there at its next reschedule
 * @return 0, -1 for a bad or offline core
 */
int AuThreadSetAffinity(AA64Thread* thread, int cpu) {
	if (!thread)
		return -1;
	if (cpu >= 0 && !schedCpuUsable((uint32_t)cpu))
		return -1;
	uint64_t d = AuSchedLock();
	thread->cpu_bind = (cpu < 0) ? 0 : (uint32_t)cpu + 1;
	if (thread->sched_list == SCHED_LIST_READY && thread->on_cpu == 0)
		schedKick(thread);
	AuSchedUnlock(d);
	return 0;
}
