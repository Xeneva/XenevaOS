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

#include <Mm/mmap.h>
#include <Mm/vmarea.h>
#include <aucon.h>
#include <Hal/serial.h>
#include <Hal/x86_64_lowlevel.h>
#include <_null.h>


#define PROTECTION_FLAG_READONLY  1<<0
#define PROTECTION_FLAG_WRITE 1<<1
#define PROTECTION_FLAG_NO_EXEC  1<<2
#define PROTECTION_FLAG_NO_CACHE 1<<3

#define MEMMAP_FLAG_SHARED 1<<0
#define MEMMAP_FLAG_COW  1<<1
#define MEMMAP_FLAG_PRIVATE  1<<2
#define MEMMAP_FLAG_DISCARD_FILE_READ 1<<3


#pragma pack(push,1)
/* shared memory map object */
typedef struct _sh_memap_object_ {
	char* objectName;
	uint8_t flags;
	uint8_t prot_flags;
	uint64_t beginPhysicalAddr;
	uint64_t len;  //length in bytes
	uint16_t linkCount;
	uint16_t ownerProc;
}AuSharedMmapObject;
#pragma pack(pop)

list_t* shmmaplist;

/*
 * SharedMemMapListInitialise -- initialise
 * the shared memory map list
 */
void SharedMemMapListInitialise() {
	shmmaplist = initialize_list();
}

/*
 * AuCreateSharedMmapObject -- create a global object
 */
AuSharedMmapObject* AuCreateSharedMmapObject(char* name) {
	AuSharedMmapObject* obj = (AuSharedMmapObject*)kmalloc(sizeof(AuSharedMmapObject));
	memset(obj, 0, sizeof(AuSharedMmapObject));
	obj->objectName = (char*)kmalloc(strlen(name));
	strcpy(obj->objectName, name);
	obj->linkCount += 1;
	return obj;
}

void AuAddSharedMmapObject(AuSharedMmapObject* obj) {
	list_add(shmmaplist, obj);
}

void AuRemoveSharedMmapObject(AuSharedMmapObject* obj) {
	for (int i = 0; i < shmmaplist->pointer; i++){
		AuSharedMmapObject* obj_ = (AuSharedMmapObject*)list_get_at(shmmaplist, i);
		if (obj_ == obj)
			list_remove(shmmaplist, i);
	}
	kfree(obj->objectName);
	kfree(obj);
}

AuSharedMmapObject* AuSharedMmapObjectFindByName(char* name) {
	for (int i = 0; i < shmmaplist->pointer; i++){
		AuSharedMmapObject* obj_ = (AuSharedMmapObject*)list_get_at(shmmaplist, i);
		if (strcmp(obj_->objectName, name) == 0)
			return obj_;
	}
	return NULL;
}

/*
 * CreateMemMapping -- Create a memory mapping of just memory, file or device
 * @param address -- address from where mapping start, if null, kernel will
 * find by its own
 * @param len -- length of the address
 * @param prot -- protection flags
 * @param flags -- flags 
 * @param fd -- file descriptor, -1 for no file descriptor
 * @param offset -- byte offset for file and device
 */
void* CreateMemMapping(void* address, size_t len, int prot, int flags, int fd,
	uint64_t offset) {
	x64_cli();

	if (!len)
		return 0;

	/* for now, memory mapping doesn't support lazy loading 
	 * so everything works at pre-paging */

	AuThread* curr_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(curr_thr);
	if (!proc) {
		proc = AuProcessFindSubThread(curr_thr);
		if (!proc)
			return NULL;
	}
	AuVFSNode *file = NULL;
	AuVFSNode* fsys = NULL;
	uint64_t startingPhysAddr = NULL;
	bool shobj_len_increase = false;
	bool shobj_new_create = false;

	AuSharedMmapObject* shobj = NULL;
	if (fd != -1) 
		file = proc->fds[fd];
	
	size_t lookup_addr = NULL;
	if (!address)
		lookup_addr = (size_t)AuGetFreePage(true, (void*)PROCESS_MMAP_ADDRESS);
	else
		lookup_addr = (size_t)address;

	len = PAGE_ALIGN(len); //simply align the length


	if (file) {
		uint64_t file_block_start = 0;
		fsys = AuVFSFind("/");
		if (!fsys && fd != -1)
			return 0;
		if (!(file->flags & FS_FLAG_DEVICE)) {
			file_block_start = AuVFSGetBlockFor(fsys, file, offset);
			file->current = file_block_start;
		}

		if (flags & MEMMAP_FLAG_SHARED) {
			shobj = AuSharedMmapObjectFindByName(file->filename);
			/* no shobject found with specified name, so we create
			 * new one */
			if (!shobj) {
				shobj = AuCreateSharedMmapObject(file->filename);
				shobj->flags = flags;
				shobj->len = 0;
				shobj->prot_flags = prot;
				shobj->ownerProc = proc->proc_id;
				AuAddSharedMmapObject(shobj);
				shobj_new_create = true;
			}
			/* okay, already there is an shared object with this
			 * particular name, so check further
			 */
			if (shobj) {
				/* firstly, check if the owner process is accessing,
				 * then simply we work on the shared object
				 */
				if (shobj->ownerProc == proc->proc_id) {
					shobj_len_increase = true;
				}
				else {
					/* we need to map that object here*/
					startingPhysAddr = shobj->beginPhysicalAddr;
				}
			}
		}
	}

	

	for (int i = 0; i < len / PAGE_SIZE; i++) {
		uint64_t phys = 0;
		if (startingPhysAddr && (flags & MEMMAP_FLAG_SHARED))
			phys = startingPhysAddr + static_cast<int64_t>(i) * PAGE_SIZE;
		else
			phys = (uint64_t)AuPmmngrAlloc();

		if (startingPhysAddr == 0 && shobj_new_create)
			startingPhysAddr = phys;

		if (file && !(flags & MEMMAP_FLAG_DISCARD_FILE_READ)){
			SeTextOut("mmap reading file \r\n");
			AuVFSNodeReadBlock(fsys, file, (uint64_t*)phys);
		}
		AuMapPage(phys, lookup_addr + static_cast<int64_t>(i) * PAGE_SIZE, X86_64_PAGING_USER);
		AuVPage *page = AuVmmngrGetPage(lookup_addr + static_cast<int64_t>(i) * PAGE_SIZE, NULL, VIRT_GETPAGE_ONLY_RET);

		/* check for  protection flag */
		if (prot & PROTECTION_FLAG_READONLY)
			page->bits.writable = 0;
		if (prot & PROTECTION_FLAG_NO_EXEC)
			page->bits.nx = 1;
		if (prot & PROTECTION_FLAG_NO_CACHE)
			page->bits.cache_disable = 1;
		if (prot & PROTECTION_FLAG_READONLY && prot & PROTECTION_FLAG_WRITE)
			page->bits.writable = 0;

		if (flags & MEMMAP_FLAG_COW)
			page->bits.cow = 1;
	}

	/* shared bit should be handled differently */
	if (flags & MEMMAP_FLAG_SHARED && shobj) {
		if (shobj_new_create){
			shobj->beginPhysicalAddr = startingPhysAddr;
			shobj->len = len;
			SeTextOut("SHOBJ newly created -> %s \r\n", shobj->objectName);
		}
		if (shobj_len_increase)
			shobj->len += len;
	}

	proc->proc_mmap_len += len;
	return (void*)lookup_addr;
}

/*
 * MemMapDirty -- dirty update previously allocated memory map
 * @param startingVaddr -- starting address
 * @param len -- length in bytes
 * @param flags -- memory map flags
 * @param prot -- protection flags
 */
void MemMapDirty(void* startingVaddr, size_t len, int flags, int prot) {
	x64_cli();

	AuThread* curr_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(curr_thr);
	if (!proc) {
		proc = AuProcessFindSubThread(curr_thr);
		if (!proc)
			return;
	}

	len = PAGE_ALIGN(len); //simply align the length
	uint64_t startAddr = (uint64_t)startingVaddr;
	if (prot != 0) {
		for (int i = 0; i < len / PAGE_SIZE; i++) {
			AuVPage* page = AuVmmngrGetPage(startAddr + static_cast<int64_t>(i) * PAGE_SIZE, NULL, VIRT_GETPAGE_ONLY_RET);

			/* check for  protection flag */
			if (prot & PROTECTION_FLAG_READONLY)
				page->bits.writable = 0;
			if (prot & PROTECTION_FLAG_NO_EXEC)
				page->bits.nx = 1;
			if (prot & PROTECTION_FLAG_NO_CACHE)
				page->bits.cache_disable = 1;
			if (prot & PROTECTION_FLAG_READONLY && prot & PROTECTION_FLAG_WRITE)
				page->bits.writable = 0;

			if (flags & MEMMAP_FLAG_COW)
				page->bits.cow = 1;
		}
	}
	if (!AuVMAreaGet(proc, (size_t)startingVaddr)) {
		AuVMArea* area = AuVMAreaCreate((size_t)startingVaddr, ((uint64_t)startingVaddr + len), 0, len, VM_EXEC);
		list_add(proc->vmareas, area);
		SeTextOut("VMArea added for %s : %x-%x \r\n", proc->name, area->start, area->end);
	}
}


/*
 * UnmapMemMapping -- unmaps a memory mapping
 * @param address -- address from where mapping starts
 * @param len -- length of the mapping
 */
void UnmapMemMapping(void* address, size_t len) {
	x64_cli();
	if (!len)
		return;

	//SeTextOut("MemUnmap len -> %d \r\n", len);
	len = PAGE_ALIGN(len); //simply align the length
	//SeTextOut("Mem Unmap len aligned -> %d \r\n", len);
	uint64_t addr = (uint64_t)address;
	for (int i = 0; i < len / PAGE_SIZE; i++) {
		AuVPage* page = AuVmmngrGetPage(addr + static_cast<int64_t>(i) * PAGE_SIZE, VIRT_GETPAGE_ONLY_RET, VIRT_GETPAGE_ONLY_RET);
		if (page) {
			uint64_t phys = page->bits.page << PAGE_SHIFT;
			if (phys){
				AuPmmngrFree((void*)phys);
			}
			page->bits.page = 0;
			page->bits.present = 0;
		}
	}

}