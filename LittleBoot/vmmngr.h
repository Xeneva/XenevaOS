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

#ifndef __VMMNGR_H__
#define __VMMNGR_H__

#include "littleboot.h"

#define PTE_VALID (1ULL << 0)
#define PTE_TABLE (1ULL << 1)
#define PTE_BLOCK (0ULL << 1)
#define PTE_AF    (1ULL << 10)
#define PTE_SH_INNER (3ULL << 8)
#define PTE_AP_RW   (0ULL << 6)
#define PTE_ATTR_IDX_0 (0ULL << 2)
#define PTE_ATTR_IDX_1 (1ULL << 2)
#define PTE_SH_NONE (0 << 8)
#define PTE_DEVICE PTE_ATTR_IDX_0
#define PTE_NORMAL PTE_ATTR_IDX_1

/* kernel physical direct map, same base XNLDR uses (BaseHdr PHYSICAL_MEM_BASE) */
#define LB_DIRECT_MAP_BASE 0xFFFF800000000000ULL
/* direct map covers L1[0..1]: SDRAM, peripherals, ARM local peripherals */
#define LB_DIRECT_MAP_SIZE 0x80000000ULL

/*
 * LBPagingInitialize -- build identity + direct map, enable MMU and caches
 * @param ram_end -- end of ARM SDRAM
 */
extern void LBPagingInitialize(uint64_t ram_end);

/*
 * LBCacheSyncICache -- clean D-cache to PoU and invalidate I-cache
 */
extern void LBCacheSyncICache(uint64_t start, uint64_t end);

/*
 * LBCacheCleanRange -- clean D-cache to PoC
 */
extern void LBCacheCleanRange(uint64_t start, uint64_t end);

/*
 * LBPagingMap -- maps a virtual address to its physical
 * address
 * @param virtual -- virtual address to map
 * @param physical -- physical address to map
 */
extern void LBPagingMap(uint64_t virtual, uint64_t physical);

extern void LBPageTableWalk(uint64_t virtual);

/*
 * LBPagingFree -- free up a mapping
 * @param virt_addr -- virtual address to free
 * @param free_physical -- boolean value whether to free up
 * the physical address or not
 */
extern void LBPagingFree(uint64_t virt_addr, bool free_physical);

#endif