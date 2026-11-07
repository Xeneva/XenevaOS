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

#include "littleboot.h"

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
}__attribute__((packed)) IMAGE_DOS_HEADER;

typedef struct _IMAGE_FILE_HEADER_ {
	uint16_t Machine;
	uint16_t NumberOfSections;
	uint32_t TimeDateStamp;
	uint32_t PointerToSymbolTable;
	uint32_t NumberOfSymbols;
	uint16_t SizeOfOptionaHeader;
	uint16_t Characteristics;
}__attribute__((packed)) IMAGE_FILE_HEADER, * PIMAGE_FILE_HEADER;

enum PeOptionalMagic {
	MAGIC_PE32 = 0x10b,
	MAGIC_PE32P = 0x20b
};

typedef struct _IMAGE_DATA_DIRECTORY_ {
	uint32_t VirtualAddress;
	uint32_t Size;
}__attribute__((packed)) IMAGE_DATA_DIRECTORY, * PIMAGE_DATA_DIRECTORY;

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
}__attribute__((packed)) IMAGE_OPTIONAL_HEADER_PE32, * PIMAGE_OPTIONAL_HEADER_PE32;

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
}__attribute__((packed)) IMAGE_OPTIONAL_HEADER_PE32PLUS, * PIMAGE_OPTIONAL_HEADER_PE32PLUS;

typedef struct _IMAGE_NT_HEADERS_PE32PLUS {
	uint32_t                 Signature;
	IMAGE_FILE_HEADER     FileHeader;
	IMAGE_OPTIONAL_HEADER_PE32PLUS OptionalHeader;
}__attribute__((packed)) IMAGE_NT_HEADERS;

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
}__attribute__((packed)) IMAGE_SECTION_HEADER, * PSECTION_HEADER;

#endif
