/**
* @file smp_soak.c
*
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

/*
 * SMP soak test (build with SMPSOAK=1).
 *
 * Six CPU bound user mode threads in three address spaces (two threads
 * each) at the same user VAs. They mix integer and FP work and check
 * invariant registers (Hal/aa64smpsoak.s), so a broken context switch,
 * migration, FP save/restore or ASID/TLB alias shows up as an error count.
 * Every 16 iterations they make a syscall: sleep (BKL free, parks and
 * wakes through the IPI path), thread id and time (BKL free) or a process
 * count (BKL). A reporter kernel thread prints per worker progress and
 * per core busy time every few seconds and a PASS/FAIL verdict at the end.
 */

#ifdef __XENEVA_SMP_SOAK__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <_null.h>
#include <aurora.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64percpu.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64smp.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Drivers/uart.h>
#include <Serv/syscall.h>
#include <Sync/bkl.h>
#include <signal.h>

/* hard coded in Hal/aa64smpsoak.s */
_Static_assert(SYS_GET_THREAD_ID == 3, "smp soak syscall numbers");
_Static_assert(SYS_PROCESS_SLEEP == 23, "smp soak syscall numbers");
_Static_assert(SYS_GET_CURRENT_US == 70, "smp soak syscall numbers");
_Static_assert(SYS_GET_NUM_PROCESS_COUNT == 75, "smp soak syscall numbers");
_Static_assert(SYS_PAUSE_THREAD == 2, "smp soak syscall numbers");
_Static_assert(SYS_SEND_SIGNAL_TO_THREAD == 80, "smp soak syscall numbers");

#define SMPSOAK_SPACES	  3
#define SMPSOAK_PER_SPACE 2
#define SMPSOAK_WORKERS	  (SMPSOAK_SPACES * SMPSOAK_PER_SPACE)
#define SMPSOAK_CODE_VA	  0x40000000ULL
#define SMPSOAK_DATA_VA	  0x40001000ULL
#define SMPSOAK_STACK_VA  0x40010000ULL /* + worker * 0x2000, one page each */
#define SMPSOAK_REPORT_MS 5000
#define SMPSOAK_ROUNDS	  12

typedef struct {
	volatile uint64_t iters;
	volatile uint64_t errors;
	volatile uint64_t fast_calls;
	volatile uint64_t bkl_calls;
	volatile uint64_t id;
	uint64_t pad[3];
} SoakSlot;
_Static_assert(sizeof(SoakSlot) == 64, "slot layout is hard coded in aa64smpsoak.s");

typedef struct {
	AA64Thread* thr;
	SoakSlot* kslot; /* kernel alias of the slot */
	uint64_t slot_va;
	uint64_t stack_top;
	uint64_t entry_va;
} SoakWorker;

/* the pause/wake pair (aa64_smp_soak_waiter/_waker) */
typedef struct {
	volatile uint64_t flag;
	volatile uint64_t handoffs;
	volatile uint64_t wakes;
	volatile uint64_t pauses;
	volatile uint64_t waiter_tid;
	volatile uint64_t signum;
} SoakPair;

#define SMPSOAK_PAIR_WORKERS 2
static SoakWorker s_workers[SMPSOAK_WORKERS + SMPSOAK_PAIR_WORKERS];
static SoakPair* s_pair;

extern uint8_t aa64_smp_soak_user[];
extern uint8_t aa64_smp_soak_waiter[];
extern uint8_t aa64_smp_soak_waker[];
extern uint8_t aa64_smp_soak_user_end[];

static uint64_t soakMapPage(uint64_t* pd, uint64_t va, uint8_t attr) {
	uint64_t phys = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	if (phys == PMM_INVALID_PHYS)
		return 0;
	memset((void*)P2V(phys), 0, PAGE_SIZE);
	if (!AuMapPageEx(pd, phys, va, attr))
		return 0;
	return phys;
}

/* code written through the kernel alias must reach instruction fetch */
static void soakSyncCode(void* kva, size_t len) {
	for (uint64_t a = (uint64_t)kva & ~63ULL; a < (uint64_t)kva + len; a += 64)
		__asm__ volatile("dc cvau, %0" ::"r"(a) : "memory");
	__asm__ volatile("dsb ish\n ic ialluis\n dsb ish\n isb" ::: "memory");
}

/* kernel entry of a worker: drop into the user mode body */
static void AuSMPSoakWorkerEntry(uint64_t ctx) {
	(void)ctx;
	mask_irqs();
	AA64Thread* self = AuGetCurrentThread();
	SoakWorker* w = NULL;
	for (int i = 0; i < SMPSOAK_WORKERS + SMPSOAK_PAIR_WORKERS; i++)
		if (s_workers[i].thr == self)
			w = &s_workers[i];
	if (!w) {
		UARTDebugOut("[smpsoak]: worker has no slot \r\n");
		for (;;)
			_wfi();
	}
	/* leaving the kernel for EL0 */
	AuBKLReleaseAll();
	aa64_enter_user(w->stack_top, w->entry_va, w->slot_va);
	for (;;)
		;
}

typedef struct {
	uint64_t tick;
	uint64_t idle;
	uint64_t switches;
} SoakCpuSnap;

static void soakSnapCpus(SoakCpuSnap* s) {
	for (int c = 0; c < AA64_SMP_MAX_CPUS; c++) {
		AA64PerCPU* p = &_aa64_percpu[c];
		s[c].tick = p->tick_count;
		s[c].idle = p->idle_tick;
		s[c].switches = p->context_switches;
	}
}

static void AuSMPSoakReporter(uint64_t ctx) {
	(void)ctx;
	SoakCpuSnap prev[AA64_SMP_MAX_CPUS], now[AA64_SMP_MAX_CPUS];
	uint64_t prev_iters[SMPSOAK_WORKERS] = {0};
	uint64_t prev_handoffs = 0;
	bool ok = true;
	uint32_t min_busy_ap = 100;
	soakSnapCpus(prev);

	for (int round = 1; round <= SMPSOAK_ROUNDS; round++) {
		AuSchedYieldMs(SMPSOAK_REPORT_MS);
		soakSnapCpus(now);
		UARTDebugOut("[smpsoak]: round %d/%d \r\n", round, SMPSOAK_ROUNDS);
		for (int i = 0; i < SMPSOAK_WORKERS; i++) {
			SoakSlot* s = s_workers[i].kslot;
			uint64_t it = s->iters;
			UARTDebugOut("[smpsoak]:  worker %d iters %d (+%d) errors %d fast %d bkl %d \r\n",
						 i, it, it - prev_iters[i], s->errors, s->fast_calls, s->bkl_calls);
			if (s->errors)
				ok = false;
			if (it == prev_iters[i])
				ok = false; /* starved or stuck */
			prev_iters[i] = it;
		}
		if (s_pair) {
			uint64_t h = s_pair->handoffs;
			UARTDebugOut("[smpsoak]:  pause/wake pair handoffs %d (+%d) wakes %d pauses %d \r\n",
						 h, h - prev_handoffs, s_pair->wakes, s_pair->pauses);
			if (h == prev_handoffs)
				ok = false; /* a lost wakeup left the pair stuck */
			prev_handoffs = h;
		}
		for (int c = 0; c < AA64_SMP_MAX_CPUS; c++) {
			AA64PerCPU* p = &_aa64_percpu[c];
			if (p->magic != AA64_PERCPU_MAGIC || p->state != AA64_CPU_STATE_ONLINE)
				continue;
			uint64_t dt = now[c].tick - prev[c].tick;
			uint64_t di = now[c].idle - prev[c].idle;
			uint32_t busy = (dt && di < dt) ? (uint32_t)(100 - (di * 100) / dt) : 0;
			UARTDebugOut("[smpsoak]:  cpu %d busy %d pct ticks %d switches %d \r\n",
						 c, busy, dt, now[c].switches - prev[c].switches);
			if (c != 0 && round > 1 && busy < min_busy_ap)
				min_busy_ap = busy;
		}
		memcpy(prev, now, sizeof(prev));
	}

	if (ok && AA64SMPGetCPUCount() > 1 && min_busy_ap < 50)
		ok = false; /* the secondary cores did not carry the load */
	UARTDebugOut("[smpsoak]: %s (cpus %d, least busy secondary core %d pct) \r\n",
				 ok ? "PASS" : "FAIL", AA64SMPGetCPUCount(), min_busy_ap);
	for (;;)
		AuSchedYieldMs(60000);
}

/**
 * @brief AuSMPSoakStart -- set up the SMP soak test, before the scheduler
 * starts (the threads run once it does)
 */
void AuSMPSoakStart(void) {
	size_t code_len = (size_t)(aa64_smp_soak_user_end - aa64_smp_soak_user);
	for (int sp = 0; sp < SMPSOAK_SPACES; sp++) {
		uint64_t* pd = AuCreateVirtualAddressSpace();
		uint64_t code = soakMapPage(pd, SMPSOAK_CODE_VA, PTE_USER_EXECUTABLE | PTE_NORMAL_MEM | PTE_AP_RW_USER);
		uint64_t data = soakMapPage(pd, SMPSOAK_DATA_VA, PTE_NORMAL_MEM | PTE_AP_RW_USER);
		if (!pd || !code || !data) {
			UARTDebugOut("[smpsoak]: setup failed \r\n");
			return;
		}
		memcpy((void*)P2V(code), aa64_smp_soak_user, code_len);
		soakSyncCode((void*)P2V(code), code_len);

		for (int j = 0; j < SMPSOAK_PER_SPACE; j++) {
			int i = sp * SMPSOAK_PER_SPACE + j;
			uint64_t stack_va = SMPSOAK_STACK_VA + (uint64_t)i * 0x2000;
			if (!soakMapPage(pd, stack_va, PTE_NORMAL_MEM | PTE_AP_RW_USER)) {
				UARTDebugOut("[smpsoak]: stack setup failed \r\n");
				return;
			}
			SoakWorker* w = &s_workers[i];
			w->kslot = (SoakSlot*)(P2V(data) + (uint64_t)j * sizeof(SoakSlot));
			w->kslot->id = 0x1000 + (uint64_t)i * 0x10 + (uint64_t)sp;
			w->slot_va = SMPSOAK_DATA_VA + (uint64_t)j * sizeof(SoakSlot);
			w->stack_top = stack_va + PAGE_SIZE;
			w->entry_va = SMPSOAK_CODE_VA;
			char name[8] = {'u', 's', 'o', 'a', 'k', (char)('0' + i), 0, 0};
			w->thr = AuCreateKthread(AuSMPSoakWorkerEntry, pd, name);
			w->thr->threadType = THREAD_LEVEL_USER | THREAD_LEVEL_NOPROC;
		}
	}
	/* pause/wake pair in an address space of its own */
	{
		uint64_t* pd = AuCreateVirtualAddressSpace();
		uint64_t code = soakMapPage(pd, SMPSOAK_CODE_VA, PTE_USER_EXECUTABLE | PTE_NORMAL_MEM | PTE_AP_RW_USER);
		uint64_t data = soakMapPage(pd, SMPSOAK_DATA_VA, PTE_NORMAL_MEM | PTE_AP_RW_USER);
		if (!pd || !code || !data) {
			UARTDebugOut("[smpsoak]: pair setup failed \r\n");
			return;
		}
		memcpy((void*)P2V(code), aa64_smp_soak_user, code_len);
		soakSyncCode((void*)P2V(code), code_len);
		s_pair = (SoakPair*)P2V(data);
		s_pair->signum = SIGUSR1;
		for (int j = 0; j < SMPSOAK_PAIR_WORKERS; j++) {
			int i = SMPSOAK_WORKERS + j;
			uint64_t stack_va = SMPSOAK_STACK_VA + (uint64_t)i * 0x2000;
			if (!soakMapPage(pd, stack_va, PTE_NORMAL_MEM | PTE_AP_RW_USER)) {
				UARTDebugOut("[smpsoak]: pair stack setup failed \r\n");
				return;
			}
			SoakWorker* w = &s_workers[i];
			w->slot_va = SMPSOAK_DATA_VA;
			w->stack_top = stack_va + PAGE_SIZE;
			uint8_t* entry = j == 0 ? aa64_smp_soak_waiter : aa64_smp_soak_waker;
			w->entry_va = SMPSOAK_CODE_VA + (uint64_t)(entry - aa64_smp_soak_user);
			char name[8] = {'u', 'p', 'a', 'i', 'r', (char)('0' + j), 0, 0};
			w->thr = AuCreateKthread(AuSMPSoakWorkerEntry, pd, name);
			w->thr->threadType = THREAD_LEVEL_USER | THREAD_LEVEL_NOPROC;
			if (j == 0)
				s_pair->waiter_tid = w->thr->thread_id;
		}
	}

	AuCreateKthread(AuSMPSoakReporter, AuCreateVirtualAddressSpace(), "smpsoak");
	UARTDebugOut("[smpsoak]: %d user workers in %d address spaces \r\n", SMPSOAK_WORKERS,
				 SMPSOAK_SPACES);
}

#endif
