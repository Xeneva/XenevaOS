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
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Hal/x86_64_hal.h>
#include <Hal/serial.h>
#include <Sync/spinlock.h>
#include <list.h>
#include <aucon.h>
#include <_null.h>
#include <string.h>

list_t *shm_list;
uint16_t shm_id;
Spinlock *shmlock;

/*
 * AuInitialiseSHMMan -- initialise shm manager
 */
void AuInitialiseSHMMan() {
	shm_list = initialize_list();
	shm_id = 1;
	shmlock = AuCreateSpinlock(false);
}

/*
 * AuSHMGetID -- allocate a new shared
 * memory id
 */
uint16_t AuSHMGetID() {
	uint16_t _id = shm_id;
	shm_id = shm_id + 1;
	return  _id;
}

/*
 * AuGetSHMSeg -- searches and return a
 * shm segment by its key
 * @param key -- key to search
 */
AuSHM * AuGetSHMSeg(uint16_t key) {
	for (int i = 0; i < shm_list->pointer; i++) {
		AuSHM* shm = (AuSHM*)list_get_at(shm_list, i);
		if (shm->key == key)
			return shm;
	}

	return NULL;
}

/*
* AuGetSHMSeg -- searches and return a
* shm segment by its key
* @param key -- key to search
*/
AuSHM * AuGetSHMByID(uint16_t id) {
	for (int i = 0; i < shm_list->pointer; i++) {
		AuSHM* shm = (AuSHM*)list_get_at(shm_list, i);
		if (shm->id == id)
			return shm;
	}

	return NULL;
}
/*
 * AuCreateSHM -- create a new shared memory segment or
 * returns previously allocated one
 * @param proc -- Creator process
 * @param key  --  unique key to use
 * @param sz   --  size in multiple of PAGE_SIZE
 * @param flags -- security flags
 */
int AuCreateSHM(AuProcess* proc, uint16_t key, size_t sz, uint8_t flags) {
	AuSHM* shm = NULL;
	AuAcquireSpinlock(shmlock);
	if (key > UINT16_MAX){
		AuReleaseSpinlock(shmlock);
		SeTextOut("[SHM] Creation failed, key exceeds limitation \r\n");
		return -1;
	}
	/*  search if it's already created */
	shm = AuGetSHMSeg(key);
	/* create a new*/
	if (!shm) {
		shm = (AuSHM*)kmalloc(sizeof(AuSHM));
		memset(shm, 0, sizeof(AuSHM));
		shm->id = AuSHMGetID();
		shm->key = key;
		shm->num_frames = (sz / 0x1000) + ((sz % 0x1000) ? 1 : 0);
		shm->link_count = 0;
		shm->frames = (uint64_t*)kmalloc(sizeof(uint64_t)* shm->num_frames);
		for (int i = 0; i < shm->num_frames; i++)  {
			shm->frames[i] = (uint64_t)AuPmmngrAlloc();
		}

		list_add(shm_list, shm);
	}

	if (!shm) {
		AuReleaseSpinlock(shmlock);
		return -1;
	}

	AuReleaseSpinlock(shmlock);
	return shm->id;
}

/*
 * AuSHMDelete -- removes a SHM Segment from system shm list
 * @param shm -- segment to delete
 */
void AuSHMDelete(AuSHM* shm) {
	if (!shm)
		return;
	if (shm->link_count > 0){
		shm->link_count--;
	}

	if (shm->link_count == 0){
		for (int i = 0; i < shm->num_frames; i++) {
			size_t phys = shm->frames[i];
			AuPmmngrFree((void*)phys);
		}
		
		for (int j = 0; j <= shm_list->pointer; j++) {
			AuSHM *shm_ = (AuSHM*)list_get_at(shm_list, j);
			if (shm_ == shm)
				list_remove(shm_list, j);
		}
		kfree(shm->frames);
		kfree(shm);
	}
}
/*
 * AuSHMProcBreak -- gets some available shm memory
 * and increase the break count
 * @param proc -- Process to look
 * @param num_frames -- number of frames to increase
 */
size_t AuSHMProcBreak(AuProcess* proc, size_t num_frames) {
	size_t start_addr = proc->shm_break;
	proc->shm_break = (proc->shm_break + num_frames * PAGE_SIZE);
	return start_addr;
}

/*
 * AuSHMProcSwap -- Swaps data between list entry
 */
void AuSHMProcSwap(dataentry* current, dataentry* index) {
	void* tmp = current->data;
	current->data = index->data;
	index->data = tmp;
}

/*
 * AuSHMProcOrderList -- orders current shared memory mappings
 * @param proc -- Pointer to process slot
 */
void AuSHMProcOrderList(AuProcess* proc) {
	dataentry* current = proc->shmmaps->entry_current;
	dataentry* index = NULL;
	for (int i = 0; i < proc->shmmaps->pointer; i++) {
		if (current == NULL)
			break;
		AuSHMMappings* mappsone = (AuSHMMappings*)current->data;
		index = current->next;
		for (int k = 0; k < proc->shmmaps->pointer - 1; k++) {
			if (index == NULL)
				break;
			AuSHMMappings* maptwo = (AuSHMMappings*)index->data;
			if (mappsone->start_addr > maptwo->start_addr) 
				AuSHMProcSwap(current, index);
			index = index->next;
		}
		current = current->next;
	}
}

/*
 * AuSHMObtainMem -- obtains a virtual memory from given
 * shm segment
 * @param proc -- Calling process
 * @param id -- shm segment id
 * @param shmaddr -- starting shared memory address to map
 * @parma shmflg -- flags
 */
void* AuSHMObtainMem(AuProcess* proc, uint16_t id, void* shmaddr, int shmflg) {
	AuAcquireSpinlock(shmlock);
	AuSHM* mem = NULL;

	/* search for shm memory segment */
	mem = AuGetSHMByID(id);

	if (!mem)
		return NULL;


	AuSHMMappings *mappings = (AuSHMMappings*)kmalloc(sizeof(AuSHMMappings));
	memset(mappings, 0, sizeof(AuSHMMappings));

	mem->link_count++;

	/* look for already available address space gap
	 * before increasing the process shm_break
	 */
	uint64_t last_addr = USER_SHARED_MEM_START;
	bool have_mappings = false;
	for (int i = 0; i < proc->shmmaps->pointer; i++) {
		AuSHMMappings *maps = (AuSHMMappings*)list_get_at(proc->shmmaps, i);
		if (!have_mappings)
			have_mappings = true;
		if (maps->start_addr > last_addr) {
			size_t gap = maps->start_addr - last_addr;
			if (gap >= static_cast<uint64_t>(mem->num_frames) * PAGE_SIZE){
				for (int j = 0; j < mem->num_frames; j++) {
					size_t phys = mem->frames[j];
					AuMapPage(phys, last_addr + static_cast<int64_t>(j) * PAGE_SIZE, X86_64_PAGING_USER);
				}
				mappings->start_addr = last_addr;
				mappings->length = mem->num_frames * PAGE_SIZE;
				mappings->shm = mem;

				/* Here we need some sorting algorithm to sort
				 * out mappings in ascending order, like Bubble-sort
				 * algorithm between nodes of mappings
				 */
				list_add(proc->shmmaps, mappings);
				AuReleaseSpinlock(shmlock);

#if 0
				/* just for debugging purpose */
				for (int i = 0; i < proc->shmmaps->pointer; i++) {
					AuSHMMappings* map = (AuSHMMappings*)list_get_at(proc->shmmaps, i);
					SeTextOut("M -> %x \r\n", map->start_addr);
				}
#endif

				/* Now order the list, in ascending order */
				AuSHMProcOrderList(proc);

#if 0
				/* just for debugging purpose after sorting 
				 * has been done 
				 */
				SeTextOut("After ordering \r\n");

				for (int i = 0; i < proc->shmmaps->pointer; i++) {
					AuSHMMappings* map = (AuSHMMappings*)list_get_at(proc->shmmaps, i);
					SeTextOut("M -> %x \r\n", map->start_addr);
				}
#endif

				return (void*)mappings->start_addr;
			}
		}
		last_addr = maps->start_addr + maps->length;
	}

	if (!have_mappings) {
		size_t start_addr = USER_SHARED_MEM_START;
		if (proc->shm_break > start_addr) {
			size_t gap = proc->shm_break - start_addr;
			if (gap >= mem->num_frames * PAGE_SIZE) {
				for (int j = 0; j < mem->num_frames; j++) {
					size_t phys = mem->frames[j];
					AuMapPage(phys, last_addr + static_cast<int64_t>(j) * PAGE_SIZE, X86_64_PAGING_USER);
				}
				mappings->start_addr = last_addr;
				mappings->length = mem->num_frames * PAGE_SIZE;
				mappings->shm = mem;
				list_add(proc->shmmaps, mappings);
				/* Now order the list, in ascending order */
				AuSHMProcOrderList(proc);
				AuReleaseSpinlock(shmlock);
				return (void*)mappings->start_addr;
			}
		}
	}

	/* finally, we need to increase the shm break */
	for (int i = 0; i < mem->num_frames; i++) {
		uint64_t phys_addr = mem->frames[i];
		uint64_t current_virt = AuSHMProcBreak(proc, 1);
		AuMapPage((uint64_t)phys_addr, current_virt, X86_64_PAGING_USER);
		if (mappings->start_addr == 0)
			mappings->start_addr = current_virt;
	}

	mappings->length = mem->num_frames * PAGE_SIZE;
	mappings->shm = mem;
	list_add(proc->shmmaps, mappings);
	/* Now order the list, in ascending order */
	AuSHMProcOrderList(proc);
	AuReleaseSpinlock(shmlock);
	return (void*)mappings->start_addr;
}

/*
 * AuSHMUnmap -- unmaps a shared memory segment
 * @param key -- key to search
 * @param proc -- process to look 
 */
void AuSHMUnmap(uint16_t key, AuProcess* proc) {
	AuAcquireSpinlock(shmlock);
	AuSHM* shm = AuGetSHMSeg(key);

	if (!shm) {
		AuReleaseSpinlock(shmlock);
		return;
	}
	
	AuSHMMappings* mapping = NULL;
	int index = 0;
	for (int i = 0; i < proc->shmmaps->pointer; i++) {
		AuSHMMappings* maps = (AuSHMMappings*)list_get_at(proc->shmmaps, i);
		if (maps->shm == shm){
			mapping = maps;
			for (int i = 0; i < mapping->length / PAGE_SIZE; i++) {
				AuVPage* vpage = AuVmmngrGetPage(mapping->start_addr + static_cast<int64_t>(i) * PAGE_SIZE, VIRT_GETPAGE_ONLY_RET, VIRT_GETPAGE_ONLY_RET);
				if (vpage) {
					vpage->bits.page = 0;
					vpage->bits.present = 0;
					flush_tlb((void*)(mapping->start_addr + static_cast<int64_t>(i) * PAGE_SIZE));
				}
			}
			SeTextOut("Closing index -> %d \r\n", i);
			list_remove(proc->shmmaps, i);
			kfree(mapping);
			break;
		}
	}


	AuSHMDelete(shm);
	SeTextOut("%s Unmapping shm ->%d count \r\n",proc->name, shm->link_count);
	AuReleaseSpinlock(shmlock);
}

/*
 * AuSHMUnmapAll -- unmaps all mappings for this
 * process
 * @param proc -- Pointer to process that needs
 * unmapping
 */
void AuSHMUnmapAll(AuProcess* proc) {
	AuAcquireSpinlock(shmlock);
	for (int i = 0; i < proc->shmmaps->pointer; i++) {
		AuSHMMappings* mapping = (AuSHMMappings*)list_remove(proc->shmmaps, i);
		for (int j = 0; j < mapping->length / PAGE_SIZE; j++) {
			AuVPage* vpage = AuVmmngrGetPage(mapping->start_addr + 
				static_cast<int64_t>(j) * PAGE_SIZE, VIRT_GETPAGE_ONLY_RET, VIRT_GETPAGE_ONLY_RET);
			vpage->bits.page = 0;
			vpage->bits.present = 0;
			flush_tlb((void*)(mapping->start_addr + 
				static_cast<int64_t>(j) * PAGE_SIZE));
		}
		AuSHMDelete(mapping->shm);
		SeTextOut("Unmapping shm -> %x \r\n", mapping->start_addr);
		kfree(mapping);
	}
	kfree(proc->shmmaps);
	AuReleaseSpinlock(shmlock);
}

