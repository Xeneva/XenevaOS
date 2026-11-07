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

/*
 * loader.cpp -- loader that is included inside kernel, which only
 * loads static binaries, for dynamic binaries another dynamic loader
 * is invoked from system, which is a static binary and loads the 
 * actual executable with its libraries
 */

#include <loader.h>
#include <process.h>
#include <Fs/vfs.h>
#include <string.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <Hal/x86_64_cpu.h>
#include <Mm/vmarea.h>
#include <Hal/hal.h>
#include <Hal/x86_64_gdt.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/serial.h>
#include <Sync/spinlock.h>
#include <Sync/mutex.h>
#include <aucon.h>

Spinlock* loader_lock;
AuMutex* loader_mutex;
bool is_loader_busy;

/* push item on the stack */
#define PUSH(stack, type, item) do { \
	stack -= sizeof(type); \
while (stack & (sizeof(type)-1))stack--; \
	*((type*)stack) = item; \
}while (0);


#define PUSHSTRING(stack, s) do { \
	size_t l = strlen(s) - 1; \
    do {\
       PUSH2(stack, char, s[l]); \
	   l--; \
	} while (l >= 0); \
}while (0)


extern "C" void x64_set_rbp(uint64_t rbp);

/*
* AuProcessEntUser -- main kernel thread call
* in order to enter user for processes
* @param rcx -- user entry structure
*/
void AuProcessEntUser(uint64_t rcx) {
	x64_cli();
	AuThread* t = AuGetCurrentThread();
	AuUserEntry* ent = t->uentry;

	//x64_set_rbp(ent->rsp);
	/* do all arguments passing stuff, arguments
	 * are passed as strings to stack
	 */
	char** argvs = (char**)ent->argvaddr;
	for (int i = 0; i < ent->num_args; i++) {
		char* str = ent->argvs[i];
		for (int j = strlen(str); j >= 0; j--) {
			PUSH(ent->rsp, char, str[j]);
		}
		argvs[i] = (char*)ent->rsp;
	}

	if (ent->argvs){
		for (int i = 0; i < ent->num_args; i++) {
			uint64_t addr = (uint64_t)ent->argvs[i];
			kfree((void*)addr);
		}
		void* address = (void*)ent->argvs;
		kfree(address);
	}
	ent->argvs = 0;
	PUSH(ent->rsp, size_t, (size_t)ent->argvaddr);
	PUSH(ent->rsp, size_t, ent->num_args);
	SeTextOut("Enterint USER GSBase -> %x \r\n", x64_read_msr(MSR_IA32_GS_BASE));
	x64_enter_user(ent->rsp, ent->entrypoint, ent->cs, ent->ss);
	while (1) {
	}
}

extern bool _vfs_debug_on;
/*
 * AuLoadExecToProcess -- loads an executable to the 
 * process
 * @param proc -- pointer to process data structure
 * @param filename -- executable file name
 * @param argc -- number of arguments
 * @param argv -- array of argument in strings
 */
int AuLoadExecToProcess(AuProcess* proc, char* filename, int argc,char** argv) {

	AuAcquireSpinlock(loader_lock);
	
	/* verify the filename, it can only be '.exe' file no '.dll' or other */
	char* v_ = strchr(filename, '.');
	if (v_)
		v_++;
	if (strcmp(v_, "exe") != 0) {
		AuReleaseSpinlock(loader_lock);
		SeTextOut("[aurora]: non-executable process \r\n");
		return -1;
	}
	AuVFSNode *fsys = AuVFSFind(filename);	
	_vfs_debug_on = true;
	AuVFSNode *file = AuVFSOpen(filename);
	
	if (!file) {
		SeTextOut("No File found -> %s \r\n", filename);
		AuReleaseSpinlock(loader_lock);
		return -1;
	}

	uint64_t* buf = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
	memset(buf, 0, 4096);

	size_t read_bytes = AuVFSNodeReadBlock(fsys, file, (uint64_t*)V2P((uint64_t)buf));

	
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)buf;
	PIMAGE_NT_HEADERS nt = raw_offset<PIMAGE_NT_HEADERS>(dos, dos->e_lfanew);
	PSECTION_HEADER secthdr = raw_offset<PSECTION_HEADER>(&nt->OptionalHeader, nt->FileHeader.SizeOfOptionaHeader);

	uint64_t _image_base_ = nt->OptionalHeader.ImageBase;
	entry ent = (entry)(nt->OptionalHeader.AddressOfEntryPoint + nt->OptionalHeader.ImageBase);
	uint64_t* cr3 = proc->cr3;

	/* check if the binary is dynamically linked */
	if (AuPEFileIsDynamicallyLinked(buf)) {
		/* free the current file*/
		kfree(file);
		AuPmmngrFree((void*)V2P((sizeof(buf))));

		/* now load XELoader process, which'll further
		 * link this dynamic process with its shared
		 * libraries
		 */
		int char_cnt = 0;
		for (int i = 0; i < argc; i++){
			char_cnt += strlen(argv[i]);
		}
		/* here we allocate extra memories for each strings
		 * because we cannot allocate it to stack, the stack
		 * will get changed once we enter the desired thread
		 */
		int num_args_ = 1 + argc;
		int string_len = strlen(filename);
		char* file__ = (char*)kmalloc(string_len);
		strcpy(file__, filename);

		/* BUGG: if kmalloc allocates smaller memory below than 15 bytes,
		 * it crashes while freeing the allocated memory, that's why we
		 * allocate memory of size (string_len + char_cnt) * sizeof(char) for
		 * argument array
		 */
		char** argvs = (char**)kmalloc(num_args_ * sizeof(char*));
		memset(argvs, 0, num_args_ * sizeof(char*));
		argvs[0] = file__;

		for (int i = 0; i < argc; i++){
			char* argpass = (char*)kmalloc(strlen(argv[i]));
			memset(argpass, 0, strlen(argv[i]));
			strcpy(argpass, argv[i]);
			argvs[1 + i] = argpass;
		}
	

		if (argc > 0){
			kfree(argv);
		}

		AuReleaseSpinlock(loader_lock);

		/* load the loader */
		return AuLoadExecToProcess(proc, "/xeldr.exe", num_args_, argvs);
	}

	AuMapPageEx(cr3, V2P((size_t)buf), _image_base_, X86_64_PAGING_USER);
	/* this should be memory mapped, i.e, sections should be
	 * memory mapped
	 */
	SeTextOut("Binary -> alignment -. %d \r\n", nt->OptionalHeader.FileAlignment);
	if (nt->OptionalHeader.FileAlignment == 512) {
		int count = 1;
		while (file->eof != 1) {
			uint64_t* block = (uint64_t*)AuPmmngrAlloc();
			memset(block, 0, 4096);
			AuVFSNodeReadBlock(fsys, file, block);
			AuMapPage((uint64_t)block, (_image_base_ + static_cast<uint64_t>(count) * 4096), 0);
			count++;
		}
	}
	else {
		for (size_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
			size_t sect_ld_addr = _image_base_ + secthdr[i].VirtualAddress;
			size_t sect_sz = secthdr[i].VirtualSize;
			int req_pages = sect_sz / PAGE_SIZE;
			if ((sect_sz % PAGE_SIZE) != 0)
				req_pages++;
			for (int j = 0; j < req_pages; j++) {
				uint64_t* block = (uint64_t*)P2V((uint64_t)AuPmmngrAlloc());/*(buf + secthdr[i].PointerToRawData);*/
				AuVFSNodeReadBlock(fsys, file, (uint64_t*)V2P((size_t)block));
				AuMapPageEx(cr3, V2P((size_t)block), sect_ld_addr + static_cast<uint64_t>(j) * PAGE_SIZE, X86_64_PAGING_USER);
			}

		}
	}

	
	/* create a vmarea segment here */
	AuVMArea* textarea = AuVMAreaCreate(_image_base_, VIRT_ADDR_ALIGN(_image_base_ + nt->OptionalHeader.SizeOfImage),
		VM_PRESENT | VM_EXEC,0, VM_TYPE_TEXT);
	textarea->len = textarea->end - textarea->start;
	textarea->file = 0;
	AuInsertVMArea(proc, textarea);


	uint64_t stack = proc->_main_stack_;

	/* create the user mode entry structure*/
	AuUserEntry *entry = (AuUserEntry*)kmalloc(sizeof(AuUserEntry));
	entry->entrypoint = (uint64_t)ent;
	entry->cs = SEGVAL(GDT_ENTRY_USER_CODE, 3);
	entry->ss = SEGVAL(GDT_ENTRY_USER_DATA, 3);
	entry->rsp = stack;
	entry->stackBase = stack;
	int num_args = argc;
	uint64_t argvaddr = 0;
	if (num_args) {
		/* Allocate a memory for passing arguments */
		char* args = (char*)P2V((size_t)AuPmmngrAlloc());
		memset(args, 0, PAGE_SIZE);
		if (!AuMapPageEx(proc->cr3, (size_t)V2P((size_t)args), 0x4000, X86_64_PAGING_USER)){
			AuTextOut("Arguments address already mapped \n");
			argvaddr = 0;
		}
		else{
			argvaddr = 0x4000;
		}
	}
	entry->argvaddr = argvaddr;	
	entry->num_args = num_args;
	entry->argvs = argv;
	AuThread *thr = AuCreateKthread(AuProcessEntUser,CreateKernelStack(proc,cr3) /* P2V((uint64_t)AuPmmngrAlloc() + 4096)*/,
		V2P((uint64_t)cr3), proc->name);
	thr->frame.rsp -= 32; // just decrease the stack by 40 for arguments passing
	thr->frame.kern_esp = thr->frame.rsp;
	thr->uentry = entry;
	thr->priviledge |= THREAD_LEVEL_USER | THREAD_LEVEL_MAIN_THREAD | ~THREAD_LEVEL_SUBTHREAD;
	proc->main_thread = thr;
	proc->num_thread = 1;
	proc->entry_point = ent;
	proc->_image_base_ = _image_base_;
	proc->_image_size_ = file->size;
	proc->state = PROCESS_STATE_READY;
	proc->file = file;
	proc->fsys = fsys;
	thr->procSlot = proc;
	AuReleaseSpinlock(loader_lock);
	return 0;
}

void AuInitialiseLoader() {
	loader_mutex = NULL;
	loader_lock = AuCreateSpinlock(false);
	loader_mutex = AuCreateMutex();
	is_loader_busy = false;
}

/*
 * AuLoaderGetMutex -- returns loader 
 * mutex
 */
AuMutex* AuLoaderGetMutex() {
	return loader_mutex;
}

bool AuIsLoaderBusy() {
	return is_loader_busy;
}