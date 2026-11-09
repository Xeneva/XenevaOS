/**
* @file clean.c
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

#include <Ipc/auipc.h>
#include <aurora.h>
#include <clean.h>
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <Mm/tlsf.h>
#include <Mm/shm.h>
#include <Hal/AA64/sched.h>
#include <_null.h>

static void AuCleanThread(AA64Thread* thr) {
	/* its stack is freed below */
	AuThreadWaitOffCPU(thr);
	AuThreadCleanTrash(thr);
	if (thr->originalKSp) {
		uint64_t location = (thr->originalKSp + 64) - KERNEL_STACK_SIZE;
		AuFreePages(location, true, KERNEL_STACK_SIZE);
	}
	AuUserEntry* uentry = thr->uentry;
	if (uentry) {
		/* A process killed before its first run still owns argv copies. */
		if (uentry->argvs) {
			for (int i = 0; i < uentry->num_args; ++i)
				kfree(uentry->argvs[i]);
			kfree(uentry->argvs);
		}
		kfree(uentry);
	}
	kfree(thr->waitlist);
	kfree(thr);
}

static void AuCleanList(list_t* list) {
	if (!list)
		return;
	while (list->pointer)
		kfree(list_remove(list, 0));
	kfree(list);
}

/**
 * @brief AuProcessClean -- clean up a process
 * @param parent -- Pointer to parent process
 * @param killable -- Killable process
 */
void AuProcessClean(AuProcess* parent, AuProcess* killable) {
	if (!AuProcessCanReap(killable))
		return;
	UARTDebugOut("[aurora-clean]: killing process : %s \r\n", killable->name);
	/* a thread of killable may still be finishing its time slice on
	 * another core, nothing below may run under it */
	AuThreadWaitOffCPU(killable->main_thread);
	for (int i = 0; i < killable->num_thread; ++i)
		AuThreadWaitOffCPU(killable->threads[i]);
	AuIpcProcessGone(killable);
	AuSHMUnmapAll(killable);
	AA64Thread* main_thr = killable->main_thread;
	for (int i = 0; i < killable->num_thread; ++i) {
		AA64Thread* thr = killable->threads[i];
		killable->threads[i] = NULL;
		if (!thr || thr == main_thr)
			continue;
		/* Drop aliases before poisoning the thread object. */
		for (int j = i + 1; j < killable->num_thread; ++j) {
			if (killable->threads[j] == thr)
				killable->threads[j] = NULL;
		}
		AuCleanThread(thr);
	}
	if (main_thr)
		AuCleanThread(main_thr);
	killable->main_thread = NULL;

	/* Walk actual mappings, including executable pages, signal trampoline,
	 * stacks and sparse/explicit mmap ranges. Length counters miss holes
	 * and cannot reclaim the page tables themselves. */
	AuDestroyVirtualAddressSpace(killable->cr3);
	killable->cr3 = NULL;
	AuCleanList(killable->vmareas);
	killable->vmareas = NULL;
	AuProcessWakeWaiters(killable);

	/* Keep the process alive until diagnostics have read its PID/name.
	 * kfree poisons both fields in debug builds. */
	AuPmmOwnerTeardownCheck(killable->proc_id, killable->name);
	AuRemoveProcess(parent, killable);
	UARTDebugOut("[aurora-clean]: kernel heap in use: %d bytes\r\n", tlsf_used(tlsf_get_pool()));

	AuPmmStats pmm_stats;
	AuPmmngrGetStats(&pmm_stats);
	size_t total_ram = (pmm_stats.managed_pages * PAGE_SIZE) / 1024 / 1024;
	size_t used_ram = (pmm_stats.allocated_pages * PAGE_SIZE) / 1024 / 1024;
	size_t free_ram = (pmm_stats.free_pages * PAGE_SIZE) / 1024 / 1024;
	UARTDebugOut("[aurora-clean]: process cleaned successfully \r\n");
	UARTDebugOut("total mem : %d mb, used mem : %d mb , free mem : %d mb\r\n",
				 total_ram,
				 used_ram,
				 free_ram);
}

/**
 * @brief AuExitSubThread -- exit a sub thread of a process
 * @param proc -- pointer to the process
 * @param thread_id -- sub thread id
 */
void AuExitSubThread(AuProcess* proc, AA64Thread* thread, int thread_id) {
	for (int i = 0; i < proc->num_thread; i++) {
		AA64Thread* thr = proc->threads[i];
		if (!thr)
			continue;
		if (thr->thread_id == thread_id && thr == thread) {
			/* we cannot directly kill thread here, freeing up the running
			   stack will immediately stall the kernel, so better let
			   AuThreadMoveToTrash mark it killable and remove it from the
			   scheduler queues. Setting THREAD_STATE_KILLABLE here first
			   makes MoveToTrash take its early return and leaves the dead
			   thread in the ready list, where a later pthread_join finds it
			   and blocks forever.
			*/
			AuProcessFreeKeResource(thr);
			AuThreadMoveToTrash(thr);
			break;
		}
	}
}
