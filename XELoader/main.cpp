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

#include <_xeneva.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include <sys/iocodes.h>
#include <sys/_kesignal.h>
#include "pe_.h"
#include <sys/_procheap.h>
#include "XELdrObject.h"

void XECopyMem(void* dest, void* src, size_t len) {
	uint8_t* dstp = (uint8_t*)dest;
	uint8_t* srcp = (uint8_t*)src;
	while (len--)
		*dstp++ = *srcp++;
}

extern bool _debug_buf;
uint8_t* ldr_buffer;



/*
 * XELdrLoadObject -- loads an object
 * @param obj
 */
int XELdrLoadObject(XELoaderObject *obj){
#ifdef ARCH_ARM64
	//uint64_t addr_preserve = (uint64_t)obj->objname;
#endif
	int file = _KeOpenFile(obj->objname, FILE_OPEN_READ_ONLY);
	if (file == -1)
		return 0;

#ifdef ARCH_ARM64
	//obj->objname = (char*)addr_preserve;
#endif
	XEFileStatus *stat = (XEFileStatus*)malloc(sizeof(XEFileStatus));
	memset(stat, 0, sizeof(XEFileStatus));
	_KeFileStat(file, stat);

	uint64_t* buffer = (uint64_t*)_KeMemMap(NULL, 4096, 0, 0,file, 0);
	//memset(buffer, 0, 4096);

	obj->len += 4096;

	int countbytes = 4096;

	uint64_t* first_ptr = buffer;
	uint64_t _image_load_base_ = (uint64_t)first_ptr;

	size_t ret_bytes = 0;

	//ret_bytes = _KeReadFile(file, buffer, 4096);
	IMAGE_DOS_HEADER *dos_ = (IMAGE_DOS_HEADER*)buffer;
	PIMAGE_NT_HEADERS nt = raw_offset<PIMAGE_NT_HEADERS>(dos_, dos_->e_lfanew);
	uint64_t original_base = nt->OptionalHeader.ImageBase;
	uint64_t new_addr = _image_load_base_;
	uint64_t diff = new_addr - original_base;
	/* FORMULA to obtain real address -> new_addr - diff*/
	PSECTION_HEADER secthdr = raw_offset<PSECTION_HEADER>(&nt->OptionalHeader, nt->FileHeader.SizeOfOptionaHeader);

	if (nt->OptionalHeader.SectionAlignment == 512) {
		while (!stat->eof) {
			uint64_t* block = (uint64_t*)_KeMemMap(NULL, 4096, 0, 0,file, 0);
			_KeReadFile(file, block, 4096);
			_KeFileStat(file, stat);
			obj->len += 4096;
		}
	}
	else {
		for (size_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
			size_t sect_ld_addr = _image_load_base_ + secthdr[i].VirtualAddress;
			size_t sect_sz = secthdr[i].VirtualSize;
			int req_pages = sect_sz / 4096 +
				((sect_sz % 4096) ? 1 : 0);
			uint64_t* block = 0;
			_KeFileSetOffset(file, secthdr[i].PointerToRawData);
			int flags = 0;
			if ((secthdr[i].Characteristics & IMAGE_SCN_MEM_WRITE) &&
				!(secthdr[i].Characteristics & IMAGE_SCN_MEM_SHARED))
				flags |= MEMMAP_FLAG_COW;
			if (secthdr[i].Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
				flags |= MEMMAP_FLAG_COW;
		
			for (int j = 0; j < req_pages; j++) {
				uint64_t* alloc = (uint64_t*)_KeMemMap(NULL, 4096, 0, flags, file, obj->len);
				//memset(alloc, 0, 4096);
				if (!block)
					block = alloc;
				countbytes += 4096;
				//int bytes = _KeReadFile(file, alloc, 4096);
				_KeFileStat(file, stat);
				if (stat->eof) {
					//_KePrint("File already ended \n");
					//break;
				}
				//ret_bytes += bytes;
				obj->len += 4096;
			}

			if (secthdr[i].VirtualSize > secthdr[i].SizeOfRawData) {
				memset(raw_offset<void*>(block, secthdr[i].SizeOfRawData), 0, secthdr[i].VirtualSize - secthdr[i].SizeOfRawData);
			}
		}
	}

	uint8_t* aligned_buf = (uint8_t*)first_ptr;

	XELdrRelocatePE(aligned_buf, nt, diff);
	XELdrCreatePEObjects(first_ptr);
	obj->load_addr = _image_load_base_;
	obj->loaded = true;
	_KeMemMapDirty((void*)_image_load_base_, obj->len, 0, 0);

	//_KeCloseFile(file);
	free(stat);
	return 0;
}

extern "C" uint64_t _printstack();
/*
 * XELdrStartProc -- starts a new process
 * @param filename -- path and name of the
 * process
 */
int XELdrStartProc(char* filename, XELoaderObject *obj) {
	int file = 0;
	char fname[128];
	memset(fname, 0, 127);
	fname[127] = '\0';
	
	strncpy(fname, filename,127);
	file = _KeOpenFile(fname, FILE_OPEN_READ_ONLY);
	XEFileStatus *stat = (XEFileStatus*)malloc(sizeof(XEFileStatus));
	memset(stat, 0, sizeof(XEFileStatus));

	
	int ret_bytes = 0;
	
	_KeFileStat(file, stat);



	uint64_t* buffer = (uint64_t*)_KeMemMap(NULL,4096, 0, 0, file, 0);
	//memset(buffer, 0, 4096);
	obj->len += 4096;

	uint64_t* first_ptr = buffer;
	uint64_t _image_load_base_ = (uint64_t)first_ptr;

	//ret_bytes = _KeReadFile(file, buffer, 4096);
	IMAGE_DOS_HEADER *dos_ = (IMAGE_DOS_HEADER*)buffer;
	PIMAGE_NT_HEADERS nt = raw_offset<PIMAGE_NT_HEADERS>(dos_, dos_->e_lfanew);
	PSECTION_HEADER secthdr = raw_offset<PSECTION_HEADER>(&nt->OptionalHeader, nt->FileHeader.SizeOfOptionaHeader);
	intptr_t original_base = nt->OptionalHeader.ImageBase;
	intptr_t new_addr = _image_load_base_;
	intptr_t diff = new_addr - original_base;
	
	if (nt->OptionalHeader.SectionAlignment == 512) {
		while (!stat->eof) {
			uint64_t* block = (uint64_t*)_KeMemMap(NULL, 4096, 0, 0,file, 0);
		//	int bytes = _KeReadFile(file, block, 4096);
			//ret_bytes += bytes;
			_KeFileStat(file, stat);
		}
	}
	else {
		for (size_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
			size_t sect_ld_addr = _image_load_base_ + secthdr[i].VirtualAddress;
			size_t sect_sz = secthdr[i].VirtualSize;
			int req_pages = sect_sz / 4096 +
				((sect_sz % 4096) ? 1 : 0);
			uint64_t* block = 0;
			_KeFileSetOffset(file, secthdr[i].PointerToRawData);
			int flags = 0;
		}

		for (size_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
			size_t sect_ld_addr = _image_load_base_ + secthdr[i].VirtualAddress;
			size_t sect_sz = secthdr[i].VirtualSize;
			int req_pages = sect_sz / 4096 +
				((sect_sz % 4096) ? 1 : 0);
			uint64_t* block = 0;
			_KeFileSetOffset(file, secthdr[i].PointerToRawData);
			int flags = 0;
			if ((secthdr[i].Characteristics & IMAGE_SCN_MEM_WRITE) &&
				!(secthdr[i].Characteristics & IMAGE_SCN_MEM_SHARED))
				flags |= MEMMAP_FLAG_COW;
			if (secthdr[i].Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
				flags |= MEMMAP_FLAG_COW;

			for (int j = 0; j < req_pages; j++) {
				uint64_t* alloc = (uint64_t*)_KeMemMap(NULL, 4096, 0, flags,file, obj->len);
				if (!block)
					block = alloc;
				
				//int bytes = _KeReadFile(file, alloc, 4096);
				_KeFileStat(file, stat);
				if (stat->eof) {
					//_KePrint("File already ended \n");
					
				}
				//ret_bytes += bytes;
				obj->len += 4096;
			}
			if (secthdr[i].VirtualSize > secthdr[i].SizeOfRawData)
				memset(raw_offset<void*>(block, secthdr[i].SizeOfRawData), 0, secthdr[i].VirtualSize - secthdr[i].SizeOfRawData);
		}
	}
	uint8_t* aligned_buff = (uint8_t*)first_ptr;
	XELdrRelocatePE(aligned_buff, nt, diff);
	XELdrCreatePEObjects(aligned_buff);
	obj->load_addr = _image_load_base_;
	obj->loaded = true;
	obj->entry_addr = _image_load_base_ + nt->OptionalHeader.AddressOfEntryPoint;
	_KeMemMapDirty((void*)_image_load_base_, obj->len, 0, 0);
	free(stat);
	//_KeCloseFile(file);
	return 0;
}


/*
 * DefaultSignalHandler -- default signal handler
 * for all signals
 */
void DefaultSignalHandler(int signo) {
	_KeProcessExit();
}

typedef int(*entrypoint) (int argc, char*argv[]);


/*
 * main entry point of the loader, it accepts
 * three commands "-about", "-f", and filename
 * in 3rd argument
 */
extern "C" void main(int argc, char* argv[]) {
	
#ifdef ARCH_ARM64
	_KePrint("From inside XELoader (Xeneva Dynamic Loader v1.0 ARM64)\n");
#elif ARCH_X64
	_KePrint("From inside XELoader (Xeneva Dynamic Loader v1.0 x86_64 \r\n");
#endif
	_KePrint("Copyright (C) Manas Kamal Choudhury 2023-2025 \r\n");

	///* simply exit*/
	if (!argv)
		_KeProcessExit();

	XELdrInitObjectList();


	/* load the main object */
	char* filename = argv[0];


	XELoaderObject* mainobj = XELdrCreateObj(filename);
	
	XELdrStartProc(filename, mainobj);

	XELdrLoadAllObject();
	
	/* links all dependencies of libraries*/
	XELdrLinkDepObject(mainobj);
	
	/* now link all objects from the list
	 * to main object
	 */
	XELdrLinkAllObject(mainobj);
	
	
	uint64_t entry_addr = mainobj->entry_addr;
	

	//XELdrClearObjectList();

	
	/* register the default signal handler to
	 * all signal
	 */

	//for (int i = 0; i < NUMSIGNALS; i++)
	//	_KeSetSignal(i + 1, DefaultSignalHandler);


	entrypoint e = (entrypoint)entry_addr;

	e(argc , argv);
	
	while (1) {
		_KeProcessExit();
	}
}