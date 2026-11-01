/**
* @file process.c
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

#include <process.h>
#include <aucon.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <clean.h>
#include <Mm/pmmngr.h>
#include <string.h>
#include <_null.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/shm.h>
#include <loader.h>
#include <Ipc/postbox.h>
#include <Drivers/uart.h>
#include <timer.h>
#include <Cap/capability.h>
#include <Fs/Dev/devfs.h>
#include <Sound/sound.h>

static int pid = 1;
AuProcess* proc_first;
AuProcess* proc_last;
AuProcess* root_proc;
/**
 * @brief AuAddProcess -- adds process to kernel data structure
 * @param root -- pointer to the root process
 * @param proc -- process to add
 */
void AuAddProcess(AuProcess* parent, AuProcess* proc) {
	(void)parent;
	proc->next = NULL;
	proc->prev = NULL;

	if (proc_first == NULL) {
		proc_last = proc;
		proc_first = proc;
	} else {
		proc_last->next = proc;
		proc->prev = proc_last;
	}
	proc_last = proc;
}

/**
 * @brief AuRemoveProcess -- removes a process from the process
 * data structure
 * @param parent -- pointer to the parent process
 * @param proc -- process to remove
 */
void AuRemoveProcess(AuProcess* parent, AuProcess* proc) {
	(void)parent;
	if (proc_first == NULL)
		return;

	if (proc == proc_first) {
		proc_first = proc_first->next;
	} else {
		proc->prev->next = proc->next;
	}

	if (proc == proc_last) {
		proc_last = proc->prev;
	} else {
		proc->next->prev = proc->prev;
	}
	if (proc->mmap_lock)
		AuDeleteSpinlock(proc->mmap_lock);
	kfree(proc);
}

/**
 * @brief AuProcessFindByPID -- finds a process by its pid
 * @param parent -- parent process to search in
 * @param pid -- process id to find
 */
AuProcess* AuProcessFindByPID(AuProcess* proc, int pid) {
	(void)proc;
	for (AuProcess* proc_ = proc_first; proc_ != NULL; proc_ = proc_->next) {
		if (proc_->proc_id == pid)
			return proc_;
	}
	return NULL;
}

/**
* @brief AuProcessFindByThread -- finds a process by its main thread
* @param parent -- parent process to search in
* @param thread -- thread to find
*/
AuProcess* AuProcessFindByThread(AuProcess* proc, AA64Thread* thread) {
	(void)proc;
	for (AuProcess* proc_ = proc_first; proc_ != NULL; proc_ = proc_->next) {
		if (proc_->main_thread == thread) {
			return proc_;
		}
	}
	return NULL;
}

/**
 * @brief AuProcessFindPID -- finds a process by its pid from
 * the process tree
 * @param pid -- process id of the process
 */
AuProcess* AuProcessFindPID(int pid) {
	AuProcess* proc_;
	for (proc_ = proc_first; proc_ != NULL; proc_ = proc_->next) {
		if (proc_->proc_id == pid)
			return proc_;
	}
	return NULL;
}

/**
 * @brief AuProcessFindThread -- finds a process by its
 * main thread
 * @param thread -- pointer to  main thread
 */
AuProcess* AuProcessFindThread(AA64Thread* thread) {
	for (AuProcess* proc_ = proc_first; proc_ != NULL; proc_ = proc_->next) {
		if (proc_->main_thread == thread) {
			return proc_;
		}
	}

	return NULL;
}

/**
 * @brief AuProcessFindSubThread -- find a process from its
 * sub threads which contain a pointer to its process
 * slot
 * @param thread -- Pointer to sub thread
 */
AuProcess* AuProcessFindSubThread(AA64Thread* thread) {
	AuProcess* proc = (AuProcess*)thread->procSlot;
	return proc;
}

/**
 * @brief AuAllocateProcessID -- allocates a new
 * pid and return
 */
int AuAllocateProcessID() {
	size_t _pid = pid;
	pid = pid + 1;
	return _pid;
}

#define USER_STACK_FLAG (1ULL << 54 | 2ULL << 6 | 1ULL << 10 | PTE_NORMAL_MEM | 1)
/**
 * @brief CreateUserStack -- creates new user stack
 * @param proc -- Pointer to process slot
 * @param cr3 -- pointer to the address space where to
 * map
 */
uint64_t* CreateUserStack(AuProcess* proc, uint64_t* cr3) {
#define USER_STACK 0x000000A000000000
	uint64_t location = USER_STACK;
	location += proc->_user_stack_index_;

	for (size_t i = 0; i < PROCESS_USER_STACK_SZ / PAGE_SIZE; ++i) {
		uint64_t blk = (uint64_t)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id);
		if (!AuMapPageEx(
				cr3, blk, location + i * PAGE_SIZE, PTE_NORMAL_MEM | PTE_AP_RW_USER | PTE_AP_RW)) {
			UARTDebugOut("CreateUserStack: already mapped %x \r\n", (location + i * PAGE_SIZE));
		}
	}

	proc->_user_stack_index_ += PROCESS_USER_STACK_SZ;
	uint64_t* addr = (uint64_t*)(location + PROCESS_USER_STACK_SZ);
	return addr;
}

/**
 * @brief CreateSubUserStack -- creates new user stack
 * @param proc -- Pointer to process slot
 * @param cr3 -- pointer to the address space where to
 * map
 */
uint64_t* CreateSubUserStack(AuProcess* proc, uint64_t* cr3) {
#define USER_STACK 0x000000A000000000
	uint64_t location = USER_STACK;
	UARTDebugOut("User stack index : %x \r\n", proc->_user_stack_index_);
	location += proc->_user_stack_index_;

	/* must match CreateUserStack: Normal memory + map into the process
	 * address space. Device-mapped stacks fault on unaligned STP/STUR
	 * (term.exe asyncth: stur d0, [sp,#0x14] -> FAR A0000FFF34) --axiss */
	for (size_t i = 0; i < PROCESS_USER_STACK_SZ / PAGE_SIZE; ++i) {
		uint64_t blk = (uint64_t)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id);
		if (!AuMapPageEx(
				cr3, blk, location + i * PAGE_SIZE, PTE_NORMAL_MEM | PTE_AP_RW_USER | PTE_AP_RW)) {
			UARTDebugOut("CreateSubUserStack: already mapped %x \r\n", (location + i * PAGE_SIZE));
		}
	}

	proc->_user_stack_index_ += PROCESS_USER_STACK_SZ;
	uint64_t* addr = (uint64_t*)(location + PROCESS_USER_STACK_SZ);
	return addr;
}
/**
* @brief AuCreateProcessSlot -- creates a blank process slot
* @param parent -- pointer to the parent process
*/
AuProcess* AuCreateProcessSlot(AuProcess* parent, char* name) {
	AuProcess* proc = (AuProcess*)kmalloc(sizeof(AuProcess));
	if (!proc)
		return NULL;
	memset(proc, 0, sizeof(AuProcess));
	if (name)
		strncpy(proc->name, name, sizeof(proc->name) - 1);
	proc->name[sizeof(proc->name) - 1] = '\0';

	proc->proc_id = AuAllocateProcessID();
	/* create empty virtual address space */
	uint64_t* cr3 = AuCreateVirtualAddressSpace();
	/* create the process main thread stack */
	uint64_t main_thr_stack = (uint64_t)CreateUserStack(proc, cr3);
	proc->state = PROCESS_STATE_NOT_READY;
	proc->cr3 = cr3;
	proc->shm_break = USER_SHARED_MEM_START;
	proc->proc_mem_heap = PROCESS_BREAK_ADDRESS;
	proc->proc_heapmem_len = 0;
	proc->mmap_next = PROCESS_MMAP_ADDRESS;
	proc->mmap_lock = AuCreateSpinlock(false);
	proc->_kstack_index_ = 1;
	proc->_main_stack_ = main_thr_stack;
	proc->prev_sample_time_us = AuGetCurrentUS();
	proc->prev_sample_runtime_us = 0;
	uint64_t* envpBlock =
		(uint64_t*)P2V((size_t)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id));
	memset(envpBlock, 0, PAGE_SIZE);

	/** confusing code :hehehehe **/
	if (!AuMapPageEx(
			cr3, (uint64_t)V2P((size_t)envpBlock), 0x5000, PTE_AP_RW_USER | PTE_NORMAL_MEM))
		UARTDebugOut("Failed to map environment block for proc %s \r\n", name);
	else
		proc->_envp_block_ = 0x5000;

	if (parent) {
		memcpy((void*)envpBlock, (void*)parent->_envp_block_, PAGE_SIZE);

		/** just inherit all parent credential's to belong to that 
		 * uac and group
		 */
		proc->creds.uid = parent->creds.uid;
		proc->creds.gid = parent->creds.gid;
		proc->creds.num_sgid = parent->creds.num_sgid;
		for (int i = 0; i < parent->creds.num_sgid; i++)
			proc->creds.sgid[i] = parent->creds.sgid[i];
		proc->creds.caps = parent->creds.caps;
	}

	proc->waitlist = initialize_list();
	proc->vmareas = initialize_list();
	proc->shmmaps = initialize_list();
	uint64_t ustack = proc->_main_stack_;
	proc->_main_stack_ = (((uint64_t)ustack + 15) & ~(uint64_t)0xF);
	AuAddProcess(parent, proc);
	return proc;
}

/**
 * @brief AuProcessGetFileDesc -- returns a empty file descriptor
 * from process slot, 0, 1 & 2 are reserved for terminal
 * output
 * @param proc -- pointer to process slot
 */
int AuProcessGetFileDesc(AuProcess* proc) {
	for (int i = 3; i < (FILE_DESC_PER_PROCESS - 3); i++) {
		if (!proc->fds[i]) {
			return i;
		}
	}
	return -1;
}

/**
*  @brief Creates a user mode thread
*  @param entry -- Entry point address
*  @param stack -- Stack address
*  @param cr3 -- the top most page map level address
*  @param name -- name of the thread
*  @param priority -- (currently unused) thread's priority
*/
int AuCreateUserthread(AuProcess* proc, void (*entry)(), char* name, uint64_t arg) {
	UARTDebugOut("[aurora]: user thread creating kmapping : %s \r\n", proc->name);
	uint64_t stack = AuCreateKernelStack(proc->cr3);
	uint64_t kstack = stack;
	stack = ((uint64_t)kstack & ~(uint64_t)0xF);
	stack -= 64;
	AA64Thread* thr = AuCreateSubKthread(AuProcessEntSubThread, stack, proc->cr3, name);
	thr->threadType = THREAD_LEVEL_USER;
	thr->first_run = 0;
	thr->procSlot = proc;
	AuUserEntry* uentry = (AuUserEntry*)kmalloc(sizeof(AuUserEntry));
	memset(uentry, 0, sizeof(AuUserEntry));
	uentry->argvaddr = arg;
	uentry->entrypoint = (uint64_t)entry;
	uentry->argvs = 0;
	uentry->num_args = 0;
	uentry->rsp = (uint64_t)CreateSubUserStack(proc, proc->cr3);
	uentry->stackBase = uentry->rsp;
	thr->uentry = uentry;
	if (proc->num_thread > MAX_THREADS_PER_PROCESS) {
		//free kstack
		//free up sub kthread
		kfree(uentry);
		return -1;
	}
	proc->threads[proc->num_thread] = thr;
	proc->num_thread += 1;
	return thr->thread_id;
}

/**
 * @brief AuProcessFreeKeResource -- free up allocated kernel
 * resources
 * @param thr -- Pointer to thread which allocated
 * kernel resources
 */
void AuProcessFreeKeResource(AA64Thread* thr) {
	if (!thr)
		return;
	AuSoundRemoveDSP(thr->thread_id);
	PostBoxDestroyByID(thr->thread_id);
	int timer_id = AuGetTimerByThread(thr);
	if (timer_id != -1)
		AuroraTimerCancel(timer_id);

	AuThreadAwakeWaiters(thr);
}

static void AuProcessCloseFiles(AuProcess* proc) {
	BordoisilaCapCleanupProcess(proc);
	for (int i = 0; i < FILE_DESC_PER_PROCESS; i++) {
		AuVFSNode* file = proc->fds[i];
		if (!file)
			continue;
		proc->fds[i] = NULL;
		UARTDebugOut(
			"[AuProcessExit]: closing file : %s flags %x\r\n", file->filename, file->flags);
		if (file->flags & FS_FLAG_CACHED) {
			UARTDebugOut("[AuProcessExit]: cached file skipped close : %s, flags : %x\r\n",
						 file->filename);
			if (file->fileCopyCount > 0)
				file->fileCopyCount -= 1;
			continue;
		}
		if (file->flags & FS_FLAG_DEVICE) {
			/* The node belongs to the device layer and is shared by
			 * every open, so it is not freed here; AuDevFSNodeClose runs
			 * the close hook (DCL routes it to the module's release, and
			 * a DRM file's master lives there -- skipping it left
			 * card0/card1 master set after the boot self-test exited, so
			 * a later dcltest had every DRM_MASTER ioctl refused with
			 * -EACCES) and keeps the driver image referenced until the
			 * final fd release. --axiss */
			AuDevFSNodeClose(file);
			continue;
		}
		if (file->flags & FS_FLAG_FILE_SYSTEM)
			continue;
		if (file->flags & (FS_FLAG_GENERAL | FS_FLAG_DIRECTORY)) {
			if (file->fileCopyCount <= 0) {
				UARTDebugOut("Freeing up file : %s \r\n", file->filename);
				/* Duplicated descriptors can refer to the same node. */
				for (int j = i + 1; j < FILE_DESC_PER_PROCESS; j++) {
					if (proc->fds[j] == file)
						proc->fds[j] = NULL;
				}
				kfree(file);
			} else
				file->fileCopyCount -= 1;
			continue; /* file may have been freed (and poisoned) above. */
		}
		if ((file->flags & FS_FLAG_SOCKET) && file->close)
			file->close(file, file);
	}
}

void AuProcessWakeWaiters(AuProcess* proc) {
	if (!proc->waitlist)
		return;
	while (proc->waitlist->pointer) {
		AA64Thread* thr = (AA64Thread*)list_remove(proc->waitlist, 0);
		if (thr)
			AuUnblockThread(thr);
	}

	kfree(proc->waitlist);
	proc->waitlist = NULL;
}

/**
 * @brief AuProcessExit -- release resources and queue a process for reaping
 * @param proc -- process to exit
 * @param schedulable -- retained for the shared process API; the caller schedules
 */
void AuProcessExit(AuProcess* proc, bool schedulable) {
	(void)schedulable;
	if (!proc || (proc->state & PROCESS_STATE_DIED) || (proc->state & PROCESS_STATE_BUSY_WAIT)) {
		if (proc)
			UARTDebugOut(
				"[aurora]: process : %s cannot exit, as it is marked dead or busy wait \r\n",
				proc->name);
		return;
	}
	if (proc == root_proc) {
		UARTDebugOut("[aurora]: cannot exit root process \r\n");
		return;
	}
	if (proc->type_flags & PROCESS_TYPE_NON_KILLABLE) {
		UARTDebugOut("[aurora]: process : %s cannot exit \r\n", proc->name);
		return;
	}

	/* Stop other threads before releasing their process-wide resources. */
	AA64Thread* current = AuGetCurrentThread();
	if (proc->main_thread && proc->main_thread != current)
		AuThreadMoveToTrash(proc->main_thread);
	for (int i = 0; i < proc->num_thread; ++i) {
		AA64Thread* thr = proc->threads[i];
		if (thr && thr != current && thr != proc->main_thread)
			AuThreadMoveToTrash(thr);
	}

	AuProcessCloseFiles(proc);
	AuProcessFreeKeResource(proc->main_thread);
	AuSHMUnmapAll(proc);
	AuProcessWakeWaiters(proc);
	for (int i = 0; i < proc->num_thread; i++) {
		AA64Thread* killable = proc->threads[i];

		/* here check if the thread is already marked as THREAD_STATE_KILLABLE, that
		 * should be marked by exit thread call, because some thread may exit after 
		 * finishing up their job
		 */
		if (!killable || killable == proc->main_thread || killable->state == THREAD_STATE_KILLABLE)
			continue;
		AuProcessFreeKeResource(killable);
		if (killable == current)
			AuThreadMoveToTrash(killable);
	}
	/* The main thread completes its handoff in the syscall/abort handler.
	 * Publish death only after exit cleanup is complete. */
	proc->state = PROCESS_STATE_DIED;
}

bool AuProcessCanReap(AuProcess* proc) {
	if (!proc || !(proc->state & PROCESS_STATE_DIED) || AuIsVirtualAddressSpaceActive(proc->cr3))
		return false;
	AA64Thread* current = AuGetCurrentThread();
	AA64Thread* main_thr = proc->main_thread;
	if (main_thr && (main_thr == current || main_thr->state != THREAD_STATE_KILLABLE))
		return false;
	for (int i = 0; i < proc->num_thread; ++i) {
		AA64Thread* thr = proc->threads[i];
		if (thr && (thr == current || thr->state != THREAD_STATE_KILLABLE))
			return false;
	}
	return true;
}

/**
 * @brief AuGetKillableProcess -- return the first process safe to reap
 */
AuProcess* AuGetKillableProcess(void) {
	for (AuProcess* proc_ = proc_first; proc_ != NULL; proc_ = proc_->next) {
		if (AuProcessCanReap(proc_))
			return proc_;
	}

	return NULL;
}

/**
 * @brief AuProcessWaitForTermination -- waits for termination
 * of child processes
 * @param proc -- pointer to process who needs to
 * wait for termination
 * @param pid -- pid of the process, if -1 then any child
 * process
 */
int AuProcessWaitForTermination(AuProcess* proc, int pid) {
	if (pid == -1) {
		AuProcess* killable;
		while ((killable = AuGetKillableProcess()) != NULL)
			AuProcessClean(0, killable);
		proc->state = PROCESS_STATE_SUSPENDED;
		return -1;
	}
	AuProcess* child = AuProcessFindByPID(0, pid);
	if (!child || (child->state & PROCESS_STATE_DIED) || !child->waitlist)
		return 0;
	AA64Thread* thr = AuGetCurrentThread();
	AuBlockThread(thr);
	list_add(child->waitlist, thr);
	return 1;
}

/**
 * @brief AuProcGetNumProcessCount -- returns the total number
 * of process created 
 */
int AuProcGetNumProcessCount() {
	int count = 0;
	for (AuProcess* first = proc_first; first != NULL; first = first->next)
		count++;
	return count;
}

static int AuProcGetOpenFileCount(AuProcess* proc) {
	int count = 0;
	for (int i = 0; i < FILE_DESC_PER_PROCESS; i++) {
		if (!proc->fds[i])
			continue;

		count += 1;
	}
	return count;
}

static uint64_t AuProcessGetLiveRuntime(AuProcess* proc, uint64_t now_us) {
	uint64_t runtime = proc->total_runtime_us;
	/* Process enumeration can observe a slot between create and exec. */
	if (proc->main_thread && proc->main_thread->state == THREAD_STATE_RUNNING)
		runtime += (now_us - proc->main_thread->start_time_us);
	for (int i = 0; i < proc->num_thread; i++) {
		AA64Thread* thr = proc->threads[i];
		if (!thr)
			continue;
		if (thr->state == THREAD_STATE_RUNNING)
			runtime += (now_us - thr->start_time_us);
	}
	return runtime;
}

static uint32_t AuProcessUpdateCPUPercent(AuProcess* proc, uint64_t now) {
	uint64_t live_runtime = AuProcessGetLiveRuntime(proc, now);

	uint64_t runtime_delta = live_runtime - proc->prev_sample_runtime_us;
	uint64_t time_delta = now - proc->prev_sample_time_us;

	if (time_delta == 0)
		proc->cpu_usage = 0;
	else
		proc->cpu_usage = (uint32_t)((runtime_delta * 1000) / time_delta); // * num_cores );

	proc->prev_sample_runtime_us = live_runtime;
	proc->prev_sample_time_us = now;
	return proc->cpu_usage;
}
/**
 * @brief AuProcessFetch -- fetch current process table
 * status
 * @param list -- Pointer to AuProcessList
 * @param num_proc_count -- number of process count
 */
int AuProcessFetch(AuProcessList* list, int num_proc_count) {
	if (!list)
		return 1;

	//AuProcess* first = proc_first;
	uint64_t now = AuGetCurrentUS();

	size_t n = 0;
	for (AuProcess* p = proc_first; p && n < num_proc_count; p = p->next, n++) {
		if (!p)
			break;
		size_t namelen = strlen(p->name);
		if (namelen >= sizeof(list[n].name))
			namelen = sizeof(list[n].name) - 1;
		memcpy(list[n].name, p->name, namelen);
		list[n].name[namelen] = '\0';
		list[n].num_file_opened = AuProcGetOpenFileCount(p);
		list[n].num_threads = p->num_thread;
		list[n].proc_id = p->proc_id;
		list[n].total_runtime_us = p->total_runtime_us;
		list[n].window_runtime_us = p->window_runtime_us;
		p->cpu_usage = AuProcessUpdateCPUPercent(p, now);
		list[n].cpu_usage = p->cpu_usage;
	}

	return 0;
}

/**
 * @brief AuProcessReapWaitCount -- decrease waiting thread counts
 * @param proc -- desired process
 * @param num_count -- total number of threads to decrease
 */
void AuProcessReapWaitcount(AuProcess* proc, int num_count) {
	proc->waiting_threads -= num_count;
	if ((int16_t)proc->waiting_threads <= 0) {
		UARTDebugOut("[aurora]: process : %s has now %d waiting threads, marking it free \r\n",
					 proc->name,
					 proc->waiting_threads);
		proc->waiting_threads = 0;
		proc->state &= ~PROCESS_STATE_BUSY_WAIT;
	}
}

AuProcess* AuProcessCurrent() {
	AA64Thread* thr = AuGetCurrentThread();
	if (!thr)
		return NULL;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return NULL;
	}
	return proc;
}
