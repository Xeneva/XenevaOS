/**
* @file loader.c
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

#include <loader.h>
#include <Drivers/uart.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <Mm/mmfile.h>
#include <aucon.h>
#include <Fs/vfs.h>
#include <pe.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <ftmngr.h>
#include <Mm/mmfile.h>
#include <_null.h>
#include <Hal/AA64/profile.h>
#include <timer.h>
#include <Fs/vfsmod.h>

#define LOADER_SCRATCH_VIRT 0xFFFFC00000700000

uint64_t* _ldr_scratchBuffer;
uint64_t physFrames[64];

/* push item on the stack */ //
#define PUSH(stack, type, item)                                                                    \
	do {                                                                                           \
		stack -= sizeof(type);                                                                     \
		while (stack & (sizeof(type) - 1))                                                         \
			stack--;                                                                               \
		*((type*)(stack)) = (item);                                                                \
	} while (0);

#define PUSHALIGN(stack, align)                                                                    \
	do {                                                                                           \
		stack &= ~((align) - 1);                                                                   \
	} while (0)

void testFunc(uint64_t x0, uint64_t x1) {
	UARTDebugOut("x0: %x x1 : %x \r\n", x0, x1);
}

/**
* @brief AuProcessEntUser -- main kernel thread call
* in order to enter user for processes
* @param rcx -- user entry structure
*/
void AuProcessEntUser(uint64_t rcx) {
	mask_irqs();
	AA64Thread* t = AuGetCurrentThread();
	t->start_time_us = AuGetCurrentUS();
	AuUserEntry* uentry = t->uentry;
	uentry->rsp -= 32;
	PUSHALIGN(uentry->rsp, 16);
	t->first_run = 1;

	/* musl reads argc from [sp]. The PE crt0 pair stays on the other path. */
	{
		AuProcess* linux_proc = (AuProcess*)t->procSlot;
		if (!linux_proc)
			linux_proc = AuProcessFindThread(t);
		if (linux_proc && (linux_proc->type_flags & PROCESS_TYPE_LINUX)) {
			AuLinuxBuildUserStack(uentry);
			if (uentry->argvs) {
				for (int i = 0; i < uentry->num_args; i++)
					kfree(uentry->argvs[i]);
				kfree(uentry->argvs);
				uentry->argvs = 0;
			}
			aa64_enter_user(uentry->rsp, uentry->entrypoint, 0);
			while (1) {}
		}
	}

	/*
	 * ARM64 ABI: argc and argv must be placed on the user stack so that
	 * crt0arm64.s can pop them via: ldp x0, x1, [sp], #16
	 *
	 * Step 1: Push each argument string onto the user stack (grow downward).
	 *         Record the resulting user-space SP as argv[i] in the
	 *         user-mapped argvaddr page.
	 * Step 2: Push argc and argv_ptr (argvaddr) onto the user stack.
	 * Step 3: Free kernel-side copies AFTER we have finished reading them.
	 */

	/* Step 1 — push strings and record user-space pointers */
	char** argvs = (char**)uentry->argvkernel; /* kernel-accessible VA of argv[] page */
	for (int i = uentry->num_args - 1; i >= 0; i--) {
		char* str = uentry->argvs[i];
		int slen = strlen(str);
		/* push null terminator first, then chars in reverse so string
		 * reads forward at lower address after push */
		PUSH(uentry->rsp, char, '\0');
		for (int j = slen - 1; j >= 0; j--) {
			PUSH(uentry->rsp, char, str[j]);
		}
		/* record the user-space address of this string */
		argvs[i] = (char*)uentry->rsp;
	}

	/* Step 2 — align, then push argv_ptr and argc */
	PUSHALIGN(uentry->rsp, 16);
	PUSH(uentry->rsp, size_t, (size_t)uentry->argvaddr); /* x1 = argv */
	PUSH(uentry->rsp, size_t, (size_t)uentry->num_args); /* x0 = argc */
	PUSHALIGN(uentry->rsp, 16);

	/* Step 3 — free kernel-side argv copies (no longer needed) */
	if (uentry->argvs) {
		for (int i = 0; i < uentry->num_args; i++) {
			kfree(uentry->argvs[i]);
		}
		kfree(uentry->argvs);
	}
	uentry->argvs = 0;

	/*UARTDebugOut("[loader]: entering user: sp=%x entry=%x argc=%d argv=%x\r\n",
				 uentry->rsp,
				 uentry->entrypoint,
				 uentry->num_args,
				 uentry->argvaddr);*/

	uint64_t* check_sp = (uint64_t*)uentry->rsp;
	//UARTDebugOut("[loader]: stack check [0]: %x, [1]: %x\r\n", check_sp[0], check_sp[1]);

	aa64_enter_user(uentry->rsp, uentry->entrypoint, 0);
	while (1) {}
}

/**
 * AuProcessEntSubThread -- Sub process thread entry helper
 */
void AuProcessEntSubThread(uint64_t rcx) {
	(void)rcx;
	mask_irqs();
	AA64Thread* t = AuGetCurrentThread();
	t->start_time_us = AuGetCurrentUS();
	AuUserEntry* uentry = t->uentry;
	aa64_enter_user(uentry->rsp, uentry->entrypoint, uentry->argvaddr);
}

/**
 * @brief AuLoaderMapExecFromCache -- file cache lookup and map directly from the 
 * file cache page collections, this function called from inside AuLoadExecToProcess
 * @param proc -- Pointer to process
 * @param fb -- Pointer to file back struct
 * @param nt -- Pointer to Image nt header
 * @param secthdr -- Pointer to Image sect header
 * @param _image_base_ -- Image base address
 */
void AuLoaderMapExecFromCache(AuProcess* proc,
							  AuMMFileBack* fb,
							  PIMAGE_NT_HEADERS nt,
							  PSECTION_HEADER secthdr,
							  uint64_t _image_base_) {
	UARTDebugOut("AuLoaderMapExecFromCache : %x, %x %xr\n", proc, fb, fb->file);
	if (fb->file)
		UARTDebugOut("Name : %s \r\n", fb->file->filename);
	for (size_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
		size_t load_addr = _image_base_ + secthdr[i].VirtualAddress;
		size_t virtSize = secthdr[i].VirtualSize;
		size_t rawSize = secthdr[i].SizeOfRawData;
		size_t fileOffset = secthdr[i].PointerToRawData;

		/* Ensure all pages for this section's virtual address range are mapped */
		size_t end_addr = load_addr + virtSize;
		for (size_t v_page = (load_addr & ~0xFFFULL); v_page < end_addr; v_page += PAGE_SIZE) {
			void* phys = AuGetPhysicalAddressEx(proc->cr3, v_page);
			if (!phys) {
				phys = (void*)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id);
				memset((void*)P2V((size_t)phys), 0, PAGE_SIZE);
				AuMapPageEx(proc->cr3,
							(uint64_t)phys,
							v_page,
							PTE_USER_EXECUTABLE | PTE_AP_RW_USER | PTE_NORMAL_MEM);
			}
			if (((v_page >> 12) & 15) == 0)
				AuSchedYieldMs(1);
		}

		/* Section bytes live in the file page cache. Scratch is only 1 MiB,
		 * so a file offset past that window is not a valid kernel address. */
		size_t bytes_to_copy = rawSize;
		size_t current_vaddr = load_addr;
		size_t current_file_offset = fileOffset;
		while (bytes_to_copy > 0) {
			size_t page_offset = current_vaddr & 0xFFF;
			size_t bytes_in_this_page = PAGE_SIZE - page_offset;
			if (bytes_in_this_page > bytes_to_copy) {
				bytes_in_this_page = bytes_to_copy;
			}

			void* phys = AuGetPhysicalAddressEx(proc->cr3, current_vaddr);
			if (phys) {
				void* dest = (void*)(P2V((size_t)phys) + page_offset);
				size_t left = bytes_in_this_page;
				size_t dst_off = 0;
				size_t src_off = current_file_offset;
				while (left > 0) {
					size_t src_page = src_off / PAGE_SIZE;
					size_t src_poff = src_off % PAGE_SIZE;
					size_t chunk = PAGE_SIZE - src_poff;
					if (chunk > left)
						chunk = left;
					AuMMPageCache* cache = AuMmngrFileCacheGetByIndex(fb, src_page);
					if (!cache) {
						UARTDebugOut("[loader]: missing page cache at %x\r\n", src_off);
						memset((uint8_t*)dest + dst_off, 0, chunk);
					} else {
						memcpy((uint8_t*)dest + dst_off,
							   (uint8_t*)P2V(cache->physicalPage) + src_poff,
							   chunk);
					}
					dst_off += chunk;
					src_off += chunk;
					left -= chunk;
				}
			}

			current_vaddr += bytes_in_this_page;
			current_file_offset += bytes_in_this_page;
			bytes_to_copy -= bytes_in_this_page;
			if (((current_vaddr >> 12) & 15) == 0)
				AuSchedYieldMs(1);
		}
	}
}

extern bool setStk();

/**
 * @brief AuLoadExecToProcess -- loads an executable to the
 * process
 * @param proc -- pointer to process data structure
 * @param filename -- executable file name
 * @param argc -- number of arguments
 * @param argv -- array of argument in strings
 * @return 0 on success -1 on failure
 */
int AuLoadExecToProcess(AuProcess* proc, char* filename, int argc, char** argv) {
	/* verify the filename, it can only be '.exe' file no '.dll' or other */
#ifdef __KERNEL_PROFILER_ON__
	PROFILE_START("AuLoadExecToProcess");
#endif
	char* v_ = strchr(filename, '.');
	if (v_)
		v_++;
	if (strcmp(v_, "exe") != 0 && strcmp(v_, "elf") != 0) {
		UARTDebugOut("[aurora]: non-executable process \r\n");
		return -1;
	}

	AuMMFileBack* fb = AuMmngrFileCacheLookup(filename);

	AuVFSNode* fsys = AuVFSFind(filename);
	AuVFSNode* file = NULL;
	if (fb) {
		file = fb->file;
	} else {
		//	UARTDebugOut("[loader.c]: file : %s was not in cache adding it \r\n", filename);
		file = AuVFSOpen(filename);
		if (!file) {
			UARTDebugOut("[loader.c]: Failed to open file %s\r\n", filename);
			return -1;
		}
		fb = (AuMMFileBack*)kmalloc(sizeof(AuMMFileBack));
		memset(fb, 0, sizeof(AuMMFileBack));
		file->flags |= FS_FLAG_CACHED;
		fb->file = file;
		fb->pageCache = NULL;
		fb->pageCacheLast = NULL;
		fb->readComplete = false;
		AuMmngrAddFileBack(fb);
		//aa64_data_cache_clean_range(fb, sizeof(AuMMFileBack));
	}
	if (!file) {
		UARTDebugOut("No File found -> %s \r\n", filename);
		return -1;
	}

	if (file->flags & FS_FLAG_DIRECTORY)
		return -1;

	if (!AuVFSCheckAccess(&proc->creds, file, 1 /* execute bit */)) {
		UARTDebugOut("[loader]: %s : permission denied, file mode incorrect \r\n", filename);
		return -1;
	}

	size_t scratch_bytes = 0;

	if (file->eof == 1 && fb->readComplete == 1)
		file->eof = 0;

	if (fb->readComplete == false) {
		size_t file_offset = 0;
		size_t yield_mark = 0;
		int elf_file = 0;
		const size_t scratch_limit = 1024 * 1024;
		while (file->eof != 1) {
			/* Leave a margin so a block read cannot run off the 1MiB window.
			 * The rest of an ELF is stored in the page cache, not scratch. */
			if (elf_file && file_offset + (256 * 1024) > scratch_limit)
				break;
			if (file_offset >= scratch_limit) {
				UARTDebugOut("[loader]: file exceeds 1MiB scratch\r\n");
				break;
			}
			uint64_t block = ((uint64_t)_ldr_scratchBuffer + file_offset);
			size_t bytes_read = AuVFSNodeReadBlock(fsys, file, (uint64_t*)block);
			if (bytes_read == 0)
				break;
			file_offset += bytes_read;
			if (!elf_file && file_offset >= 4) {
				uint8_t* mag = (uint8_t*)_ldr_scratchBuffer;
				if (mag[0] == 0x7f && mag[1] == 'E' && mag[2] == 'L' && mag[3] == 'F')
					elf_file = 1;
			}
			/* The whole load is one syscall, and syscall entry masks IRQs.
			 * Reading netsurf.exe straight through froze the desktop until
			 * the last block. Hand the compositor a tick every 64KB. */
			if (file_offset - yield_mark >= (64 * 1024)) {
				yield_mark = file_offset;
				AuSchedYieldMs(1);
			}
		}

		size_t scratch_filled = file_offset;
		if (scratch_filled > scratch_limit)
			scratch_filled = scratch_limit;

		size_t total_pages = scratch_filled / PAGE_SIZE + ((scratch_filled % PAGE_SIZE) ? 1 : 0);
		for (size_t i = 0; i < total_pages; i++) {
			if ((i & 15) == 0)
				AuSchedYieldMs(1);
			uint64_t physcache = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
			AuMMPageCache* cache = AuMmngrPageCacheCreate();
			cache->physicalPage = physcache;
			cache->diskBlock = 0;
			cache->pageIndex = i;
			AuMmngrFileBackAddPageCache(fb, cache);

			memset((void*)P2V(physcache), 0, PAGE_SIZE);
			size_t copy_sz = PAGE_SIZE;
			if (i == total_pages - 1 && (scratch_filled % PAGE_SIZE) != 0)
				copy_sz = scratch_filled % PAGE_SIZE;
			memcpy((void*)P2V(physcache),
				   (void*)((uint64_t)_ldr_scratchBuffer + i * PAGE_SIZE),
				   copy_sz);
		}

		/* Anything past the scratch window goes into the page cache.
		 * PE images such as netsurf.exe are larger than 1 MiB too. */
		if (file->eof != 1) {
			while (file->eof != 1) {
				size_t bytes_read = AuVFSNodeReadBlock(fsys, file, (uint64_t*)_ldr_scratchBuffer);
				if (bytes_read == 0)
					break;
				size_t copied = 0;
				while (copied < bytes_read) {
					size_t page_index = (file_offset + copied) / PAGE_SIZE;
					size_t page_off = (file_offset + copied) % PAGE_SIZE;
					size_t chunk = PAGE_SIZE - page_off;
					if (chunk > (bytes_read - copied))
						chunk = bytes_read - copied;
					AuMMPageCache* cache = NULL;
					if (fb->pageCacheLast && fb->pageCacheLast->pageIndex == page_index)
						cache = fb->pageCacheLast;
					if (!cache) {
						uint64_t physcache = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
						memset((void*)P2V(physcache), 0, PAGE_SIZE);
						cache = AuMmngrPageCacheCreate();
						cache->physicalPage = physcache;
						cache->diskBlock = 0;
						cache->pageIndex = page_index;
						AuMmngrFileBackAddPageCache(fb, cache);
					}
					memcpy((uint8_t*)P2V(cache->physicalPage) + page_off,
						   (uint8_t*)_ldr_scratchBuffer + copied,
						   chunk);
					copied += chunk;
				}
				file_offset += bytes_read;
				if (file_offset - yield_mark >= (64 * 1024)) {
					yield_mark = file_offset;
					AuSchedYieldMs(1);
				}
			}
		}

		fb->readComplete = true;
		if (file->size)
			scratch_bytes = file->size;
		else
			scratch_bytes = file_offset;
	} else {
		AuMMPageCache* page = fb->pageCache;
		size_t copied = 0;
		size_t cached_end = 0;
		while (page != NULL) {
			size_t end = (size_t)((page->pageIndex + 1) * PAGE_SIZE);
			if (end > cached_end)
				cached_end = end;
			if (copied < (1024 * 1024)) {
				memcpy((void*)((uint64_t)_ldr_scratchBuffer + copied),
					   (void*)P2V(page->physicalPage),
					   PAGE_SIZE);
				copied += PAGE_SIZE;
			}
			page = page->next;
		}
		scratch_bytes = file->size ? file->size : cached_end;
	}

	fb->readComplete = 1;

	/* Magic lives on page-cache page 0. The tail read reuses scratch, so the
	 * scratch prefix is not the ELF header once the file passes that window. */
	int is_elf = 0;
	if (scratch_bytes >= 4 && fb->pageCache) {
		AuMMPageCache* mag_page = fb->pageCache;
		while (mag_page && mag_page->pageIndex != 0)
			mag_page = mag_page->next;
		if (mag_page) {
			uint8_t* mag = (uint8_t*)P2V(mag_page->physicalPage);
			if (mag[0] == 0x7f && mag[1] == 'E' && mag[2] == 'L' && mag[3] == 'F')
				is_elf = 1;
		}
	}
	if (is_elf) {
		int elf_rc = AuLoadElfImage(proc, fb, scratch_bytes, argc, argv);
#ifdef __KERNEL_PROFILER_ON__
		PROFILE_END("AuLoadExecToProcess");
#endif
		return elf_rc;
	}

	/* The tail read reuses scratch from offset 0. Put the first 1 MiB
	 * back so the DOS/NT headers and the dynamic-link check stay valid. */
	if (fb->pageCache) {
		for (AuMMPageCache* page = fb->pageCache; page; page = page->next) {
			if (page->pageIndex >= (1024 * 1024) / PAGE_SIZE)
				continue;
			memcpy((void*)((uint64_t)_ldr_scratchBuffer + page->pageIndex * PAGE_SIZE),
				   (void*)P2V(page->physicalPage),
				   PAGE_SIZE);
		}
	}

	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)_ldr_scratchBuffer;
	PIMAGE_NT_HEADERS nt = RAW_OFFSET(PIMAGE_NT_HEADERS, dos, dos->e_lfanew);
	PSECTION_HEADER secthdr =
		RAW_OFFSET(PSECTION_HEADER, &nt->OptionalHeader, nt->FileHeader.SizeOfOptionaHeader);

	uint64_t _image_base_ = 0x600000000; //   nt->OptionalHeader.ImageBase;
	entry ent = (entry)(nt->OptionalHeader.AddressOfEntryPoint +
						_image_base_); //nt->OptionalHeader.ImageBase);
	//AuTextOut("Image base address : %x \n", dos->e_magic);
	uint64_t* cr3 = proc->cr3;

	/* check if the binary is dynamically linked */
	if (AuPEFileIsDynamicallyLinked(_ldr_scratchBuffer)) {
		/* free the current file*/
		//UARTDebugOut("fb : %x \r\n", fb);
		//aa64_data_cache_clean_range(fb, sizeof(AuMMFileBack));
		//AuMmngrRemoveFileBack(fb);
		//aa64_data_cache_clean_range(file, sizeof(AuVFSNode));
		//kfree(file);
		//AuPmmngrReleasePage((uint64_t)V2P((sizeof(buf))));

		/* now load XELoader process, which'll further
		 * link this dynamic process with its shared
		 * libraries
		 */
		int char_cnt = 0;

		for (int i = 0; i < argc; i++) {
			char_cnt += strlen(argv[i]);
		}
		/* here we allocate extra memories for each strings
		 * because we cannot allocate it to stack, the stack
		 * will get changed once we enter the desired thread
		 */
		int num_args_ = 1 + argc;
		int string_len = strlen(filename);
		char* file__ = (char*)kmalloc(string_len + 1);
		strcpy(file__, filename);

		/* BUGG: if kmalloc allocates smaller memory below than 15 bytes,
		 * it crashes while freeing the allocated memory, that's why we
		 * allocate memory of size (string_len + char_cnt) * sizeof(char) for
		 * argument array
		 */
		char** argvs = (char**)kmalloc(num_args_ * sizeof(char*));
		//memset(argvs, 0, num_args_ * sizeof(char*));
		argvs[0] = file__;

		for (int i = 0; i < argc; i++) {
			char* argpass = (char*)kmalloc(strlen(argv[i]) + 1);
			memset(argpass, 0, strlen(argv[i]) + 1);
			strcpy(argpass, argv[i]);
			argvs[1 + i] = argpass;
		}

		if (argc > 0) {
			kfree(argv);
		}

		//	setStk();
		//AuReleaseSpinlock(loader_lock);

#ifdef __KERNEL_PROFILER_ON__
		PROFILE_END("AuLoadExecToProcess");
#endif
		/* load the loader */
		return AuLoadExecToProcess(proc, "/xeldr.exe", num_args_, argvs);
	}

	AuLoaderMapExecFromCache(proc, fb, nt, secthdr, _image_base_);

	AA64Thread* thr = AuCreateKthread(AuProcessEntUser, cr3, proc->name);
	thr->threadType = THREAD_LEVEL_USER;
	AuUserEntry* uentry = (AuUserEntry*)kmalloc(sizeof(AuUserEntry));
	memset(uentry, 0, sizeof(AuUserEntry));
	uentry->rsp = proc->_main_stack_;
	uentry->entrypoint = (size_t)ent;
	uentry->stackBase = proc->_main_stack_;
	int num_args = argc;
	uint64_t argvaddr = 0;
	uint64_t argvkernel = 0;
	if (num_args) {
		/* Allocate a memory for passing arguments */
		uint64_t* args =
			(uint64_t*)P2V((size_t)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id));
		memset(args, 0, PAGE_SIZE);
		if (!AuMapPageEx(proc->cr3,
						 (size_t)V2P((uint64_t)args),
						 0x40000000000,
						 PTE_AP_RW_USER | PTE_NORMAL_MEM)) {
			UARTDebugOut("Arguments address already mapped \n");
			argvaddr = 0;
			argvkernel = 0;
		} else {
			argvaddr = 0x40000000000;	 /* user-space VA for crt0 */
			argvkernel = (uint64_t)args; /* kernel-space VA for AuProcessEntUser to write */
		}
	}
	uentry->argvaddr = argvaddr;
	uentry->argvkernel = argvkernel;
	uentry->num_args = num_args;
	/*
	 * argv[] from ProcessLoadExec still points at the parent's user heap.
	 * AuProcessEntUser runs on the child's TTBR0, so those addresses are
	 * unmapped there (translation fault). Copy each string into kernel
	 * memory while we still have the parent's address space.
	 */
	if (num_args && argv) {
		char** kargv = (char**)kmalloc(num_args * sizeof(char*));
		memset(kargv, 0, num_args * sizeof(char*));
		for (int i = 0; i < num_args; i++) {
			size_t l = argv[i] ? strlen(argv[i]) : 0;
			kargv[i] = (char*)kmalloc(l + 1);
			if (argv[i])
				strcpy(kargv[i], argv[i]);
			else
				kargv[i][0] = 0;
		}
		kfree(argv);
		argv = kargv;
	}
	uentry->argvs = argv;
	thr->uentry = uentry;
	proc->main_thread = thr;
	thr->procSlot = proc;
	file->current = file->first_block;
	file->eof = 0;
#ifdef __KERNEL_PROFILER_ON__
	PROFILE_END("AuLoadExecToProcess");
#endif
	return 0;
}
/**
 * @brief AuInitialiseLoader -- initialize the loader
 * and allocate atleast 1 MiB for scratch use
 */
void AuInitialiseLoader() {
	for (int i = 0; i < (1024 * 1024) / 0x1000; i++) {
		AuMapPage((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL),
				  LOADER_SCRATCH_VIRT + i * 0x1000,
				  PTE_NORMAL_MEM);
	}
	_ldr_scratchBuffer = (uint64_t*)
		LOADER_SCRATCH_VIRT; // (uint64_t*)P2V((uint64_t)AuPmmngrAllocBlocks((1024 * 1024) / 0x1000));
	memset(_ldr_scratchBuffer, 0, 1024 * 1024);
	for (int i = 0; i < 64; i++)
		physFrames[i] = 0;

	AuTextOut("[aurora]: Kernel-level loader initialized \r\n");
}
