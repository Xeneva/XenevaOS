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

#ifndef __PAGING_H__
#define __PAGING_H__

#include <stdint.h>

/* memory attributes*/
#define PAGE_TABLE_ENTRY_PRESENT (1UL << 0)
#define PAGE_TABLE_ENTRY_BLOCK	 (0UL << 1)
#define PAGE_TABLE_ENTRY_PAGE	 (1UL << 1)
#define PAGE_TABLE_ENTRY_AF		 (1UL << 10)
#define PAGE_TABLE_ENTRY_SH		 (3UL << 8)
#define PAGE_TABLE_ENTRY_SH_NONE (0UL << 8)
#define PAGE_TABLE_ENTRY_AP_RW	 (0UL << 6)
#define PAGE_TABLE_ENTRY_MEMATTR (1UL << 2)
#define PAGE_TABLE_ENTRY_PXN	 (1ULL << 53)
#define PAGE_TABLE_ENTRY_UXN	 (1ULL << 54)
#define PAGE_TABLE_ENTRY_DEVICE	 (0ULL << 2)

#define PAGE_FLAGS                                                                                 \
	(PAGE_TABLE_ENTRY_PRESENT | PAGE_TABLE_ENTRY_PAGE | PAGE_TABLE_ENTRY_AF |                      \
	 PAGE_TABLE_ENTRY_SH | PAGE_TABLE_ENTRY_AP_RW | PAGE_TABLE_ENTRY_MEMATTR);

/*
 * XEPagingInitialize -- initialize paging
 */
extern void XEPagingInitialize();

/*
 * XEPagingMap -- maps a physical address to virtual address
 * @param virtualAddr -- virtual address
 * @param physAddr -- physical address
 */
extern void XEPagingMap(uint64_t virtualAddr, uint64_t physAddr);
extern void XEPagingInstallPhysicalDirectMap();

/* Checks if a virtual page is already mapped to prevent overwriting during PE section overlapping */
extern bool XEPagingIsMapped(uint64_t virtualAddr);

extern void XEMapMMIO(uint64_t* l1_table, uint64_t va, uint64_t pa);

extern void XEPagingCopy();
#endif
