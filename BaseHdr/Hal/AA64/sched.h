/**
* @file sched.h
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

#ifndef __AA64_SCHED_H__
#define __AA64_SCHED_H__

#include <stdint.h>
#include <Hal/AA64/aa64cpu.h>
/* Pointer only. The full <list.h> declares list_add(list_t*, void*), which
 * cannot share a file with mainline list_add(list_head*, list_head*).
 * DCL/linux_irq_shim.c includes this header and then <linux/kernel.h>. */
typedef struct _list_ list_t;

#ifdef ARCH_ARM64

#define THREAD_STATE_READY			1
#define THREAD_STATE_BLOCKED		3
#define THREAD_STATE_SLEEP			4
#define THREAD_STATE_KILLABLE		5
#define THREAD_STATE_LEFT_IN_KERNEL 6
#define THREAD_STATE_PENDING_SIGNAL 7
#define THREAD_STATE_RUNNING		8

//! Thread levels =========================================================
//! THREAD_LEVEL_KERNEL -- This bit is set when the thread given is kernel mode
//! THREAD_LEVEL_USER -- This bit is set when the thread given is user mode

#define THREAD_LEVEL_KERNEL		 (1 << 0)
#define THREAD_LEVEL_USER		 (1 << 1)
#define THREAD_LEVEL_SUBTHREAD	 (1 << 2)
#define THREAD_LEVEL_MAIN_THREAD (1 << 3)
//! THREAD_LEVEL_NOPROC -- user mode thread that belongs to no process
//! (kernel self tests); it may use the syscall fast path without one
#define THREAD_LEVEL_NOPROC		 (1 << 4)

//#pragma pack(push,1)
/* AuUserEntry structure */
typedef struct _uentry_ {
	uint64_t entrypoint;
	uint64_t rsp;
	uint64_t cs;
	uint64_t ss;
	int num_args;
	uint64_t argvaddr;	 /* user-space VA of argv[] page (for crt0 via stack) */
	uint64_t argvkernel; /* kernel-space VA of argv[] page (for EL1 writes) */
	char** argvs;
	uint64_t stackBase;
	/* PROCESS_TYPE_LINUX aux vector. Zero on a PE process. */
	uint64_t linux_phdr;
	uint16_t linux_phent;
	uint16_t linux_phnum;
} AuUserEntry;
//#pragma pack(pop)

#define MAX_SIGNAL_ENTRY 10

typedef struct _gp_context_ {
	uint64_t x[31];
	uint64_t sp_el0;
	uint64_t elr_el1;
	uint64_t spsr_el1;
} _gp_context_t;

typedef struct _signal_frame_ {
	AA64Registers regs;
	uint64_t elr_el1;
	uint64_t sigret_address;
} _signal_frame_t;

//#pragma pack(push,1)
typedef struct _aa64_task_ {
	uint64_t x19;		  //0
	uint64_t x20;		  //8
	uint64_t x21;		  //16
	uint64_t x22;		  //24
	uint64_t x23;		  //32
	uint64_t x24;		  //40
	uint64_t x25;		  //48
	uint64_t x26;		  //56
	uint64_t x27;		  //64
	uint64_t x28;		  //72
	uint64_t x29;		  //80
	uint64_t x30;		  //88
	uint64_t sp;		  //96
	uint64_t elr_el1;	  //104
	uint64_t spsr_el1;	  //112
	uint64_t x0;		  //120
	uint64_t x1;		  //128
	uint64_t x2;		  //136
	uint64_t x3;		  //144
	uint64_t x4;		  //152
	uint64_t x5;		  //160
	uint64_t x6;		  //168
	uint64_t x7;		  //176
	uint64_t x8;		  //184
	uint8_t state;		  //192
	uint8_t threadType;	  //193
	uint64_t pml;		  //186
	char name[8];		  //130
	void* procSlot;		  //138
	AuUserEntry* uentry;  //146
	bool first_run;		  //206
	uint64_t thread_id;	  //207
	uint64_t sleepQuanta; //215
	uint64_t originalKSp; //223
	bool returnFromSyscall;
	bool justStored;
	void* data;
	AU_ALIGN(16) uint8_t fp_regs[512];
	uint64_t fpcr;
	uint64_t fpsr;
	uint32_t syscallNum;
	uint64_t sig_pending;
	_signal_frame_t signal;
	uint64_t start_time_us;
	uint64_t* sigs[32];
	struct _aa64_task_* next;
	struct _aa64_task_* prev;
	/* EDF real-time class (implicit-deadline periodic model). Zeroed by
	 * default: thread is best-effort round-robin. See Hal/sched_edf.c. */
	uint8_t edf_enabled;
	uint8_t edf_waiting;
	uint64_t edf_period_us;
	uint64_t edf_wcet_us;
	uint64_t edf_deadline_us;
	uint64_t edf_release_us;
	uint64_t edf_misses;
	/** waitlist */
	list_t* waitlist;
	uint16_t wait_ref_count;
	/* Linux ELF pthread. Saved across switches because EL0 writes TPIDR_EL0. */
	uint64_t linux_tls;
	uint64_t clear_child_tid;
	uint64_t futex_uaddr;
	uint32_t futex_bitset;
	uint8_t futex_waiting;
	/* Result to place in x0/x6 when a futex waiter is resumed.  This is
	 * needed because AuScheduleThread resumes at EL0 and never returns to
	 * the kernel wait path.  futex_active means the SVC frame has not been
	 * stored yet; futex_frame_valid means it has and may be patched. */
	int futex_result;
	uint8_t futex_active;
	uint8_t futex_frame_valid;
	/** absolute wake-up time (system us) of a sleeping thread; see AuSleepThread */
	uint64_t sleep_until_us;
	uint8_t blocked_on_poll;
	/* SMP: 1 while some core executes on this thread's context or kernel
	 * stack. A core claims it when it picks the thread and the switch
	 * path in aa64switch.s drops it only once that core is off the stack,
	 * so no two cores ever run on the same stack. Keep these two at the
	 * end: aa64switch.s hard codes the offsets of the fields above. */
	volatile uint32_t on_cpu;
	/* BKL depth the thread held when it was switched out (Sync/bkl.h) */
	uint32_t bkl_depth;
	/* exception frame of the syscall this thread is executing, see
	 * AA64GetCurrentRegCtx. Per thread: syscalls run on several cores */
	AA64Registers* syscall_regs;
	/* a wakeup arrived while the thread was not blocked; its next pause
	 * returns at once instead of losing it (AuThreadPark) */
	volatile uint32_t wake_pending;
	/* scheduler list the thread is linked on (sched.c SCHED_LIST_*),
	 * 0 when on none; guards against unlinking it from the wrong list */
	uint32_t sched_list;
	/* core the thread is bound to, + 1; 0 = may run on any general core.
	 * Bound threads are the only ones an isolated core runs */
	uint32_t cpu_bind;
} AA64Thread;

//#pragma pack(pop)
extern void AuSchedulerInitialize();
extern uint64_t AuCreateKernelStack(uint64_t* pml);
AU_EXTERN AU_EXPORT AA64Thread* AuCreateKthread(void (*entry)(uint64_t), uint64_t* pml, char* name);
/**
 * @brief AuCreateSubKthread -- create sub kernel thread of parent
 * kthread
 * @param entry -- Pointer to entry point
 * @param pml -- Pointer to Page directory
 * @param name -- Name of the thread
 * @return Pointer to newly created thread
 */
AU_EXTERN AU_EXPORT AA64Thread*
AuCreateSubKthread(void (*entry)(uint64_t), uint64_t stack, uint64_t* pml, char* name);

extern void AuScheduleThread(AA64Registers* regs);
extern void AuScheduleNext();
/* Park the caller for about ms and run another thread. Returns 0, or
 * -1 when the scheduler is not running yet. */
extern int AuSchedYieldMs(uint64_t ms);
extern void AuSchedulerStart();
extern AA64Thread* AuGetIdleThread();
extern AA64Thread* AuGetCurrentThread();

/**
 * @brief AuBlockThread -- blocks a running thread
 * @param thread -- Pointer to AA64 Thread
 */
AU_EXTERN AU_EXPORT void AuBlockThread(AA64Thread* thread);

/**
* @brief AuUnblockThread -- unblocks a thread and insert it to
* ready list
* @param t -- pointer to thread
*/
AU_EXTERN AU_EXPORT void AuUnblockThread(AA64Thread* thread);

/**
 * @brief AuThreadWakePoller -- wake up a thread that was left
 * in sleep/block while polling
 * @param t -- Pointer to thread
 */
AU_EXTERN AU_EXPORT void AuThreadWakePoller(AA64Thread* t);
/**
 * @brief AuForceScheduler -- force the scheduler
 * to switch next thread
 */
AU_EXTERN AU_EXPORT void AuForceScheduler();

/**
 * @brief AuSleepThread -- block a running thread
 * and put it into sleep list
 * @param thread -- Pointer to AA64 Thread
 */
AU_EXTERN AU_EXPORT void AuSleepThread(AA64Thread* thread, uint64_t ms);

/**
 * @brief AuThreadFindByID -- finds a thread by its id from
 * ready queue
 * @param id -- id of the thread
 */
AU_EXTERN AU_EXPORT AA64Thread* AuThreadFindByID(uint64_t id);

/**
 * @brief AuThreadFindByIDBlockList -- finds a thread by its id from
 * the block queue
 * @param id -- id of the thread
 */
AU_EXTERN AU_EXPORT AA64Thread* AuThreadFindByIDBlockList(uint64_t id);

/**
 * @brief AuThreadFindByIDSleepList -- finds a thread by its id from
 * the sleep queue
 * @param id -- thread id
 */
AU_EXTERN AU_EXPORT AA64Thread* AuThreadFindByIDSleepList(uint64_t id);
/**
 * @brief AuThreadMoveToTrash -- move given thread to
 * trash
 * @param t -- Thread to move to trash
 */
AU_EXTERN AU_EXPORT void AuThreadMoveToTrash(AA64Thread* t);

/**
 * @brief AuThreadCleanTrash -- clean a thread from
 * trash list
 */
extern void AuThreadCleanTrash(AA64Thread* t);

/**
 * @brief AuGetSystemTimerTick -- return the current system
 * timer tick
 */
extern uint64_t AuGetSystemTimerTick();

/**
 * @brief AuHandleSleepThreads -- handle sleep thread, 
 * rearrange all sleep threads
 */
extern void AuHandleSleepThreads();

/**
 * @brief AuThreadMakeReady -- make a thread forcefully
 * ready for next
 * @param thread -- pointer to thread struct
 */
extern void AuThreadMakeReady(AA64Thread* thread);

/* Scheduler list lock (mask + spinlock, DAIF save/restore). Tick path
 * runs masked already; thread context must use these (or the locking
 * AuThread* wrappers) around list access. */
extern uint64_t AuSchedLock(void);
extern void AuSchedUnlock(uint64_t saved_daif);
extern bool AuSchedValidateLists(void);

/* EDF real-time class, see Hal/sched_edf.c. All no-ops / NULL-safe when
 * the thread is not EDF-enabled; best-effort threads are unaffected. */
extern bool AuEDFSetParams(AA64Thread* thread, uint64_t period_us, uint64_t wcet_us);
extern void AuEDFRemove(AA64Thread* thread);
extern void AuEDFWaitPeriod(AA64Thread* thread);
extern void AuEDFHandleReleases(uint64_t now_us);
extern AA64Thread* AuEDFPickNext(void);
extern uint64_t AuEDFGetMisses(AA64Thread* thread);
extern void AuEDFSelfTestStart(void);
extern void AuSoakStart(void);
/* SMP user mode soak test (SMPSOAK=1 builds), see Hal/smp_soak.c */
extern void AuSMPSoakStart(void);

/**
 * @brief AuThreadWaitForTermination -- wait for desired thread
 * termination
 * @param thread_id -- desired thread id
 */
extern int AuThreadWaitForTermination(int thread_id);
/**
 * @brief AuThreadAwakeWaiters -- unblock all awaiting
 * threads for the current thread
 * @praram thread -- current thread which waitlist
 * needs to be freed
 */
extern void AuThreadAwakeWaiters(AA64Thread* thread);

/**
 * @brief AuSchedCreateAPIdleThread -- create the idle thread of a
 * secondary core, called on the boot core
 * @param cpu_num -- logical cpu number of the secondary core
 */
extern AA64Thread* AuSchedCreateAPIdleThread(uint32_t cpu_num);

/**
 * @brief AuSchedEnterAPIdle -- start the calling secondary core's timer
 * and switch onto its idle thread, never returns
 */
extern void AuSchedEnterAPIdle(void) __attribute__((noreturn));

/**
 * @brief AuSchedCanRunHere -- may the calling core resume this thread now
 * (not running elsewhere, and kernel contexts only on the boot core).
 * Call with the scheduler lock held.
 */
extern bool AuSchedCanRunHere(AA64Thread* thread);

/**
 * @brief AuThreadPark -- block the calling thread for a pause unless a
 * wakeup is already pending for it
 * @return true if it blocked (caller reschedules), false otherwise
 */
extern bool AuThreadPark(AA64Thread* thread);

/**
 * @brief AuThreadUnpark -- wake a paused thread, or leave it a wakeup
 * token so its next pause returns at once
 */
extern void AuThreadUnpark(AA64Thread* thread);

/*
 * Core reservation (isolation). Two modes:
 *  - all cores general (mask 0, the default): ordinary SMP scheduling;
 *  - some cores isolated: an isolated core runs only threads bound to it
 *    (AuThreadSetAffinity), gets no general work, no wake-up IPIs except
 *    for its own threads, and stops its timer while a single bound thread
 *    owns it, so that thread is never preempted. Meant for latency
 *    critical workloads: LLM inference workers, HFT event loops, RT tasks.
 * Core 0 stays general: it takes device interrupts and the system tick.
 */

/**
 * @brief AuCpuSetIsolatedMask -- set the isolated cores (bit n = core n),
 * 0 returns every core to general use
 * @return 0, -1 if the mask names core 0, -2 if it names an offline core
 */
extern int AuCpuSetIsolatedMask(uint32_t mask);

/** @brief AuCpuGetIsolatedMask -- currently isolated cores */
extern uint32_t AuCpuGetIsolatedMask(void);

/**
 * @brief AuThreadSetAffinity -- bind a thread to one core, -1 unbinds.
 * The thread moves there at its next reschedule
 * @return 0, -1 for a bad or offline core
 */
extern int AuThreadSetAffinity(AA64Thread* thread, int cpu);

/**
 * @brief AuThreadWaitOffCPU -- wait until no core executes on the
 * thread's context any more, call before freeing its stack or address
 * space. Drops the BKL while waiting.
 */
extern void AuThreadWaitOffCPU(AA64Thread* thread);

#endif

#endif