/**
* @file mmap.c
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

#include <Mm/mmap.h>
#include <Mm/vmarea.h>
#include <aucon.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <list.h>
#include <_null.h>
#include <Mm/vmmngr.h>
#include <Mm/mmfile.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/mmfile.h>
#include <Serv/sysserv.h>
#include <Hal/AA64/profile.h>

#define PROTECTION_FLAG_READONLY 1 << 0
#define PROTECTION_FLAG_WRITE	 1 << 1
#define PROTECTION_FLAG_NO_EXEC	 1 << 2
#define PROTECTION_FLAG_NO_CACHE 1 << 3

#define MEMMAP_FLAG_SHARED			  1 << 0
#define MEMMAP_FLAG_COW				  1 << 1
#define MEMMAP_FLAG_PRIVATE			  1 << 2
#define MEMMAP_FLAG_DISCARD_FILE_READ 1 << 3
#define MEMMAP_FLAG_FILE_BACK		  1 << 4

//#pragma pack(push,1)
/* shared memory map object */
typedef struct _sh_memap_object_ {
	char* objectName;
	uint8_t flags;
	uint8_t prot_flags;
	uint64_t beginPhysicalAddr;
	uint64_t len; //length in bytes
	uint16_t linkCount;
	uint16_t ownerProc;
} AuSharedMmapObject;
//#pragma pack(pop)

list_t* shmmaplist;

/**
 * @brief SharedMemMapListInitialise -- initialise
 * the shared memory map list
 */
void SharedMemMapListInitialise() {
	shmmaplist = initialize_list();
}

/**
 * @brief AuCreateSharedMmapObject -- create a global object
 */
AuSharedMmapObject* AuCreateSharedMmapObject(char* name) {
	if (!name)
		return NULL;
	AuSharedMmapObject* obj = (AuSharedMmapObject*)kmalloc(sizeof(AuSharedMmapObject));
	if (!obj)
		return NULL;
	memset(obj, 0, sizeof(AuSharedMmapObject));
	obj->objectName = (char*)kmalloc(strlen(name) + 1);
	if (!obj->objectName) {
		kfree(obj);
		return NULL;
	}
	strcpy(obj->objectName, name);
	obj->linkCount += 1;
	return obj;
}

void AuAddSharedMmapObject(AuSharedMmapObject* obj) {
	if (!obj)
		return;
	list_add(shmmaplist, obj);
}

void AuRemoveSharedMmapObject(AuSharedMmapObject* obj) {
	if (!obj)
		return;
	for (unsigned int i = 0; i < shmmaplist->pointer; i++) {
		AuSharedMmapObject* obj_ = (AuSharedMmapObject*)list_get_at(shmmaplist, i);
		if (obj_ == obj) {
			list_remove(shmmaplist, i);
			break;
		}
	}
	kfree(obj->objectName);
	kfree(obj);
}

AuSharedMmapObject* AuSharedMmapObjectFindByName(char* name) {
	if (!name)
		return NULL;
	for (unsigned int i = 0; i < shmmaplist->pointer; i++) {
		AuSharedMmapObject* obj_ = (AuSharedMmapObject*)list_get_at(shmmaplist, i);
		if (!obj_ || !obj_->objectName)
			continue;
		if (strcmp(obj_->objectName, name) == 0)
			return obj_;
	}
	return NULL;
}

/**
 * @brief CreateMemMapping -- Create a memory mapping of just memory, file or device
 * @param address -- address from where mapping start, if null, kernel will
 * find by its own
 * @param len -- length of the address
 * @param prot -- protection flags
 * @param flags -- flags
 * @param fd -- file descriptor, -1 for no file descriptor
 * @param offset -- byte offset for file and device
 * @return Pointer to starting 
 */
void* CreateMemMapping(void* address, size_t len, int prot, int flags, int fd, uint64_t offset) {
	if (!len)
		return 0;

	/* for now, memory mapping doesn't support lazy loading
	 * so everything works at pre-paging */
	//I'll get on it soon-ish, because the unikernel plan would need it anyway. --axiss
	AA64Thread* curr_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(curr_thr);
	if (!proc) {
		proc = AuProcessFindSubThread(curr_thr);
		if (!proc)
			return NULL;
	}

	AuVFSNode* file = NULL;
	AuVFSNode* fsys = NULL;
	uint64_t startingPhysAddr = NULL;
	bool shobj_len_increase = false;
	bool shobj_new_create = false;

	AuSharedMmapObject* shobj = NULL;
	if (fd != -1)
		file = proc->fds[fd];

	len = PAGE_ALIGN(len); //simply align the length
	/* Reserve the complete VA range atomically. AuGetFreePage only found one
	 * page, so two unikernel threads could select the same address before
	 * either installed a PTE. A monotonic per-process cursor also matches the
	 * existing contiguous proc_mmap_len cleanup contract. */
	size_t lookup_addr;
	if (!address) {
		uint64_t mmap_daif = AuAcquireSpinlockIrqSave(proc->mmap_lock);
		lookup_addr = proc->mmap_next;
		proc->mmap_next += len;
		proc->proc_mmap_len += len;
		AuReleaseSpinlockIrqRestore(proc->mmap_lock, mmap_daif);
	} else {
		lookup_addr = (size_t)address;
		uint64_t mmap_daif = AuAcquireSpinlockIrqSave(proc->mmap_lock);
		if (lookup_addr + len > proc->mmap_next)
			proc->mmap_next = lookup_addr + len;
		proc->proc_mmap_len += len;
		AuReleaseSpinlockIrqRestore(proc->mmap_lock, mmap_daif);
	}
	AuMMFileBack* fb = NULL;

	if (file && (file->flags & FS_FLAG_GENERAL)) {
		//UARTDebugOut("[CreateMemMapping] getting file \n");
		uint64_t file_block_start = 0;
		fsys = AuVFSFind("/");
		if (!fsys && fd != -1)
			return 0;
		//if (!(file->flags & FS_FLAG_DEVICE)) {
		//	file_block_start = AuVFSGetBlockFor(fsys, file, offset);
		//	file->current = file_block_start;
		//}

		fb = AuMmngrFileCacheLookup(file->filename);
		if (!fb) {
			UARTDebugOut("[mmap]: file %s is not in cache, we're caching it \r\n", file->filename);
			UARTDebugOut("[mmap]: file starting address : %x , curr : %x\r\n",
						 file->first_block,
						 file->current);
			fb = (AuMMFileBack*)kmalloc(sizeof(AuMMFileBack));
			if (fb) {
				memset(fb, 0, sizeof(AuMMFileBack));
				UARTDebugOut("mmap: file flag : %x \r\n", file->flags);
				fb->file = file;
				AuMmngrAddFileBack(fb);
			}
		}

		if (flags & MEMMAP_FLAG_SHARED) {
			shobj = AuSharedMmapObjectFindByName(file->filename);
			/* no shobject found with specified name, so we create
			 * new one */
			if (!shobj) {
				shobj = AuCreateSharedMmapObject(file->filename);
				if (shobj) {
					shobj->flags = flags;
					shobj->len = 0;
					shobj->prot_flags = prot;
					shobj->ownerProc = proc->proc_id;
					AuAddSharedMmapObject(shobj);
					shobj_new_create = true;
				}
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
				} else {
					/* we need to map that object here*/
					startingPhysAddr = shobj->beginPhysicalAddr;
				}
			}
		}
	}

	//UARTDebugOut("[mmap]: num pages reading : %d \r\n", (len / PAGE_SIZE));
	for (int i = 0; i < len / PAGE_SIZE; i++) {
		uint64_t phys = 0;
		if (startingPhysAddr && (flags & MEMMAP_FLAG_SHARED))
			phys = startingPhysAddr + i * PAGE_SIZE;
		else {
			if (fb) {
				if (AuMmngrFileCacheGetPhysicalBlock(fb, offset) == UINT64_MAX) {
					phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
				} else {
					if (flags & MEMMAP_FLAG_COW) {
						uint64_t datablk = AuMmngrFileCacheGetPhysicalBlock(fb, offset);
						phys =
							(uint64_t)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id);
						memcpy((void*)P2V(phys), (void*)P2V(datablk), PAGE_SIZE);

					} else {
						phys = AuMmngrFileCacheGetPhysicalBlock(fb, offset);
					}
				}

			} else {
				phys = (uint64_t)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id);
			}
		}

		if (startingPhysAddr == 0 && shobj_new_create)
			startingPhysAddr = phys;

		if (file && fb) {
			if (AuMmngrFileCacheGetPhysicalBlock(fb, offset) == UINT64_MAX) {
				uint64_t datablk = phys;
				if (flags & MEMMAP_FLAG_COW) {
					datablk = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
					if (!file->eof)
						AuVFSNodeReadBlock(fsys, file, (uint64_t*)P2V(datablk));
					memcpy((void*)P2V(phys), (void*)P2V(datablk), PAGE_SIZE);
				} else
					AuVFSNodeReadBlock(fsys, file, (uint64_t*)P2V(datablk));

				AuMMPageCache* cache = AuMmngrPageCacheCreate();
				cache->physicalPage = datablk;
				cache->pageIndex = fb->numPageIndex;
				cache->diskBlock = fsys->get_disk_block(fsys, file, file->current);
				AuMmngrFileBackAddPageCache(fb, cache);
				fb->numPageIndex++;

				if (file->eof) {
					//UARTDebugOut("File : %s , eof done : %d\r\n", file->filename, file->eof);
				}
			}

			if (file->eof)
				fb->readComplete = 1;
		}

		AuMapPage(phys, lookup_addr + i * PAGE_SIZE, PTE_NORMAL_MEM | PTE_AP_RW_USER);
		isb_flush();
		dsb_ish();

		AuVPage* page = AuVmmngrGetPage(lookup_addr + i * PAGE_SIZE, NULL, VIRT_GETPAGE_ONLY_RET);

		/* check for  protection flag */
		if (prot & PROTECTION_FLAG_READONLY) {
			page->bits.ap = 0b11; //EL1 + EL0 readonly
			isb_flush();
		}
		if (prot & PROTECTION_FLAG_NO_EXEC) {
			page->bits.uxn = 1;
			isb_flush();
		}
		if (prot & PROTECTION_FLAG_NO_CACHE) {
			page->bits.attrindex = PTE_DEVICE_MEM;
			isb_flush();
		}
		if (prot & PROTECTION_FLAG_READONLY && prot & PROTECTION_FLAG_WRITE) {
			page->bits.ap = 0b01; //RW for both EL0 EL1
			isb_flush();
		}
		/* the live entry changed: drop cached translations (all ASIDs) */
		tlb_flush(lookup_addr + i * PAGE_SIZE);

		/*if (flags & MEMMAP_FLAG_COW) 
			page->bits.cow = 1;*/

		if (flags & MEMMAP_FLAG_FILE_BACK) {
			//add to file mapping list
		}
	}

	/* shared bit should be handled differently */
	if (flags & MEMMAP_FLAG_SHARED && shobj) {
		if (shobj_new_create) {
			shobj->beginPhysicalAddr = startingPhysAddr;
			shobj->len = len;
			UARTDebugOut("SHOBJ newly created -> %s \r\n", shobj->objectName);
		}
		if (shobj_len_increase)
			shobj->len += len;
	}

	if (file)
		file->flags |= FS_FLAG_CACHED;

	return (void*)lookup_addr;
}

/**
 * @brief MemMapDirty -- dirty update previously allocated memory map
 * @param startingVaddr -- starting address
 * @param len -- length in bytes
 * @param flags -- memory map flags
 * @param prot -- protection flags
 */
void MemMapDirty(void* startingVaddr, size_t len, int flags, int prot) {
	AA64Thread* curr_thr = AuGetCurrentThread();
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
			AuVPage* page = AuVmmngrGetPage(startAddr + i * PAGE_SIZE, NULL, VIRT_GETPAGE_ONLY_RET);
			UARTDebugOut("PAGE got : %x \n", page);
			/* check for  protection flag */
			if (prot & PROTECTION_FLAG_READONLY) {
				page->bits.ap = 0b11;
				UARTDebugOut("Memmap dirty setting : readonly \n");
				isb_flush();
			}
			if (prot & PROTECTION_FLAG_NO_EXEC) {
				page->bits.uxn = 1;
				UARTDebugOut("Memmap dirty setting : no exec \n");
				isb_flush();
			}
			if (prot & PROTECTION_FLAG_NO_CACHE) {
				page->bits.attrindex = PTE_DEVICE_MEM;
				UARTDebugOut("Memmap dirty setting : no cache\n");
				isb_flush();
			}
			if (prot & PROTECTION_FLAG_READONLY && prot & PROTECTION_FLAG_WRITE) {
				page->bits.ap = 0b01;
				UARTDebugOut("Memmap dirty setting : write \n");
				isb_flush();
			}
			/* the live entry changed: drop cached translations (all ASIDs) */
			tlb_flush(startAddr + i * PAGE_SIZE);

			/*if (flags & MEMMAP_FLAG_COW)
				page->bits.cow = 1;*/
		}
	}
	if (!AuVMAreaGet(proc, (size_t)startingVaddr)) {
		AuVMArea* area =
			AuVMAreaCreate((size_t)startingVaddr, ((uint64_t)startingVaddr + len), 0, len, VM_EXEC);
		if (area) {
			list_add(proc->vmareas, area);
		}
	}
}

/**
 * @brief UnmapMemMapping -- unmaps a memory mapping
 * @param address -- address from where mapping starts
 * @param len -- length of the mapping
 */
void UnmapMemMapping(void* address, size_t len) {
	if (!len)
		return;

	//SeTextOut("MemUnmap len -> %d \r\n", len);
	len = PAGE_ALIGN(len); //simply align the length
	//SeTextOut("Mem Unmap len aligned -> %d \r\n", len);
	uint64_t addr = (uint64_t)address;
	for (int i = 0; i < len / PAGE_SIZE; i++) {
		AuVPage* page =
			AuVmmngrGetPage(addr + i * PAGE_SIZE, VIRT_GETPAGE_ONLY_RET, VIRT_GETPAGE_ONLY_RET);
		if (page) {
			uint64_t phys = page->bits.page << PAGE_SHIFT;
			/* unmap and invalidate first, free after: another core may
			 * run this address space and reach the page until then */
			page->bits.page = 0;
			isb_flush();
			page->bits.present = 0;
			isb_flush();
			tlb_flush_vmalle1is();
			dsb_ish();
			isb_flush();
			if (phys) {
				AuPmmngrReleasePage((uint64_t)phys);
			}
		}
	}
}
