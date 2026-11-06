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

#include <pe.h>
#include <_null.h>
#include <string.h>
#include <aucon.h>
#include <Hal/serial.h>

/*
 * AuGetProcAddress -- get procedure address in a dll image
 * @param image -- dll image 
 * @param procname -- procedure name
 */
void* AuGetProcAddress(void *image, const char* procname){

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
* AuPEPrintExports -- get procedure address in a dll image
* @param image -- dll image
*/
void AuPEPrintExports(void *image){

	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_EXPORT + 1 > ntHeaders->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY& datadir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_EXPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return;
	PIMAGE_EXPORT_DIRECTORY exportdir = raw_offset<PIMAGE_EXPORT_DIRECTORY>(image, datadir.VirtualAddress);
	uint32_t* FuntionNameAddressArray = raw_offset<uint32_t*>(image, exportdir->AddressOfNames);
	uint16_t* FunctionOrdinalAddressArray = raw_offset<uint16_t*>(image, exportdir->AddressOfNameOrdinal);
	uint32_t* FunctionAddressArray = raw_offset<uint32_t*>(image, exportdir->AddressOfFunctions);

	for (int i = 0; i < exportdir->NumberOfNames; i++) {

		char* function_name = raw_offset<char*>(image, FuntionNameAddressArray[i]);
		AuTextOut("Fname -> %s \n", function_name);
	}
}
/*
* AuKernelLinkDLL -- Links a dll library to kernel symbols
* @param image -- dll image
*/
void AuKernelLinkDLL(void* image) {
	uint8_t *imageAligned = (uint8_t*)image;
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)imageAligned;
	PIMAGE_NT_HEADERS nt_headers = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY& datadir = nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0) {
		return;
	}
	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(imageAligned, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n) {
		const char* func = raw_offset<const char*>(image, importdir[n].NameRva);
		PIMAGE_IMPORT_LOOKUP_TABLE_PE32P iat = raw_offset<PIMAGE_IMPORT_LOOKUP_TABLE_PE32P>(imageAligned, importdir[n].ThunkTableRva);
		while (*iat) {
			PIMAGE_IMPORT_HINT_TABLE hint = raw_offset<PIMAGE_IMPORT_HINT_TABLE>(image, *iat);
			const char* fname = hint->name;
			void* procaddr = AuGetProcAddress((void*)0xFFFFC00000000000, fname);
			*iat = (uint64_t)procaddr;
			++iat;
		}
	}
}


/*
* AuKernelLinkImports -- Links kernel imports to dll
* @param image -- dll image
*/
void AuKernelLinkImports(void* image) {
	void* keaddr = (void*)0xFFFFC00000000000;
	uint8_t *imageAligned = (uint8_t*)keaddr;
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)imageAligned;
	PIMAGE_NT_HEADERS nt_headers = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY& datadir = nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return;
	
	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(imageAligned, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n) {
		const char* func = raw_offset<const char*>(imageAligned, importdir[n].NameRva);
		PIMAGE_IMPORT_LOOKUP_TABLE_PE32P iat = raw_offset<PIMAGE_IMPORT_LOOKUP_TABLE_PE32P>(imageAligned, importdir[n].ThunkTableRva);
		while (*iat) {
			PIMAGE_IMPORT_HINT_TABLE hint = raw_offset<PIMAGE_IMPORT_HINT_TABLE>(imageAligned, *iat);
			const char* fname = hint->name;
			//AuTextOut("Imports -> %s \n", fname);
			void* procaddr = AuGetProcAddress((void*)image, fname);
			*iat = (uint64_t)procaddr;
			++iat;
		}
	}
}

/* relocation types */
#define IMAGE_REL_BASED_ABSOLUTE  0
#define IMAGE_REL_BASED_HIGH      1
#define IMAGE_REL_BASED_LOW       2
#define IMAGE_REL_BASED_HIGHLOW   3
#define IMAGE_REL_BASED_HIGHADJ   4
#define IMAGE_REL_BASED_MIPS_JMPADDR   5
#define IMAGE_REL_BASED_MIPS_JMPADDR16 9
#define IMAGE_REL_BASED_DIR64          10

/* dll characteristics*/
#define IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE          0x40
#define IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY       0x80
#define IMAGE_DLL_CHARACTERISTICS_NX_COMPAT             0x100
#define IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION          0x200
#define IMAGE_DLL_CHARACTERISTICS_NO_SEH                0x400
#define IMAGE_DLL_CHARACTERISTICS_NO_BIND               0x800
#define IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER            0x2000
#define IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE 0x8000


#define DIV_ROUND_UP(x, y) \
	((x + y - 1) / y)


#define IMAGE_DATA_DIRECTORY_EXPORT 0
#define IMAGE_DATA_DIRECTORY_IMPORT 1
#define IMAGE_DATA_DIRECTORY_RELOC  5

/*
* AuKernelRelocatePE -- relocates the image from its actual
* base address
* @param image -- pointer to executable image
* @param nt -- nt headers
* @param diff -- difference from its original
*/
void AuKernelRelocatePE(void* image, PIMAGE_NT_HEADERS nt, uint64_t diff) {
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
				break;
			case IMAGE_REL_BASED_LOW:
				*reinterpret_cast<uint16_t*>(relocitem) += (diff & UINT16_MAX);
				break;
			case IMAGE_REL_BASED_HIGHLOW:
				*reinterpret_cast<uint32_t*>(relocitem) += (diff & UINT32_MAX);
				break;
			case IMAGE_REL_BASED_HIGHADJ:
				return;
				break;
			case IMAGE_REL_BASED_DIR64:
				*reinterpret_cast<uint64_t*>(relocitem) += diff;
				break;
			default:
				return;
				break;
			}

		}

		uint32_t next_off = DIV_ROUND_UP(cur_block->BlockSize, 4) * 4;
		cur_block = raw_offset<PIMAGE_RELOCATION_BLOCK>(cur_block, next_off);
	}
	//for (;;);
}

/*
 * AuPEFileIsDynamicallyLinked -- checks if the current
 * binary image is dynamically linked
 * @param image -- pointer to image address
 */
bool AuPEFileIsDynamicallyLinked(void* image) {
	uint8_t *imageAligned = (uint8_t*)image;
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)imageAligned;
	PIMAGE_NT_HEADERS nt_headers = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);
	
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return false;
	IMAGE_DATA_DIRECTORY* datadir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir->VirtualAddress == 0 || datadir->Size == 0)
		return false;
	
	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(imageAligned, datadir->VirtualAddress);
	return true;
}