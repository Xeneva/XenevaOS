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

#include "pe_.h"
#include "string.h"
#include <_xeneva.h>
#include <stdlib.h>
#include "XELdrObject.h"

/* dll characteristics*/
#define IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE          0x40
#define IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY       0x80
#define IMAGE_DLL_CHARACTERISTICS_NX_COMPAT             0x100
#define IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION          0x200
#define IMAGE_DLL_CHARACTERISTICS_NO_SEH                0x400
#define IMAGE_DLL_CHARACTERISTICS_NO_BIND               0x800
#define IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER            0x2000
#define IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE 0x8000

/* section type and protection types */
#define IMAGE_SECT_CODE               0x00000020
#define IMAGE_SECT_INITIALIZED_DATA   0x00000040
#define IMAGE_SECT_UNINITIALIZED_DATA 0x00000080
#define IMAGE_SECT_PROT_DISCARDABLE 0x02000000
#define IMAGE_SECT_PROT_NOT_CACHED  0x04000000
#define IMAGE_SECT_PROT_NOT_PAGED   0x08000000
#define IMAGE_SECT_PROT_SHARED      0x10000000
#define IMAGE_SECT_PROT_EXECUTE     0x20000000
#define IMAGE_SECT_PROT_READ        0x40000000
#define IMAGE_SECT_PROT_WRITE       0x80000000

/* relocation types */
#define IMAGE_REL_BASED_ABSOLUTE  0
#define IMAGE_REL_BASED_HIGH      1
#define IMAGE_REL_BASED_LOW       2
#define IMAGE_REL_BASED_HIGHLOW   3
#define IMAGE_REL_BASED_HIGHADJ   4
#define IMAGE_REL_BASED_MIPS_JMPADDR   5
#define IMAGE_REL_BASED_MIPS_JMPADDR16 9
#define IMAGE_REL_BASED_DIR64          10


#define DIV_ROUND_UP(x, y) \
	((x + y - 1) / y)

/*
 * XELdrRelocatePE -- relocates the image from its actual
 * base address
 * @param image -- pointer to executable image
 * @param nt -- nt headers
 * @param diff -- difference from its original
 */
void XELdrRelocatePE(void* image, PIMAGE_NT_HEADERS nt, uint64_t diff) {
	if (!diff)
		return;
	if ((nt->OptionalHeader.DllCharacteristics & IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE) == 0)
		return;
	if (IMAGE_DATA_DIRECTORY_RELOC + 1 > nt->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY& data_dir = nt->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_RELOC];
	if (data_dir.VirtualAddress == 0 || data_dir.Size == 0)
		return;
	PIMAGE_RELOCATION_BLOCK reloc_table = raw_offset<PIMAGE_RELOCATION_BLOCK>(image, data_dir.VirtualAddress);
	PIMAGE_RELOCATION_BLOCK cur_block = reloc_table;
	while (raw_diff(cur_block, reloc_table) < data_dir.Size) {
		uint32_t entries = (cur_block->BlockSize - 8) / 2;
		for (uint32_t i = 0; i < entries; ++i) {
			auto& entry = cur_block->entries[i];
			uint16_t type = entry.type;
			void* relocitem = raw_offset<void*>(image, entry.offset + cur_block->PageRVA);
			switch (type){
			case IMAGE_REL_BASED_ABSOLUTE:
				break;
			case IMAGE_REL_BASED_HIGH:
				*reinterpret_cast<uint16_t*>(relocitem) += (diff >> 16) & UINT16_MAX;
				//_KePrint("Relocation Based HIGH \r\n");
				break;
			case IMAGE_REL_BASED_LOW:
				*reinterpret_cast<uint16_t*>(relocitem) += (diff & UINT16_MAX);
				//_KePrint("Relocation Based LOW \r\n");
				break;
			case IMAGE_REL_BASED_HIGHLOW:
				*reinterpret_cast<uint32_t*>(relocitem) += (diff & UINT32_MAX);
				//_KePrint("Relocation Based HIGHLOW \r\n");
				break;
			case IMAGE_REL_BASED_HIGHADJ:
				//_KePrint("Rel based HIGHADJ \r\n");
				break;
			case IMAGE_REL_BASED_DIR64:
				*reinterpret_cast<uint64_t*>(relocitem) += diff; // &UINT32_MAX);
				//_KePrint("Relocating executable dir64 %x, diff : %x \r\n", *reinterpret_cast<uint64_t*>(relocitem), diff);
				break;
			default:
				break;
			}
		
		}
		uint32_t next_off = DIV_ROUND_UP(cur_block->BlockSize, 4) * 4;
		cur_block = raw_offset<PIMAGE_RELOCATION_BLOCK>(cur_block, next_off);
	}
	//for (;;);
}

bool _proc_debug = false;
/*
* AuGetProcAddress -- get procedure address in a dll image
* @param image -- dll image
* @param procname -- procedure name
*/
void* XELdrGetProcAddress(void *image, const char* procname){
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);
	
	if (IMAGE_DATA_DIRECTORY_EXPORT + 1 > ntHeaders->OptionalHeader.NumberOfRvaAndSizes)
		return NULL;
	IMAGE_DATA_DIRECTORY& datadir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_EXPORT];
	
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return NULL;

	PIMAGE_EXPORT_DIRECTORY exportdir = raw_offset<PIMAGE_EXPORT_DIRECTORY>(image, datadir.VirtualAddress);
	uint32_t* FuntionNameAddressArray = raw_offset<uint32_t*>(image, exportdir->AddressOfNames);
	uint16_t* FunctionOrdinalAddressArray = raw_offset<uint16_t*>(image, exportdir->AddressOfNameOrdinal);
	uint32_t* FunctionAddressArray = raw_offset<uint32_t*>(image, exportdir->AddressOfFunctions);

	for (int i = 0; i < exportdir->NumberOfNames; i++) {
		char* function_name = raw_offset<char*>(image, FuntionNameAddressArray[i]);
		if (strcmp(function_name, procname) == 0) {
			uint16_t ordinal = FunctionOrdinalAddressArray[i];
			uint32_t FunctionAddress = FunctionAddressArray[ordinal];
			return raw_offset<void*>(image, FunctionAddress);
		}
	}

	return nullptr;
}

/*
* XELdrLinkPE -- Links a dll library to its executable
* @param image -- dll image
* @param exporter -- executable image
*/
void XELdrLinkPE(void* exec) {
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)exec;
	PIMAGE_NT_HEADERS nt_headers = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return;

	IMAGE_DATA_DIRECTORY& datadir = nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0) {
		return;
	}

	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(exec, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n) {
		const char* func = raw_offset<const char*>(exec, importdir[n].NameRva);
		XELoaderObject* dep_obj = XELdrGetObject(func);
		if (!dep_obj)
			return;
		void* dll_dep = (void*)dep_obj->load_addr;
		PIMAGE_IMPORT_LOOKUP_TABLE_PE32P iat = raw_offset<PIMAGE_IMPORT_LOOKUP_TABLE_PE32P>(exec, importdir[n].ThunkTableRva);
		while (*iat) {
			PIMAGE_IMPORT_HINT_TABLE hint = raw_offset<PIMAGE_IMPORT_HINT_TABLE>(exec, *iat);
			const char* fname = hint->name;
			void* procaddr = XELdrGetProcAddress((void*)dll_dep, fname);
			*iat = (uint64_t)procaddr;
			++iat;
		}
	}
}

bool _debug_buf = false;
/*
* XELdrCreatePEObjects -- load all required objects
* @param image -- dll image
* @param exporter -- executable image
*/
void XELdrCreatePEObjects(void* exec) {
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)exec;
	PIMAGE_NT_HEADERS nt_headers = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);

	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return;

	IMAGE_DATA_DIRECTORY& datadir = nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0) {
		return;
	}
	

	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(exec, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n) {
		const char* func = raw_offset<const char*>(exec, importdir[n].NameRva);
		if (!XELdrCheckObject(func)) {
			int flen = strlen(func);
			char *separator = (char*)malloc(flen + 1);
			strcpy(separator, "/");
			char *fname = strcat(separator, func);
			XELoaderObject* obj = XELdrCreateObj(fname);
			free(separator);
		}
		PIMAGE_IMPORT_LOOKUP_TABLE_PE32P iat = raw_offset<PIMAGE_IMPORT_LOOKUP_TABLE_PE32P>(exec, importdir[n].ThunkTableRva);
		while (*iat) {
			PIMAGE_IMPORT_HINT_TABLE hint = raw_offset<PIMAGE_IMPORT_HINT_TABLE>(exec, *iat);
			const char* fname = hint->name;
			++iat;
		}
	}
}


/*
 * XELdrLinkDependencyPE -- link all dependencies except
 * the main object
 * @param obj -- obj to point
 */
void XELdrLinkDependencyPE(XELoaderObject* obj) {
	if (obj->linked)
		return;
	void* exec = (void*)obj->load_addr;
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)exec;
	PIMAGE_NT_HEADERS nt_headers = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return;

	IMAGE_DATA_DIRECTORY& datadir = nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0) {
		return;
	}

	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(exec, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n) {
		const char* func = raw_offset<const char*>(exec, importdir[n].NameRva);
		XELoaderObject* dep_obj = XELdrGetObject(func);
		if (!dep_obj){
			return;
		}
	  
		XELdrLinkDependencyPE(dep_obj);
		void* dll_dep = (void*)dep_obj->load_addr;
		PIMAGE_IMPORT_LOOKUP_TABLE_PE32P iat = raw_offset<PIMAGE_IMPORT_LOOKUP_TABLE_PE32P>(exec, importdir[n].ThunkTableRva);
		while (*iat) {
			PIMAGE_IMPORT_HINT_TABLE hint = raw_offset<PIMAGE_IMPORT_HINT_TABLE>(exec, *iat);
			const char* fname = hint->name;
			void* procaddr = XELdrGetProcAddress((void*)dll_dep, fname);
			*iat = (uint64_t)procaddr;
			++iat;
		}
	}

	obj->linked = true;
}

void XELdrLoadResourceDirectory(void *image) {
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS nt_headers = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);

	IMAGE_DATA_DIRECTORY& datadir = nt_headers->OptionalHeader.DataDirectory[2];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0) {
		return;
	}

	PIMAGE_RESOURCE_DIRECTORY resdir = (PIMAGE_RESOURCE_DIRECTORY)(datadir.VirtualAddress + (uint64_t)image);
	IMAGE_RESOURCE_DIRECTORY_ENTRY *lpResEntry = resdir->DirectoryEntries;

	int entries = resdir->NumberOfIdEntries + resdir->NumberOfNamedEntries;

	while (entries-- != 0) {
		_KePrint("res id -> %d \n", lpResEntry->Id);
		lpResEntry++;
	}
	
}

void XELdrGetIATBoundary(void* image,uint64_t* cowStart, uint64_t* cowEnd) {
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS nt_headers = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);

	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return;

	IMAGE_DATA_DIRECTORY& datadir = nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0) {
		return;
	}

	size_t iatStart = (size_t)image + datadir.VirtualAddress;
	size_t iatEnd = iatStart + datadir.Size;

	*cowStart = iatStart; // iatStart & ~(0x1000 - 1);
	*cowEnd = iatEnd; // (iatEnd + 0x1000 - 1) & ~(0x1000 - 1);

	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(image, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n) {
		const char* func = raw_offset<const char*>(image, importdir[n].NameRva);
		XELoaderObject* dep_obj = XELdrGetObject(func);
		if (!dep_obj) {
			return;
		}

		XELdrLinkDependencyPE(dep_obj);
		void* dll_dep = (void*)dep_obj->load_addr;
		PIMAGE_IMPORT_LOOKUP_TABLE_PE32P iat = raw_offset<PIMAGE_IMPORT_LOOKUP_TABLE_PE32P>(image, importdir[n].ThunkTableRva);
		while (*iat) {
			PIMAGE_IMPORT_HINT_TABLE hint = raw_offset<PIMAGE_IMPORT_HINT_TABLE>(image, *iat);
			++iat;
		}
	}
}