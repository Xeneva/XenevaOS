/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#ifndef __PE_H__
#define __PE_H__


#include <cstdint>
#include "xnldr.h"

#pragma pack(push, 1)

typedef struct _IMAGE_DOS_HEADER_ {
	uint16_t e_magic;
	uint16_t e_cblp;
	uint16_t e_cp;
	uint16_t e_crlc;
	uint16_t e_cparhdr;
	uint16_t e_minalloc;
	uint16_t e_maxalloc;
	uint16_t e_ss;
	uint16_t e_sp;
	uint16_t e_csum;
	uint16_t e_ip;
	uint16_t e_cs;
	uint16_t e_lfarlc;
	uint16_t e_ovno;
	uint16_t e_res[4];
	uint16_t e_oemid;
	uint16_t e_oeminfo;
	uint16_t e_res2[10];
	uint16_t e_lfanew;
} IMAGE_DOS_HEADER, * PIMAGE_DOS_HEADER;

typedef struct _IMAGE_NT_HEADERS_PE32_ IMAGE_NT_HEADERS_PE32, * PIMAGE_NT_HEADERS_PE32;
typedef struct _IMAGE_NT_HEADERS_PE32PLUS_  IMAGE_NT_HEADERS_PE32PLYS, * PIMAGE_NT_HEADERS_PE32PLUS;

enum PeMachineType {
	IMAGE_FILE_MACHINE_AMD64 = 0x8664,
	IMAGE_FILE_MACHINE_ARM = 0x1C0,
	IMAGE_FILE_MACHINE_EBC = 0xEBC,
	IMAGE_FILE_MACHINE_I386 = 0x14c,
	IMAGE_FILE_MACHINE_THUMB = 0x1c2,
	IMAGE_FILE_MACHINE_ARM64 = 0xaa64,
	IMAGE_FILE_MACHINE_ARMNT = 0x1c4
};

typedef struct _IMAGE_FILE_HEADER_ {
	uint16_t Machine;
	uint16_t NumberOfSections;
	uint32_t TimeDateStamp;
	uint32_t PointerToSymbolTable;
	uint32_t NumberOfSymbols;
	uint16_t SizeOfOptionaHeader;
	uint16_t Characteristics;
} IMAGE_FILE_HEADER, * PIMAGE_FILE_HEADER;

enum PeOptionalMagic {
	MAGIC_PE32 = 0x10b,
	MAGIC_PE32P = 0x20b
};

typedef struct _IMAGE_DATA_DIRECTORY_ {
	uint32_t VirtualAddress;
	uint32_t Size;
} IMAGE_DATA_DIRECTORY, * PIMAGE_DATA_DIRECTORY;

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16

typedef struct _IMAGE_OPTIONAL_HEADER_PE32_ {
	uint16_t Magic;
	uint8_t  MajorLinkerVersion;
	uint8_t  MinorLinkerVersion;
	uint32_t SizeOfCode;
	uint32_t SizeOfInitializedData;
	uint32_t SizeOfUninitializedData;
	uint32_t AddressOfEntryPoint;
	uint32_t BaseOfCode;
	uint32_t BaseOfData;
	uint32_t ImageBase;
	uint32_t SectionAlighnment;
	uint32_t FileAlignment;
	uint16_t MajorOperatingSystemVersion;
	uint16_t MinorOperatingSystemVersion;
	uint16_t MajorImageVersion;
	uint16_t MinorImageVersion;
	uint16_t MajorSubsystemVersion;
	uint16_t MinorSubsystemVersion;
	uint32_t Reserved1;
	uint32_t SizeOfImage;
	uint32_t SizeOfHeaders;
	uint32_t CheckSum;
	uint16_t Subsystem;
	uint16_t DllCharacteristics;
	uint32_t SizeOfStackReserve;
	uint32_t SizeOfStackCommit;
	uint32_t SizeOfHeapReserve;
	uint32_t SizeOfHeapCommit;
	uint32_t LoaderFlags;
	uint32_t NumberOfRvaAndSizes;
	IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER_PE32, * PIMAGE_OPTIONAL_HEADER_PE32;

typedef struct _IMAGE_OPTIONAL_HEADER_PE32PLUS {

	uint16_t  Magic;				// not-so-magical number
	uint8_t   MajorLinkerVersion;			// linker version
	uint8_t   MinorLinkerVersion;
	uint32_t   SizeOfCode;				// size of .text in bytes
	uint32_t   SizeOfInitializedData;		// size of .bss (and others) in bytes
	uint32_t   SizeOfUninitializedData;		// size of .data,.sdata etc in bytes
	uint32_t   AddressOfEntryPoint;		// RVA of entry point
	uint32_t   BaseOfCode;				// base of .text
	uint64_t   ImageBase;				// image base VA
	uint32_t   SectionAlignment;			// file section alignment
	uint32_t   FileAlignment;			// file alignment
	uint16_t  MajorOperatingSystemVersion;	// Windows specific. OS version required to run image
	uint16_t  MinorOperatingSystemVersion;
	uint16_t  MajorImageVersion;			// version of program
	uint16_t  MinorImageVersion;
	uint16_t  MajorSubsystemVersion;		// Windows specific. Version of SubSystem
	uint16_t  MinorSubsystemVersion;
	uint32_t   Reserved1;
	uint32_t   SizeOfImage;			// size of image in bytes
	uint32_t   SizeOfHeaders;			// size of headers (and stub program) in bytes
	uint32_t   CheckSum;				// checksum
	uint16_t  Subsystem;				// Windows specific. subsystem type
	uint16_t  DllCharacteristics;			// DLL properties
	uint64_t   SizeOfStackReserve;			// size of stack, in bytes
	uint64_t   SizeOfStackCommit;			// size of stack to commit
	uint64_t   SizeOfHeapReserve;			// size of heap, in bytes
	uint64_t   SizeOfHeapCommit;			// size of heap to commit
	uint32_t   LoaderFlags;			// no longer used
	uint32_t   NumberOfRvaAndSizes;		// number of DataDirectory entries
	IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER_PE32PLUS, * PIMAGE_OPTIONAL_HEADER_PE32PLUS;

struct _IMAGE_NT_HEADERS_PE32 {
	uint32_t                 Signature;
	IMAGE_FILE_HEADER     FileHeader;
	IMAGE_OPTIONAL_HEADER_PE32 OptionalHeader;
};

struct _IMAGE_NT_HEADERS_PE32PLUS {
	uint32_t                 Signature;
	IMAGE_FILE_HEADER     FileHeader;
	IMAGE_OPTIONAL_HEADER_PE32PLUS OptionalHeader;
};


#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16


#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000
#define IMAGE_SCN_MEM_NOT_CACHED 0x04000000
#define IMAGE_SCN_MEM_NOT_PAGED 0x08000000
#define IMAGE_SCN_MEM_SHARED 0x10000000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000

typedef struct _IMAGE_SECTION_HEADER {
	char Name[8];
	uint32_t VirtualSize;
	uint32_t VirtualAddress;
	uint32_t SizeOfRawData;
	uint32_t PointerToRawData;
	uint32_t PointerToRelocations;
	uint32_t PointerToLinenumbers;
	uint16_t NumberOfRelocations;
	uint16_t NumberOfLinenumbers;
	uint32_t Characteristics;
}SECTION_HEADER, * PSECTION_HEADER;

#define IMAGE_DATA_DIRECTORY_EXPORT 0
#define IMAGE_DATA_DIRECTORY_IMPORT 1

typedef struct _IMAGE_EXPORT_DIRECTORY {
	uint32_t Characteristics;
	uint32_t TimeDateStamp;
	uint16_t MajorVersion;
	uint16_t MinorVersion;
	uint32_t Name;
	uint32_t Base;
	uint32_t NumberOfFunctions;
	uint32_t NumberOfNames;
	uint32_t AddressOfFunctions;   //export table rva
	uint32_t AddressOfNames;
	uint32_t AddressOfNameOrdinal;  //ordinal table rva
}IMAGE_EXPORT_DIRECTORY, * PIMAGE_EXPORT_DIRECTORY;

typedef struct _IMAGE_IMPORT_DIRECTORY {
	union {
		uint32_t Characteristics;
		uint32_t OriginalFirstThunk;
	};
	uint32_t TimeDateStamp;
	uint32_t ForwarderChain;
	uint32_t NameRva;
	uint32_t ThunkTableRva;
}IMAGE_IMPORT_DIRECTORY, * PIMAGE_IMPORT_DIRECTORY;

typedef struct _IMAGE_RELOCATION_ENTRY_ {
	uint16_t offset : 12;
	uint16_t type : 4;
}IMAGE_RELOCATION_ENTRY, * PIMAGE_RELOCATION_ENTRY;

typedef struct _IMAGE_RELOCATION_BLOCK_ {
	uint32_t PageRVA;
	uint32_t BlockSize;
	IMAGE_RELOCATION_ENTRY entries[0];
}IMAGE_RELOCATION_BLOCK, * PIMAGE_RELOCATION_BLOCK;

typedef struct _IMAGE_IMPORT_HINT_TABLE {
	uint16_t Hint;
	char name[2];
}IMAGE_IMPORT_HINT_TABLE, * PIMAGE_IMPORT_HINT_TABLE;


#define IMAGE_IMPORT_LOOKUP_TABLE_FLAG_PE32 0x80000000
typedef uint32_t IMAGE_IMPORT_LOOKUP_TABLE_PE32, * PIMAGE_IMPORT_LOOKUP_TABLE_PE32;

#define IMAGE_IMPORT_LOOKUP_TABLE_FLAG_PE32P 0x8000000000000000
typedef unsigned long long IMAGE_IMPORT_LOOKUP_TABLE_PE32P, * PIMAGE_IMPORT_LOOKUP_TABLE_PE32P;

#pragma pack(pop)

typedef struct _IMAGE_NT_HEADERS_PE32PLUS IMAGE_NT_HEADERS, * PIMAGE_NT_HEADERS;
typedef IMAGE_IMPORT_LOOKUP_TABLE_PE32  IMAGE_IMPORT_LOOKUP_TABLE, * PIMAGE_IMPORT_LOOKUP_TABLE;
typedef IMAGE_IMPORT_LOOKUP_TABLE_PE32P IMAGE_IMPORT_LOOKUP_TABLE_PE32P, * PIMAGE_IMPORT_LOOKUP_TABLE_PE32P;
static const enum PeOptionalMagic MAGIC_NATIVE = MAGIC_PE32;
static const enum PeMachineType   MACHINE_NATIVE = IMAGE_FILE_MACHINE_I386;
#define IMAGE_IMPORT_LOOKUP_TABLE_FLAG  IMAGE_IMPORT_LOOKUP_TABLE_FLAG_PE32


/*
 * XEPELoadImage -- loads a executable image to memory
 * @param filebuf -- file buffer
 */
extern void* XEPELoadImage(void* filebuf);
/*
 * XEPELoadDLLImage -- load dll image to memory
 @param filebuf -- Pointer to file buffer
 */
extern uint64_t XEPELoadDLLImage(void* filebuf);


/*
 * XEPEGetEntryPoint -- returns the entry point
 * of loaded image
 * @param image -- Pointer to file buffer
 */
extern XEImageEntry XEPEGetEntryPoint(void* image);



#endif