/**
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

#include <Mm/shm.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <aucon.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/serial.h>

/* ============================================================
 *  Shared Memory
 * ============================================================
 */
/*
 * CreateSharedMem -- create a shared memory chunk
 * @param key -- key to use
 * @param sz -- memory size
 * @param flags -- shared memory flags
 */
int CreateSharedMem(uint16_t key, size_t sz, uint8_t flags){
	x64_cli();
	AuThread* thr = AuGetCurrentThread();
	if (!thr)
		return -1;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc){
		proc = AuProcessFindSubThread(thr);
		if (!proc){
			return -1;
		}
	}
	int id = AuCreateSHM(proc, key, sz, flags);
	return id;
}

/*
 * ObtainSharedMem -- obtain a shared memory
 * @param id -- segment id
 * @param shmaddr -- user specified address
 * @param shmflg -- flags to use for protection
 */
void* ObtainSharedMem(uint16_t id, void* shmaddr, int shmflg) {
	x64_cli();
	AuThread* thr = AuGetCurrentThread();
	if (!thr)
		return 0;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return NULL;
	}
	return AuSHMObtainMem(proc,id, shmaddr, shmflg);
}

/*
 * UnmapSharedMem -- unmap shared memory segment
 * @param key -- key to search
 */
void UnmapSharedMem(uint16_t key) {
	x64_cli();
	AuThread* thr = AuGetCurrentThread();
	if (!thr)  //some serious memory problem
		return;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return;
	}
	AuSHMUnmap(key, proc);
}



/*
 * GetProcessHeapMem -- get a memory from 
 * process heap
 */
uint64_t GetProcessHeapMem(size_t sz) {
	x64_cli();
	/* check if size is page aligned */
	if ((sz % PAGE_SIZE) != 0){
		AuTextOut("Returning error heap mem -> %d \r\n", sz);
		return -1;
		sz = PAGE_ALIGN(sz);
	}
	
	AuThread* thr = AuGetCurrentThread();
	if (!thr)
		return -1;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc){
		proc = AuProcessFindSubThread(thr);
		if (!proc) {
			return -1;
		}
	}
	
	uint64_t start_addr = (uint64_t)AuGetFreePage(false, (void*)proc->proc_mem_heap);

	for (int i = 0; i < sz / PAGE_SIZE; i++) {
		uint64_t phys = (uint64_t)AuPmmngrAlloc();
		if (!AuMapPage(phys, start_addr + static_cast<uint64_t>(i) * PAGE_SIZE, X86_64_PAGING_USER)) {
			SeTextOut("Failed to map %x \r\n", (start_addr + static_cast<uint64_t>(i) * PAGE_SIZE));
			AuPmmngrFree((void*)phys);
		}
	}
	
	proc->proc_mem_heap = start_addr;
	proc->proc_heapmem_len += sz;
	return start_addr;
}

/*
 * ProcessHeapUnmap -- unmaps previosly allocated
 * heap memory 
 * @param ptr -- Pointer to freeable address
 * @param sz -- size in bytes to unallocate
 */
int ProcessHeapUnmap(void* ptr, size_t sz) {
	x64_cli();

	/* check if size is page aligned */
	if ((sz % PAGE_SIZE) != 0) {
		AuTextOut("Returning error heap unmap -> %d \r\n", sz);
		return -1;
		sz = PAGE_ALIGN(sz);
	}


	AuThread* thr = AuGetCurrentThread();
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
		AuVPage* page_ = AuVmmngrGetPage(start_addr + static_cast<uint64_t>(i) * PAGE_SIZE, VIRT_GETPAGE_ONLY_RET, VIRT_GETPAGE_ONLY_RET);
		if (page_) {
			uint64_t phys_page = page_->bits.page << PAGE_SHIFT;
			if (phys_page){
				AuPmmngrFree((void*)phys_page);
				//page_->raw = 0;
				page_->bits.present = 0;
				page_->bits.page = 0;
				page_->bits.writable = 0;
			}
		}
	}

	flush_tlb((void*)start_addr);
	/*if (start_addr < proc->proc_mem_heap)*/
	proc->proc_mem_heap = start_addr;
	return 0;
}



 