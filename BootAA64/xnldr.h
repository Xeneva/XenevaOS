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

#include <stdint.h>
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <stddef.h>
#include <aurora.h>
#ifndef SIZE_MAX
#if defined(ARCH_ARM64) || defined(ARCH_X64) || defined(_M_AMD64) || defined(_M_ARM64) ||          \
	defined(__x86_64__) || defined(__aarch64__)
#define SIZE_MAX 0xFFFFFFFFFFFFFFFFULL
#else
#define SIZE_MAX 0xFFFFFFFFULL
#endif
#endif
//#include <stddef.h>

#ifdef __TARGET_BOARD_RPI3__
#define MMIO_BASE 0x3F000000
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
} FRAMEBUFFER_INFORMATION, *PFRAMEBUFFER_INFORMATION;

/* I intentionally have the loader and kernel share one hand-off structure, better not fragment it --axiss */
typedef KERNEL_BOOT_INFO XEBootInfo;
typedef KERNEL_BOOT_INFO* XEPBootInfo;

//#pragma pack(pop)

typedef void (*XEImageEntry)(XEBootInfo*);

struct EfiMemoryMap {
	EFI_MEMORY_DESCRIPTOR* memmap;
	UINTN MemMapSize;
	UINTN MapKey;
	UINTN DescriptorSize;
	UINT32 DescriptorVersion;
};

extern EFI_HANDLE gImageHandle;
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

extern void ASCIIToChar16(const char* src, wchar_t* dst);
extern void Char16ToASCII(char* dest, CHAR16* src);
#endif
