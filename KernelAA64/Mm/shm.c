/**
* @file shm.c
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

#include <Mm/shm.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <list.h>
#include <Drivers/uart.h>
#include <_null.h>
#include <string.h>
#include <stdint.h>

static list_t* shm_list;
static uint16_t shm_id;

/**
 * @brief AuInitialiseSHMMan -- initialise shm manager
 */
void AuInitialiseSHMMan(void) {
	shm_list = initialize_list();
	shm_id = 1;
}

/**
 * @brief AuSHMGetID -- allocate a new shared
 * memory id
 */
static uint16_t AuSHMGetID(void) {
	return shm_id++;
}

/**
 * @brief AuGetSHMSeg -- searches and return a
 * shm segment by its key
 * @param key -- key to search
 */
static AuSHM* AuGetSHMSeg(uint16_t key) {
	for (unsigned int i = 0; i < shm_list->pointer; i++) {
		AuSHM* shm = (AuSHM*)list_get_at(shm_list, i);
		if (shm->key == key)
			return shm;
	}

	return NULL;
}

/**
* @brief AuGetSHMByID -- find a shared memory segment by ID
* @param id -- segment ID to search
* @return Pointer to shm on success, NULL on failure
*/
AuSHM* AuGetSHMByID(uint16_t id) {
	for (unsigned int i = 0; i < shm_list->pointer; i++) {
		AuSHM* shm = (AuSHM*)list_get_at(shm_list, i);
		if (shm->id == id)
			return shm;
	}

	return NULL;
}
/**
 * @brief AuCreateSHM -- create a new shared memory segment or
 * returns previously allocated one
 * @param proc -- Creator process
 * @param key  --  unique key to use
 * @param sz   -- size in bytes, rounded up to whole pages
 * @param flags -- reserved
 * @return id of newly created SHM, -1 on failure
 */
int AuCreateSHM(AuProcess* proc, uint16_t key, size_t sz, uint8_t flags) {
	(void)flags;
	AuSHM* shm = AuGetSHMSeg(key);
	if (!shm) {
		if (proc) {
			UARTDebugOut("Creating shm for proc : %s \r\n", proc->name);
		}
		shm = (AuSHM*)kmalloc(sizeof(AuSHM));
		if (!shm) {
			UARTDebugOut("AuCreateSHM: failed shm : %x, proc: %s \r\n", shm, proc->name);
			return -1;
		}
		memset(shm, 0, sizeof(AuSHM));
		shm->id = AuSHMGetID();
		shm->key = key;
		shm->num_frames = sz / PAGE_SIZE + (sz % PAGE_SIZE != 0);
		shm->frames = (uint64_t*)kmalloc(sizeof(uint64_t) * shm->num_frames);
		if (!shm->frames) {
			UARTDebugOut("AuCreateSHM: failed to allocate frames \r\n");
			kfree(shm);
			return -1;
		}
		for (size_t i = 0; i < shm->num_frames; i++) {
			shm->frames[i] = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
			if (shm->frames[i] == PMM_INVALID_PHYS) {
				UARTDebugOut("AuCreateSHM: PMM_INVALID_PHYS \r\n");
				while (i > 0) {
					AuPmmngrReleasePage(shm->frames[--i]);
				}
				kfree(shm->frames);
				kfree(shm);
				return -1;
			}
		}

		list_add(shm_list, shm);
	}

	return shm->id;
}

/**
 * @brief AuSHMRelease -- drop a mapping or kernel owner's reference
 * @param shm -- segment to release
 */
void AuSHMRelease(AuSHM* shm) {
	if (!shm)
		return;
	if (shm->link_count > 0)
		shm->link_count--;

	if (shm->link_count != 0)
		return;

	for (size_t i = 0; i < shm->num_frames; i++)
		AuPmmngrReleasePage(shm->frames[i]);

	for (unsigned int j = 0; j < shm_list->pointer; j++) {
		AuSHM* shm_ = (AuSHM*)list_get_at(shm_list, j);
		if (shm_ == shm) {
			list_remove(shm_list, j);
			break;
		}
	}
	kfree(shm->frames);
	kfree(shm);
}

void AuSHMRetain(AuSHM* shm) {
	if (shm)
		shm->link_count++;
}
/*
 * AuSHMProcOrderList -- orders current shared memory mappings
 * @param proc -- Pointer to process slot
 */
static void AuSHMProcOrderList(AuProcess* proc) {
	dataentry* current = proc->shmmaps->entry_current;
	for (; current; current = current->next) {
		for (dataentry* idx = current->next; idx; idx = idx->next) {
			if (((AuSHMMappings*)current->data)->start_addr >
				((AuSHMMappings*)idx->data)->start_addr) {
				void* tmp = current->data;
				current->data = idx->data;
				idx->data = tmp;
			}
		}
	}
}

static uint64_t AuSHMFindAddress(AuProcess* proc, size_t length) {
	uint64_t last_addr = USER_SHARED_MEM_START;
	for (dataentry* entry = proc->shmmaps->entry_current; entry; entry = entry->next) {
		AuSHMMappings* mapping = (AuSHMMappings*)entry->data;
		if (mapping->start_addr > last_addr && mapping->start_addr - last_addr >= length)
			return last_addr;
		last_addr = mapping->start_addr + mapping->length;
	}
	if (!proc->shmmaps->pointer && proc->shm_break > last_addr &&
		proc->shm_break - last_addr >= length)
		return last_addr;

	uint64_t start_addr = proc->shm_break;
	proc->shm_break += length;
	return start_addr;
}

/**
 * @brief AuSHMObtainMem -- obtains a virtual memory from given
 * shm segment
 * @param proc -- Calling process
 * @param id -- shm segment id
 * @param shmaddr -- reserved; the kernel chooses the mapping address
 * @param shmflg -- reserved
 * @return starting address of this shm on success, NULL on failure
 */
void* AuSHMObtainMem(AuProcess* proc, uint16_t id, void* shmaddr, int shmflg) {
	(void)shmaddr;
	(void)shmflg;
	AuSHM* mem = AuGetSHMByID(id);
	if (!mem)
		return NULL;

	AuSHMMappings* mapping = (AuSHMMappings*)kmalloc(sizeof(AuSHMMappings));
	if (!mapping) {
		UARTDebugOut("AuSHMObtainMem : failed to allocate new mapping :%x \r\n", mapping);
		return NULL;
	}
	mapping->length = mem->num_frames * PAGE_SIZE;
	mapping->start_addr = AuSHMFindAddress(proc, mapping->length);
	mapping->shm = mem;
	AuSHMRetain(mem);

	/* The segment owns the frames; link_count controls their lifetime.
	 * Every mapping uses Normal memory so unaligned user accesses work. */
	for (size_t i = 0; i < mem->num_frames; i++) {
		AuMapPage(
			mem->frames[i], mapping->start_addr + i * PAGE_SIZE, PTE_NORMAL_MEM | PTE_AP_RW_USER);
	}
	list_add(proc->shmmaps, mapping);
	AuSHMProcOrderList(proc);
	return (void*)mapping->start_addr;
}

static void AuSHMDetachMapping(AuProcess* proc, AuSHMMappings* mapping) {
	if (!mapping)
		return;
	AuFreePagesEx(proc->cr3, mapping->start_addr, false, mapping->length);
	AuSHMRelease(mapping->shm);
	UARTDebugOut("Unmapping shm -> %x \r\n", mapping->start_addr);
	kfree(mapping);
}

/**
 * @brief AuSHMUnmap -- unmaps a shared memory segment
 * @param key -- key to search
 * @param proc -- process to look
 */
void AuSHMUnmap(uint16_t key, AuProcess* proc) {
	if (!proc || !proc->shmmaps)
		return;
	AuSHM* shm = AuGetSHMSeg(key);
	if (!shm)
		return;

	for (unsigned int i = 0; i < proc->shmmaps->pointer; i++) {
		AuSHMMappings* maps = (AuSHMMappings*)list_get_at(proc->shmmaps, i);
		if (maps && maps->shm == shm) {
			AuSHMDetachMapping(proc, (AuSHMMappings*)list_remove(proc->shmmaps, i));
			return;
		}
	}
}

/**
 * @brief AuSHMUnmapAll -- unmaps all mappings for this
 * process
 * @param proc -- Pointer to process that needs
 * unmapping
 */
void AuSHMUnmapAll(AuProcess* proc) {
	if (!proc->shmmaps)
		return;
	while (proc->shmmaps->pointer)
		AuSHMDetachMapping(proc, (AuSHMMappings*)list_remove(proc->shmmaps, 0));
	kfree(proc->shmmaps);
	proc->shmmaps = NULL;
}
