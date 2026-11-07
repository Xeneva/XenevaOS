/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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


#ifndef __XNLDR2_H__
#define __XNLDR2_H__

#include <cstdint>
#include <cstddef>
#ifdef _MSC_VER
#include <Uefi.h>
#include <Guid/DebugImageInfoTable.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Guid/Acpi.h>
#elif __GNUC__
#include <efi/efi.h>
#endif

//#pragma pack(push,1)

/*
 *FRAMEBUFFER_INFORMATION :
 * framebuffer structure
 */
typedef struct _FB_INFO_ {
	uint32_t* phyaddr;
	uint32_t* graphics_framebuffer;
	size_t size;
	uint16_t pixelsPerLine;
	uint16_t X_Resolution;
	uint16_t Y_Resolution;
	uint32_t redmask;
	uint32_t greenmask;
	uint32_t bluemask;
	uint32_t resvmask;
} FRAMEBUFFER_INFORMATION, * PFRAMEBUFFER_INFORMATION;



/* XEBootInfo, Xeneva Boot information 
 * structure passed to the kernel
 */
typedef struct _XE_BOOT_INFO_ {
	int boot_type;
	void* allocated_mem;
	uint64_t reserved_mem_count;
	void* map;
	uint64_t descriptor_size;
	uint64_t mem_map_size;
	uint32_t* graphics_framebuffer;
	size_t   fb_size;
	uint16_t  X_Resolution;
	uint16_t  Y_Resolution;
	uint16_t  pixels_per_line;
	uint32_t redmask;
	uint32_t greenmask;
	uint32_t bluemask;
	uint32_t resvmask;
	void* acpi_table_pointer;
	size_t   kernel_size;
	uint8_t* font_binary_address;
	void (*printf_gui) (const char* text, ...);
	uint8_t* driver_entry1;   //!OTHER
	uint8_t* driver_entry2;   //!NVME
	uint8_t* driver_entry3;   //!AHCI
	uint8_t* driver_entry4;   //!FLOPPY
	uint8_t* driver_entry5;   //!ATA
	uint8_t* driver_entry6;   //!USB
	void* ap_code;

	/*Boot device specific */
	uint32_t hid;
	uint32_t uid;
	uint32_t cid;
}XEBootInfo, *XEPBootInfo;

//#pragma pack(pop)

typedef void(*XEImageEntry)(XEBootInfo*);


struct EfiMemoryMap {
	EFI_MEMORY_DESCRIPTOR* memmap;
	UINT64 MemMapSize, MapKey, DescriptorSize;
	UINT32 DescriptorVersion;
};

extern EFI_HANDLE   gImageHandle;
extern EFI_SYSTEM_TABLE* gSystemTable;
extern EFI_BOOT_SERVICES* gBS;
extern EFI_RUNTIME_SERVICES* gRS;

/*
 * XEAllocatePool -- allocate pool memory
 * @param sz -- size in bytes to allocate
 */
extern void* XEAllocatePool(const uint64_t sz);

/*
 * XEFreePool -- frees up previously allocated
 * pool memory
 * @param Buffer -- Pointer to previously allocated
 * pool memory
 */
extern void XEFreePool(void* Buffer);
#endif