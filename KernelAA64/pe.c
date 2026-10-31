/**
* @file pe.c
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

#include <pe.h>
#include <_null.h>
#include <string.h>
#include <aucon.h>
#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif
#include <Drivers/uart.h>

#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE	0x00004550

#ifdef __GNUC__
struct kernel_export {
	char* name;
	void* addr;
};
extern struct kernel_export k_exports[];
extern int k_exports_count;
#endif

/**
 * @brief AuGetProcAddress -- get procedure address in a dll image
 * @param image -- dll image
 * @param procname -- procedure name
 * @return return the procedure address in memory
 */
extern void* AuDriverImportOverride(const char* name);

void* AuGetProcAddress(void* image, const char* procname) {
	/* Kernel symbols a driver imports can be swapped for tracking wrappers
	 * (component model). Checked first so it works for the allowlist path
	 * below AND for the PE export-table path used by the LLVM/Windows build,
	 * where __GNUC__ is not defined. */
	if (image == (void*)0xFFFFC00000000000) {
		void* override = AuDriverImportOverride(procname);
		if (override)
			return override;
	}
#ifdef __GNUC__
	if (image == (void*)0xFFFFC00000000000) {
		for (int i = 0; i < k_exports_count; i++) {
			if (strcmp(k_exports[i].name, procname) == 0) {
				return k_exports[i].addr;
			}
		}
		return NULL;
	}
#endif

	IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)image;
	if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
		return NULL;
	}
	PIMAGE_NT_HEADERS ntHeaders = RAW_OFFSET(IMAGE_NT_HEADERS*, dos_header, dos_header->e_lfanew);
	if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
		return NULL;
	}
	if (IMAGE_DATA_DIRECTORY_EXPORT + 1 > ntHeaders->OptionalHeader.NumberOfRvaAndSizes) {
		return NULL;
	}
	IMAGE_DATA_DIRECTORY datadir =
		ntHeaders->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_EXPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0) {
		return NULL;
	}
	PIMAGE_EXPORT_DIRECTORY exportdir =
		RAW_OFFSET(PIMAGE_EXPORT_DIRECTORY, image, datadir.VirtualAddress);
	uint32_t* FuntionNameAddressArray = RAW_OFFSET(uint32_t*, image, exportdir->AddressOfNames);
	uint16_t* FunctionOrdinalAddressArray =
		RAW_OFFSET(uint16_t*, image, exportdir->AddressOfNameOrdinal);
	uint32_t* FunctionAddressArray = RAW_OFFSET(uint32_t*, image, exportdir->AddressOfFunctions);

	for (int i = 0; i < exportdir->NumberOfNames; i++) {
		char* function_name = RAW_OFFSET(char*, image, FuntionNameAddressArray[i]);
		if (strcmp(function_name, procname) == 0) {
			uint16_t ordinal = FunctionOrdinalAddressArray[i];
			uint32_t FunctionAddress = FunctionAddressArray[ordinal];
			return RAW_OFFSET(void*, image, FunctionAddress);
		}
	}

	return NULL;
}

/**
* @brief AuPEPrintExports -- print all function exports for DEBUG purpose
* @param image -- dll image
*/
void AuPEPrintExports(void* image) {
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = RAW_OFFSET(PIMAGE_NT_HEADERS, dos_header, dos_header->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_EXPORT + 1 > ntHeaders->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY datadir =
		ntHeaders->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_EXPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return;
	PIMAGE_EXPORT_DIRECTORY exportdir =
		RAW_OFFSET(PIMAGE_EXPORT_DIRECTORY, image, datadir.VirtualAddress);
	uint32_t* FuntionNameAddressArray = RAW_OFFSET(uint32_t*, image, exportdir->AddressOfNames);
	uint16_t* FunctionOrdinalAddressArray =
		RAW_OFFSET(uint16_t*, image, exportdir->AddressOfNameOrdinal);
	uint32_t* FunctionAddressArray = RAW_OFFSET(uint32_t*, image, exportdir->AddressOfFunctions);

	for (int i = 0; i < exportdir->NumberOfNames; i++) {
		char* function_name = RAW_OFFSET(char*, image, FuntionNameAddressArray[i]);
		AuTextOut("Fname -> %s \n", function_name);
	}
}
/**
* @brief AuKernelLinkDLL -- Links a dll library to kernel symbols
* @param image -- dll image
*/
void AuKernelLinkDLL(void* image) {
	uint8_t* imageAligned = (uint8_t*)image;
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)imageAligned;
	if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
		return;
	PIMAGE_NT_HEADERS nt_headers = RAW_OFFSET(PIMAGE_NT_HEADERS, dos_header, dos_header->e_lfanew);
	if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
		return;
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY datadir =
		nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0) {
		return;
	}
	PIMAGE_IMPORT_DIRECTORY importdir =
		RAW_OFFSET(PIMAGE_IMPORT_DIRECTORY, imageAligned, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n) {
		PIMAGE_IMPORT_LOOKUP_TABLE_PE32P iat =
			RAW_OFFSET(PIMAGE_IMPORT_LOOKUP_TABLE_PE32P, imageAligned, importdir[n].ThunkTableRva);
		while (*iat) {
			PIMAGE_IMPORT_HINT_TABLE hint = RAW_OFFSET(PIMAGE_IMPORT_HINT_TABLE, image, *iat);
			const char* fname = hint->name;
			void* procaddr = AuGetProcAddress((void*)0xFFFFC00000000000, fname);
			*iat = (uint64_t)procaddr;
			++iat;
		}
	}
}

/**
*  @briefAuKernelLinkImports -- Links kernel imports to dll
* @param image -- dll image
*/
void AuKernelLinkImports(void* image) {
	void* keaddr = (void*)0xFFFFC00000000000;
	uint8_t* imageAligned = (uint8_t*)keaddr;
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)imageAligned;
	PIMAGE_NT_HEADERS nt_headers = RAW_OFFSET(PIMAGE_NT_HEADERS, dos_header, dos_header->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY datadir =
		nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return;

	PIMAGE_IMPORT_DIRECTORY importdir =
		RAW_OFFSET(PIMAGE_IMPORT_DIRECTORY, imageAligned, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n) {
		const char* func = RAW_OFFSET(const char*, imageAligned, importdir[n].NameRva);
		PIMAGE_IMPORT_LOOKUP_TABLE_PE32P iat =
			RAW_OFFSET(PIMAGE_IMPORT_LOOKUP_TABLE_PE32P, imageAligned, importdir[n].ThunkTableRva);
		while (*iat) {
			PIMAGE_IMPORT_HINT_TABLE hint =
				RAW_OFFSET(PIMAGE_IMPORT_HINT_TABLE, imageAligned, *iat);
			const char* fname = hint->name;
			//AuTextOut("Imports -> %s \n", fname);
			void* procaddr = AuGetProcAddress((void*)image, fname);
			*iat = (uint64_t)procaddr;
			++iat;
		}
	}
}

/** relocation types **/
#define IMAGE_REL_BASED_ABSOLUTE	   0
#define IMAGE_REL_BASED_HIGH		   1
#define IMAGE_REL_BASED_LOW			   2
#define IMAGE_REL_BASED_HIGHLOW		   3
#define IMAGE_REL_BASED_HIGHADJ		   4
#define IMAGE_REL_BASED_MIPS_JMPADDR   5
#define IMAGE_REL_BASED_MIPS_JMPADDR16 9
#define IMAGE_REL_BASED_DIR64		   10

/** dll characteristics **/
#define IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE			0x40
#define IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY		0x80
#define IMAGE_DLL_CHARACTERISTICS_NX_COMPAT				0x100
#define IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION			0x200
#define IMAGE_DLL_CHARACTERISTICS_NO_SEH				0x400
#define IMAGE_DLL_CHARACTERISTICS_NO_BIND				0x800
#define IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER			0x2000
#define IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE 0x8000

#define DIV_ROUND_UP(x, y) ((x + y - 1) / y)

#define IMAGE_DATA_DIRECTORY_EXPORT 0
#define IMAGE_DATA_DIRECTORY_IMPORT 1
#define IMAGE_DATA_DIRECTORY_RELOC	5

/**
* @brief AuKernelRelocatePE -- relocates the image from its actual
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
	IMAGE_DATA_DIRECTORY data_dir = nt->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_RELOC];
	if (data_dir.VirtualAddress == 0 || data_dir.Size == 0)
		return;

	PIMAGE_RELOCATION_BLOCK reloc_table =
		RAW_OFFSET(PIMAGE_RELOCATION_BLOCK, image, data_dir.VirtualAddress);
	PIMAGE_RELOCATION_BLOCK cur_block = reloc_table;

	while (RAW_DIFF(cur_block, reloc_table) < data_dir.Size) {
		uint32_t entries = (cur_block->BlockSize - 8) / 2;
		for (uint32_t i = 0; i < entries; ++i) {
			IMAGE_RELOCATION_ENTRY entry = cur_block->entries[i];
			uint16_t type = entry.type;
			void* relocitem = RAW_OFFSET(void*, image, entry.offset + cur_block->PageRVA);
			switch (type) {
			case IMAGE_REL_BASED_ABSOLUTE:
				break;
			case IMAGE_REL_BASED_HIGH:
				*(uint16_t*)relocitem += (diff >> 16) & UINT16_MAX;
				break;
			case IMAGE_REL_BASED_LOW:
				*(uint16_t*)relocitem += (diff & UINT16_MAX);
				break;
			case IMAGE_REL_BASED_HIGHLOW:
				*(uint32_t*)relocitem += (diff & UINT32_MAX);
				break;
			case IMAGE_REL_BASED_HIGHADJ:
				return;
				break;
			case IMAGE_REL_BASED_DIR64:
				/* Full 64-bit delta. Masking to 32 bits maps a preferred
				 * address such as 0x1800050d0 onto 0x200A140D0. */
				*(uint64_t*)relocitem += diff;
				break;
			default:
				return;
				break;
			}
		}

		uint32_t next_off = DIV_ROUND_UP(cur_block->BlockSize, 4) * 4;
		cur_block = RAW_OFFSET(PIMAGE_RELOCATION_BLOCK, cur_block, next_off);
	}
	//for (;;);
}

/**
 * @brief AuPEFileIsDynamicallyLinked -- checks if the current
 * binary image is dynamically linked
 * @param image -- pointer to image address
 */
bool AuPEFileIsDynamicallyLinked(void* image) {
	uint8_t* imageAligned = (uint8_t*)image;
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)imageAligned;
	PIMAGE_NT_HEADERS nt_headers = RAW_OFFSET(PIMAGE_NT_HEADERS, dos_header, dos_header->e_lfanew);

	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > nt_headers->OptionalHeader.NumberOfRvaAndSizes)
		return false;
	IMAGE_DATA_DIRECTORY* datadir =
		&nt_headers->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	UARTDebugOut("data dir va = %d , size = %d \r\n", datadir->VirtualAddress, datadir->Size);
	if (datadir->VirtualAddress == 0 || datadir->Size == 0)
		return false;

	PIMAGE_IMPORT_DIRECTORY importdir =
		RAW_OFFSET(PIMAGE_IMPORT_DIRECTORY, imageAligned, datadir->VirtualAddress);
	return true;
}

static const char* AuCoffSymName(IMAGE_COFF_SYMBOL* sym, const char* strtab) {
	if (sym->name.lng.zeros != 0)
		return sym->name.short_name;
	return strtab + sym->name.lng.offset;
}

#define OFFSETOF(s, m) ((size_t)&(((s*)0)->m))

#define IMAGE_FIRST_SECTION(nt)                                                                    \
	((SECTION_HEADER*)((uint8_t*)(nt) + OFFSETOF(IMAGE_NT_HEADERS, OptionalHeader) +               \
					   ((IMAGE_NT_HEADERS*)(nt))->FileHeader.SizeOfOptionaHeader))

static uint64_t AuCoffSectionBase(uint8_t* imageBase, int section_idx) {
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)imageBase;
	IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(imageBase + dos->e_lfanew);
	SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
	return (uint64_t)(imageBase) + sec[section_idx - 1].VirtualAddress;
}

/**
 * @brief AuCoffResolveAddress -- print the symbol function name
 * for specific pointing address
 * @param imageBase - base address of the image
 * @param paddr -- Pointing address, or instruction/function address 
 */
bool AuCoffResolveAddress(uint8_t* imageBase, uint64_t paddr) {
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)imageBase;
	IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(imageBase + dos->e_lfanew);
	uint32_t symoff = nt->FileHeader.PointerToSymbolTable;
	uint32_t nsyms = nt->FileHeader.NumberOfSymbols;

	uint64_t rva = paddr - (uint64_t)imageBase;

	IMAGE_COFF_SYMBOL* coff_symtab = (void*)(imageBase + symoff);
	char* strtab = (char*)(imageBase + symoff + nsyms * 18);
	IMAGE_COFF_SYMBOL* best = NULL;
	uint64_t best_va = 0;

	UARTDebugOut("num_symbols : %d, SymbolTable : %x \r\n", nsyms, coff_symtab);

	for (uint32_t j = 0; j < nsyms; j++) {
		IMAGE_COFF_SYMBOL* s = &coff_symtab[j];

		if (s->section <= 0)
			goto next;

		if ((s->type & 0x20) == 0)
			goto next;

		uint64_t sym_va = AuCoffSectionBase(imageBase, s->section) + s->value;
		uint64_t sym_rva = sym_va - (uint64_t)imageBase;

		if (sym_rva <= rva) {
			if (!best || sym_rva > (best_va - (uint64_t)imageBase)) {
				best = s;
				best_va = sym_va;
			}
		}
	next:
		j += s->aux_count;
	}

	if (best) {
		UARTDebugOut("[aurora]: pe symbol name : %s \r\n", AuCoffSymName(best, strtab));
		UARTDebugOut("[aurora]: out offset : %x \r\n", (paddr - best_va));
	} else
		UARTDebugOut("[aurora]: pe symbol <?no match?>, offset : %x \r\n", rva);

	return 1;
}