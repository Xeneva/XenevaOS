/**
* @file mmserv.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/shm.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <_null.h>
#include <Drivers/uart.h>
#include <aucon.h>

/* ============================================================
 *  Shared Memory
 * ============================================================
 */
/**
  * @brief CreateSharedMem -- create a shared memory chunk
  * @param key -- key to use
  * @param sz -- memory size
  * @param flags -- shared memory flags
  */
int CreateSharedMem(uint16_t key, size_t sz, uint8_t flags) {
	AA64Thread* thr = AuGetCurrentThread();
	if (!thr)
		return -1;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc) {
			return -1;
		}
	}
	int id = AuCreateSHM(proc, key, sz, flags);
	return id;
}

/**
 * @brief ObtainSharedMem -- obtain a shared memory
 * @param id -- segment id
 * @param shmaddr -- user specified address
 * @param shmflg -- flags to use for protection
 */
void* ObtainSharedMem(uint16_t id, void* shmaddr, int shmflg) {
	AA64Thread* thr = AuGetCurrentThread();
	if (!thr)
		return 0;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return NULL;
	}
	return AuSHMObtainMem(proc, id, shmaddr, shmflg);
}

/**
 * @brief UnmapSharedMem -- unmap shared memory segment
 * @param key -- key to search
 */
void UnmapSharedMem(uint16_t key) {
	AA64Thread* thr = AuGetCurrentThread();
	if (!thr) //some serious memory problem
		return;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return;
	}
	AuSHMUnmap(key, proc);
}

/**
 * @brief GetProcessHeapMem -- get a memory from
 * process heap
 * @param sz -- size in bytes
 */
uint64_t GetProcessHeapMem(size_t sz) {
	/* check if size is page aligned */
	if ((sz % PAGE_SIZE) != 0) {
		sz = PAGE_ALIGN(sz);
	}

	AA64Thread* thr = AuGetCurrentThread();
	if (!thr)
		return -1;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc) {
			return -1;
		}
	}

	if (sz < 0)
		UARTDebugOut("[GetProcessHeapMem]: dlmalloc is asking to free up pages \r\n");

	uint64_t start_addr = (uint64_t)AuGetFreePage(false, (void*)proc->proc_mem_heap);
	for (int i = 0; i < sz / PAGE_SIZE; i++) {
		uint64_t phys = (uint64_t)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id);
		if (phys == PMM_INVALID_PHYS) {
			UARTDebugOut("[heap]: out of pages va=%x\r\n", start_addr + (uint64_t)i * PAGE_SIZE);
			return (uint64_t)-1;
		}
		if (!AuMapPage(phys, start_addr + i * PAGE_SIZE, PTE_AP_RW_USER | PTE_NORMAL_MEM)) {
			UARTDebugOut("already present %x \r\n", (start_addr + i * 0x1000));
			AuPmmngrReleasePage((uint64_t)phys);
		}
	}
	isb_flush();
	dsb_ish();
	dsb_sy_barrier();
	proc->proc_mem_heap = start_addr;
	proc->proc_heapmem_len += sz;
	//UARTDebugOut("Returning heap mem : %x - page : %d, size : %d \r\n", start_addr, (sz / PAGE_SIZE), sz);
	return start_addr;
}

/**
 * @briefProcessHeapUnmap -- unmaps previosly allocated
 * heap memory
 * @param ptr -- Pointer to freeable address
 * @param sz -- size in bytes to unallocate
 */
int ProcessHeapUnmap(void* ptr, size_t sz) {
	UARTDebugOut("[ProcessHeapUnmap]: called : %x , sz : %d \r\n", ptr, sz);
	/* check if size is page aligned */
	if ((sz % PAGE_SIZE) != 0) {
		AuTextOut("Returning error heap unmap -> %d \r\n", sz);
		return -1;
		sz = PAGE_ALIGN(sz);
	}

	AA64Thread* thr = AuGetCurrentThread();
	if (!thr)
		return -1;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc) {
			return -1;
		}
	}
	uint64_t start_addr = (uint64_t)ptr;
	for (int i = 0; i < sz / PAGE_SIZE; i++) {
		AuVPage* page_ = AuVmmngrGetPage(
			start_addr + i * PAGE_SIZE, VIRT_GETPAGE_ONLY_RET, VIRT_GETPAGE_ONLY_RET);
		if (page_) {
			uint64_t phys_page = page_->bits.page << PAGE_SHIFT;
			//UARTDebugOut("Unmap phys page : %x \n", phys_page);
			if (phys_page) {
				//page_->raw = 0;
				page_->bits.present = 0;
				isb_flush();
				page_->bits.page = 0;
				isb_flush();
				/* this used to rely on the full TLB flush every context
				 * switch did. With ASIDs the translation survives, and on
				 * SMP another thread of the process may be using it right
				 * now: invalidate (all ASIDs, all cores), then free */
				tlb_flush(start_addr + i * PAGE_SIZE);
				AuPmmngrReleasePage((uint64_t)phys_page);
			}
		}
	}

	tlb_flush_vmalle1is();
	dsb_ish();
	isb_flush();
	/*if (start_addr < proc->proc_mem_heap)*/
	proc->proc_mem_heap = start_addr;
	return 0;
}