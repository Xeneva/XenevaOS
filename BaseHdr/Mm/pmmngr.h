/**
* @file pmmngr.h
* 
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#ifndef __PMMNGR_H__
#define __PMMNGR_H__

#include <aurora.h>


#define AURORA_PAGE_KERNEL  (1ULL<<0)
#define AURORA_PAGE_SHM     (1ULL<<1)
#define AURORA_PAGE_DMA     (1ULL<<2)
#define AURORA_PAGE_NORMAL (1ULL << 3)

/* ---- Stage 1: per-owner page accounting ----
 * Owner id space: 0 = kernel / untracked, >0 = proc_id, <0 = reserved
 * for subsystem tags. Query with AuPmmOwnerPages(); check at teardown
 * with AuPmmOwnerTeardownCheck(). Allocation passes the owner through
 * the ForOwner variants; the classic entry points always use
 * PMM_OWNER_KERNEL and are otherwise unchanged. */
#define PMM_OWNER_KERNEL 0
AU_EXTERN AU_EXPORT uint64_t AuPmmngrAllocPageForOwner(uint8_t page_type, int32_t owner);
AU_EXTERN AU_EXPORT uint64_t AuPmmngrAllocPagesForOwner(uint32_t pages,
	uint32_t alignment_pages, uint64_t max_phys_inclusive, uint8_t page_type,
	int32_t owner);
AU_EXTERN AU_EXPORT uint64_t AuPmmOwnerPages(int32_t owner);
extern void AuPmmOwnerTeardownCheck(int32_t owner, const char* tag);
#ifdef __XENEVA_DEBUG_ALLOC__
/* Stage 3 (debug-only): walk the frame table and print every block
 * still tagged to the owner. Called from AuPmmOwnerTeardownCheck on
 * failure; also usable from the debugger. */
extern void AuPmmOwnerScan(int32_t owner);
#endif
extern void AuPmmngrInitialize(KERNEL_BOOT_INFO *info);

#define PMM_INVALID_PHYS UINT64_MAX

typedef struct _au_pmm_stats_ {
	uint64_t managed_pages;
	uint64_t free_pages;
	uint64_t allocated_pages;
	uint64_t reserved_pages;
	uint64_t unmanaged_pages;
} AuPmmStats;

#ifdef ARCH_ARM64
AU_EXTERN AU_EXPORT uint64_t AuPmmngrAllocPage(uint8_t page_type);
AU_EXTERN AU_EXPORT uint64_t AuPmmngrAllocPages(uint32_t pages,
	uint32_t alignment_pages, uint64_t max_phys_inclusive, uint8_t page_type);
AU_EXTERN AU_EXPORT bool AuPmmngrReleasePage(uint64_t physaddr);
AU_EXTERN AU_EXPORT bool AuPmmngrReleasePages(uint64_t physaddr);
AU_EXTERN AU_EXPORT bool AuPmmngrRetainPage(uint64_t physaddr);
AU_EXTERN AU_EXPORT uint16_t AuPmmngrPageRefcount(uint64_t physaddr);
AU_EXTERN AU_EXPORT bool AuPmmngrSetBackingBlock(uint64_t physaddr, int64_t block);
AU_EXTERN AU_EXPORT int64_t AuPmmngrGetBackingBlock(uint64_t physaddr);
AU_EXTERN AU_EXPORT void AuPmmngrGetStats(AuPmmStats* stats);
extern bool AuPmmngrValidate(void);
#else
typedef struct _au_page_desc_ {
	uint64_t phys_addr;
	uint16_t refcount;
	uint64_t last_accessed;
	uint8_t page_type;
	int64_t diskblock;
} AuPageDesc;
AU_EXTERN AU_EXPORT void* AuPmmngrAlloc();
AU_EXTERN AU_EXPORT void* AuPmmngrAllocBlocks(int num);
AU_EXTERN AU_EXPORT void AuPmmngrFree(void* Address);
AU_EXTERN AU_EXPORT void AuPmmngrFreeBlocks(void* Addr, int Count);
extern uint64_t AuPmmngrGetFreeMem();
extern uint64_t AuPmmngrGetUsedMem();
extern uint64_t AuPmmngrGetTotalMem();
extern void AuPmmngrAddRefcount(uint64_t physaddr, uint16_t count);
extern uint16_t AuPmmngrGetRefcount(uint64_t physaddr);
extern AuPageDesc* AuPmmngrGetPageDesc(uint64_t physaddr);
extern void AuPmmngrSetPageType(uint64_t physaddr, uint8_t flags);
#define AuPmmngrAllocPage(type) ((uint64_t)AuPmmngrAlloc())
#define AuPmmngrAllocPages(pages, alignment, ceiling, type) \
	((uint64_t)AuPmmngrAllocBlocks((int)(pages)))
#define AuPmmngrReleasePage(physaddr) \
	(AuPmmngrFree((void*)(uint64_t)(physaddr)), true)
#endif

/**
 * @brief P2V -- Physical to Virtual conversion
 * @param addr -- Address to convert
 */
AU_EXTERN AU_EXPORT uint64_t P2V(uint64_t addr);

/**
 * @brief V2P -- Virtual to Physical conversion
 * @param vaddr -- Address to convert
 */
AU_EXTERN AU_EXPORT uint64_t V2P(uint64_t vaddr);

/**
* @brief AuPmmngrMoveHigher -- moves the kernel to higher half
* of memory
*/
extern void AuPmmngrMoveHigher();
#endif
