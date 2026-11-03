/**
* @file vmmngr.c
* 
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

#include <Mm/vmmngr.h>
#include <aucon.h>
#include <Mm/pmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <string.h>
#include <kernelAA64.h>
#include <_null.h>
#include <Drivers/uart.h>
#include <Hal/AA64/profile.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif

uint64_t* _RootPaging;
uint64_t* _RootPagingKe;
uint64_t* _MMIOBase;

static size_t pml4_index(uint64_t virt) {
	uint64_t l0_index = (virt >> 39) & 0x1FF;
	return l0_index;
}

static size_t pdpt_index(uint64_t virt) {
	uint64_t l1_index = (virt >> 30) & 0x1FF;
	return l1_index;
}

static size_t pd_index(uint64_t virt) {
	uint64_t l2_index = (virt >> 21) & 0x1FF;
	return l2_index;
}

static size_t pt_index(uint64_t virt) {
	uint64_t l3_index = (virt >> 12) & 0x1FF;
	return l3_index;
}

static int isRangeInsideKernel(uint64_t va) {
	return va >= 0xFFFF000000000000ULL;
}

#define VMM_PHYS_MASK 0x0000FFFFFFFFF000ULL

static uint64_t* vmm_current_root(uint64_t virt_addr) {
	uint64_t phys = isRangeInsideKernel(virt_addr) ? read_ttbr1_el1() : read_ttbr0_el1();
	return (uint64_t*)P2V(phys & VMM_PHYS_MASK);
}

bool AuIsVirtualAddressSpaceActive(uint64_t* root) {
	return root && (read_ttbr0_el1() & VMM_PHYS_MASK) == V2P((uint64_t)root);
}

/* Lookup only: unmapping a hole must never allocate page tables. Block
 * descriptors are not child tables and must not be followed as pointers. */
static uint64_t* vmm_get_leaf(uint64_t* root, uint64_t virt_addr) {
	if (!root)
		return NULL;
	const size_t indices[] = {pml4_index(virt_addr), pdpt_index(virt_addr), pd_index(virt_addr)};
	uint64_t* table = root;
	for (size_t level = 0; level < 3; ++level) {
		uint64_t desc = table[indices[level]];
		if ((desc & (PTE_VALID | PTE_TABLE)) != (PTE_VALID | PTE_TABLE))
			return NULL;
		table = (uint64_t*)P2V(desc & VMM_PHYS_MASK);
	}
	return &table[pt_index(virt_addr)];
}

/* a table has to be fully initialized and visible to the page-table walker
 * before its parent descriptor gets published. cleaning only the parent
 * PTE wouldve left stale contents sitting in a newly allocated child table
 * on non-coherent table walks --axiss */
static bool vmm_prepare_table(uint64_t page) {
	void* table;
	/* AuPmmngrAllocPage returns PMM_INVALID_PHYS when the free list is
	 * empty. P2V of that value is FFFF7FFFFFFFFFFF, one byte under the
	 * direct map, and memset of it is a kernel data abort. */
	if (page == PMM_INVALID_PHYS)
		return false;
	table = (void*)P2V(page);
	memset(table, 0, 4096);
	aa64_data_cache_clean_range(table, 4096);
	dsb_ish();
	return true;
}

/**
 * @brief AuVmmngrInitialize -- initialize the virtual memory manager
 */
void AuVmmngrInitialize() {
	AuTextOut("[aurora]: initializing virtual memory manager \r\n");
	/* XNLDR installs the direct physical map while it still owns the
	 * inherited firmware root, so i leave that active root alone here --axiss */
	uint64_t* userRoot = (uint64_t*)read_ttbr0_el1();
	uint64_t* kernelRoot = (uint64_t*)read_ttbr1_el1();
	/* accessing the live root through the just-installed direct map here,
	 * since the inherited low alias might point at a different firmware
	 * translation --axiss */
	uint64_t* liveKernelRoot = (uint64_t*)(PHYSICAL_MEM_BASE + ((uint64_t)kernelRoot & ~0xFFFULL));
	liveKernelRoot[pml4_index(KERNEL_BASE_ADDRESS)] = 0;
	aa64_data_cache_clean_range(&liveKernelRoot[pml4_index(KERNEL_BASE_ADDRESS)], sizeof(uint64_t));
	tlb_flush_vmalle1is();

	_RootPaging = userRoot;
	_RootPagingKe = kernelRoot;

	AuPmmngrMoveHigher();
	UARTDebugOut("[vmm]: direct map online\r\n");

	_MMIOBase = (uint64_t*)MMIO_BASE;
}

/**
 * @brief AuMapPage -- Maps a virtual page to physical frame
 * @param phys_addr -- physical address
 * @param virt_addr -- virtual address
 * @param attrib -- Page attributes
 * @return 1 on success 0 on failure
 */
bool AuMapPage(uint64_t phys_addr, uint64_t virt_addr, uint8_t attrib) {
	uint64_t flags = PTE_VALID | PTE_TABLE | PTE_AF | PTE_SH_INNER | PTE_AP_RW | attrib;
	if (attrib & PTE_AP_RW_USER) {
		flags = PTE_VALID | PTE_TABLE | PTE_AF | PTE_SH_INNER | attrib;
	}

	const long i4 = (virt_addr >> 39) & 0x1FF;
	const long i3 = (virt_addr >> 30) & 0x1FF;
	const long i2 = (virt_addr >> 21) & 0x1FF;
	const long i1 = (virt_addr >> 12) & 0x1FF;

	uint64_t* pml4i = vmm_current_root(virt_addr);

	if (!(pml4i[i4] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page)) {
			UARTDebugOut("[vmm]: out of pages for L0 va=%x\r\n", virt_addr);
			return false;
		}
		pml4i[i4] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE;
		void* address = &pml4i[i4];
		/* only this one 8-byte entry got dirtied, not the whole parent
		 * table page. cleaning all 4096 bytes here was flushing 63 clean
		 * cache lines nobody even touched, every single time a new table
		 * level got created during boot --axiss */
		aa64_data_cache_clean_range(address, sizeof(uint64_t));
	}
	uint64_t* pml3 = (uint64_t*)P2V((pml4i[i4] & ~0xFFFULL));

	if (!(pml3[i3] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page)) {
			UARTDebugOut("[vmm]: out of pages for L1 va=%x\r\n", virt_addr);
			return false;
		}
		pml3[i3] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE;
		void* address = &pml3[i3];
		/* same as the PML4 case above, only this entry is dirty --axiss */
		aa64_data_cache_clean_range(address, sizeof(uint64_t));
	}

	uint64_t pml3e = pml3[i3];
	/* a table descriptor carries a physical address, catching caller/table
	 * corruption here before P2V wraps a high virtual value into FFFF7... --axiss */
	if ((pml3e & PTE_VALID) && (pml3e & 0x0000FF0000000000ULL)) {
		UARTDebugOut("[vmm]: invalid L1 descriptor va=%x desc=%x\r\n", virt_addr, pml3e);
		return false;
	}
	if ((pml3e & 0x3) == PTE_VALID) {
		UARTDebugOut("[vmm]: L1 block cannot be split va=%x desc=%x\r\n", virt_addr, pml3e);
		return false;
	}
	uint64_t* pml2 = (uint64_t*)P2V((pml3e & ~0xFFFULL));

	if (!(pml2[i2] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page)) {
			UARTDebugOut("[vmm]: out of pages for L2 va=%x\r\n", virt_addr);
			return false;
		}
		pml2[i2] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE;
		void* address = &pml2[i2];
		/* same as the PML4 case above, only this entry is dirty --axiss */
		aa64_data_cache_clean_range(address, sizeof(uint64_t));
	}

	uint64_t pml2e = pml2[i2];
	if ((pml2e & PTE_VALID) && (pml2e & 0x0000FF0000000000ULL)) {
		UARTDebugOut("[vmm]: invalid L2 descriptor va=%x desc=%x\r\n", virt_addr, pml2e);
		return false;
	}
	if ((pml2e & 0x3) == PTE_VALID) {
		UARTDebugOut("[vmm]: L2 block cannot be split va=%x desc=%x\r\n", virt_addr, pml2e);
		return false;
	}
	uint64_t* pml1 = (uint64_t*)P2V((pml2e & ~0xFFFULL));

	if (pml1[i1] & 1) {
		AuTextOut("[aurora]: vmmngr page already present : virt=%x phys=%x \r\n",
				  virt_addr,
				  (pml1[i1] & ~0xFFFULL));
		UARTDebugOut("[aurora]: vmmngr page already present : virt=%x phys=%x \r\n",
					 virt_addr,
					 (pml1[i1] & ~0xFFFULL));
		return false;
	}

	pml1[i1] = (phys_addr & ~0xFFFULL) | flags;
	virt_addr &= ~0xFFFULL;
	void* address = &pml1[i1];
	/* a single leaf PTE changed here, so i clean just its cache line before
	 * invalidating the corresponding translation. cleaning the whole 4 KiB
	 * table made every ordinary page map pay for 64 cache lines it didnt
	 * need to --axiss */
	aa64_data_cache_clean_range(address, sizeof(uint64_t));
	dsb_ish();
	isb_flush();

	tlb_flush(virt_addr);
	return true;
}

/**
* @brief AuMapPageEx -- Maps a virtual page to physical frame in given
* page level
* @param pml4i -- root page level pointer
* @param phys_addr -- physical address
* @param virt_addr -- virtual address
* @param attrib -- Page attributes
* @return 1 on success, 0 on failure
*/
bool AuMapPageEx(uint64_t* pml4i, uint64_t phys_addr, uint64_t virt_addr, uint8_t attrib) {
	uint64_t flags = PTE_VALID | PTE_TABLE | PTE_AF | PTE_SH_INNER | PTE_AP_RW | attrib;
	if (attrib & PTE_AP_RW_USER) {
		flags = PTE_VALID | PTE_TABLE | PTE_AF | PTE_SH_INNER | PTE_AP_RW_USER | attrib;
	}

	const long i4 = (virt_addr >> 39) & 0x1FF;
	const long i3 = (virt_addr >> 30) & 0x1FF;
	const long i2 = (virt_addr >> 21) & 0x1FF;
	const long i1 = (virt_addr >> 12) & 0x1FF;

	if (!(pml4i[i4] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page))
			return false;
		pml4i[i4] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
		aa64_data_cache_clean_range(&pml4i[i4], sizeof(uint64_t));
	}
	uint64_t* pml3 = (uint64_t*)P2V((pml4i[i4] & ~0xFFFULL));

	if (!(pml3[i3] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page))
			return false;
		pml3[i3] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
		aa64_data_cache_clean_range(&pml3[i3], sizeof(uint64_t));
	}

	uint64_t* pml2 = (uint64_t*)P2V((pml3[i3] & ~0xFFFULL));

	if (!(pml2[i2] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page))
			return false;
		pml2[i2] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
		aa64_data_cache_clean_range(&pml2[i2], sizeof(uint64_t));
	}

	uint64_t* pml1 = (uint64_t*)P2V((pml2[i2] & ~0xFFFULL));
	if (pml1[i1] & 1) {
		AuTextOut("[aurora]: vmmngr page already present : virt=%x phys=%x \n",
				  virt_addr,
				  (pml1[i1] & ~0xFFFULL));
		UARTDebugOut("[aurora]: vmmngr page already present : virt=%x phys=%x \r\n",
					 virt_addr,
					 (pml1[i1] & ~0xFFFULL));
		return false;
	}

	pml1[i1] = (phys_addr & ~0xFFFULL) | flags;
	return true;
}

/**
 * @breif AuVmmngrGetPage -- Returns virtual page from virtual address
 * in AuVPage format
 * @param virt_addr -- Virtual address
 * @param _flags -- extra virtual page flags, this is set only if
 * mode is set to VMMNGR_GETPAGE_CREATE
 * @param mode -- specifies whether to create a virtual page if its
 * not present
 * @return virtual page from virtual address
 * in AuVPage format
 */
AuVPage* AuVmmngrGetPage(uint64_t virt_addr, uint8_t _flags, uint8_t mode) {
	if (mode & VIRT_GETPAGE_ONLY_RET) {
		uint64_t* leaf = vmm_get_leaf(vmm_current_root(virt_addr), virt_addr);
		return leaf && (*leaf & PTE_VALID) ? (AuVPage*)leaf : NULL;
	}
	uint64_t flags = PTE_VALID | PTE_TABLE | PTE_AF | PTE_SH_INNER | PTE_AP_RW | _flags;
	if (_flags & PTE_AP_RW_USER) {
		flags = PTE_VALID | PTE_TABLE | PTE_AF | PTE_SH_INNER | PTE_AP_RW_USER | _flags;
	}

	const long i4 = (virt_addr >> 39) & 0x1FF;
	const long i3 = (virt_addr >> 30) & 0x1FF;
	const long i2 = (virt_addr >> 21) & 0x1FF;
	const long i1 = (virt_addr >> 12) & 0x1FF;

	uint64_t* pml4i = vmm_current_root(virt_addr);

	if (!(pml4i[i4] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page))
			return NULL;
		pml4i[i4] = page | flags;
		aa64_data_cache_clean_range(&pml4i[i4], sizeof(uint64_t));
	}
	uint64_t* pml3 = (uint64_t*)P2V(pml4i[i4] & ~0xFFFULL);

	if (!(pml3[i3] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page))
			return NULL;
		pml3[i3] = page | flags;
		aa64_data_cache_clean_range(&pml3[i3], sizeof(uint64_t));
	}

	uint64_t* pml2 = (uint64_t*)P2V(pml3[i3] & ~0xFFFULL);

	if (!(pml2[i2] & 1)) {
		const uint64_t page = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		if (!vmm_prepare_table(page))
			return NULL;
		pml2[i2] = page | flags;
		aa64_data_cache_clean_range(&pml2[i2], sizeof(uint64_t));
	}

	uint64_t* pml1 = (uint64_t*)P2V(pml2[i2] & ~0xFFFULL);
	if (pml1[i1] & PTE_VALID)
		return (AuVPage*)&pml1[i1];
	if (!(mode & VIRT_GETPAGE_CREATE))
		return NULL;
	uint64_t phys_addr = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	if (phys_addr == PMM_INVALID_PHYS)
		return NULL;
	memset((void*)P2V(phys_addr), 0, PAGE_SIZE);
	pml1[i1] = (phys_addr & VMM_PHYS_MASK) | flags;
	data_cache_flush(&pml1[i1]);
	tlb_flush(VIRT_ADDR_ALIGN(virt_addr));
	return (AuVPage*)&pml1[i1];
}

/**
 * @brief AuMapMMIO -- Maps Memory Mapped I/O addresses
 * @param phys_addr -- MMIO physical address
 * @param page_count -- number of pages
 * @return Pointer to newly mapped virtual mmio address
 */
void* AuMapMMIO(uint64_t phys_addr, size_t page_count) {
	uint64_t out = (uint64_t)_MMIOBase;
	for (size_t i = 0; i < page_count; i++)
		AuMapPage(phys_addr + i * 4096, out + i * 4096, PTE_DEVICE_MEM);

	uint64_t address = out;
	_MMIOBase = (uint64_t*)(address + (page_count * 4096));
	return (void*)out;
}

/**
* @brief AuGetFreePage -- Checks for free page
* @param user -- specifies if it needs to look from
* user base address
* @param ptr -- if it is non-null, than lookup
* begins from given pointer
* @return pointer to free virtual page
*/
uint64_t* AuGetFreePage(bool user, void* ptr) {
	uint64_t start = 0;
	if (user) {
		if (ptr)
			start = (uint64_t)ptr;
		else
			start = USER_BASE_ADDRESS;
	} else {
		if (ptr)
			start = (uint64_t)ptr;
		else
			start = KERNEL_BASE_ADDRESS;
	}

	uint64_t* pml4 = (uint64_t*)P2V(read_ttbr0_el1());
	if (user == 0)
		pml4 = (uint64_t*)P2V(read_ttbr1_el1());

	if (ptr) {
		if (isRangeInsideKernel((uint64_t)ptr))
			pml4 = (uint64_t*)P2V(read_ttbr1_el1());
		else
			pml4 = (uint64_t*)P2V(read_ttbr0_el1());
	}

	/* Walk through every page tables */
	for (;;) {
		if (!(pml4[pml4_index(start)] & 1))
			return (uint64_t*)start;

		uint64_t* pdpt = (uint64_t*)P2V(pml4[pml4_index(start)] & ~0xFFFUL);
		if (!(pdpt[pdpt_index(start)] & 1))
			return (uint64_t*)start;

		uint64_t* pd = (uint64_t*)P2V(pdpt[pdpt_index(start)] & ~0xFFFUL);
		if (!(pd[pd_index(start)] & 1))
			return (uint64_t*)start;

		uint64_t* pt = (uint64_t*)P2V(pd[pd_index(start)] & ~0xFFFUL);

		if (!(pt[pt_index(start)] & 1))
			return (uint64_t*)start;

		start += 4096;
	}
}

/**
 * @brief AuFreePages -- frees up contiguous pages
 * @param virt_addr -- starting virtual address
 * @param free_physical -- free up physical frame
 * @param size_t s -- size of area to be freed
 */
void AuFreePages(uint64_t virt_addr, bool free_physical, size_t s) {
	AuFreePagesEx(vmm_current_root(virt_addr), virt_addr, free_physical, s);
}

void AuFreePagesEx(uint64_t* root, uint64_t virt_addr, bool free_physical, size_t size) {
	size_t num_pages = size / PAGE_SIZE + (size % PAGE_SIZE != 0);
	for (size_t i = 0; i < num_pages; ++i, virt_addr += PAGE_SIZE) {
		uint64_t* leaf = vmm_get_leaf(root, virt_addr);
		if (!leaf || !(*leaf & PTE_VALID))
			continue;
		uint64_t phys = *leaf & VMM_PHYS_MASK;
		*leaf = 0;
		aa64_data_cache_clean_range(leaf, sizeof(*leaf));
		dsb_ish();
		tlb_flush(virt_addr);
		dsb_ish();
		isb_flush();
		if (free_physical && phys)
			AuPmmngrReleasePage(phys);
	}
}

/* The root has already been detached and the TLB invalidated, so none of
 * these private tables can be reached by a page-table walker any longer. */
static void vmm_destroy_table(uint64_t phys, unsigned level) {
	uint64_t* table = (uint64_t*)P2V(phys);
	for (size_t i = 0; i < 512; ++i) {
		uint64_t desc = table[i];
		if (!(desc & PTE_VALID))
			continue;
		uint64_t child_phys = desc & VMM_PHYS_MASK;
		if (!(desc & PTE_TABLE))
			continue; /* Block mappings are borrowed, not private allocations. */
		if (level < 3)
			vmm_destroy_table(child_phys, level + 1);
		else if ((desc & (7ULL << 2)) != PTE_DEVICE_MEM &&
				 AuPmmngrGetBackingBlock(child_phys) == -1)
			AuPmmngrReleasePage(child_phys);
	}
	AuPmmngrReleasePage(phys);
}

bool AuDestroyVirtualAddressSpace(uint64_t* root) {
	if (!root || AuIsVirtualAddressSpaceActive(root))
		return false;

	/* AuCreateVirtualAddressSpace borrows entries 256..511 from the kernel.
	 * Only entries 0..255, their tables, and the root itself belong to us. */
	uint64_t private_entries[256];
	memcpy(private_entries, root, sizeof(private_entries));
	memset(root, 0, sizeof(private_entries));
	aa64_data_cache_clean_range(root, sizeof(private_entries));
	dsb_ish();
	tlb_flush_vmalle1is();
	dsb_ish();
	isb_flush();
	for (size_t i = 0; i < 256; ++i) {
		uint64_t desc = private_entries[i];
		if ((desc & (PTE_VALID | PTE_TABLE)) == (PTE_VALID | PTE_TABLE))
			vmm_destroy_table(desc & VMM_PHYS_MASK, 1);
	}
	AuPmmngrReleasePage(V2P((uint64_t)root));
	return true;
}

/**
 * @brief AuFreePages -- frees up contiguous pages
 * @param virt_addr -- starting virtual address
 * @param flags -- flags to update
 */
void AuUpdatePageFlags(uint64_t virt_addr, uint64_t flags) {
	uint64_t* pml4_ = (uint64_t*)P2V(read_ttbr0_el1());

	uint64_t* pdpt = (uint64_t*)P2V(pml4_[pml4_index(virt_addr)] & ~0xFFFUL);
	uint64_t* pd = (uint64_t*)P2V(pdpt[pdpt_index(virt_addr)] & ~0xFFFUL);
	uint64_t* pt = (uint64_t*)P2V(pd[pd_index(virt_addr)] & ~0xFFFUL);
	uint64_t* page = (uint64_t*)P2V(pt[pt_index(virt_addr)] & ~0xFFFUL);

	if (page) {
		pt[pt_index(virt_addr)] = (V2P(*page) & ~0xFFFULL) | flags;
		data_cache_flush(&pt[pt_index(virt_addr)]);
	}
}

/**
 * @brief AuGetPhysicalAddress -- returns the physical address
 * from a virtual address
 * @param virt_addr -- Virtual address 
 * @return the physical address of respected virtual address
 */
void* AuGetPhysicalAddress(uint64_t virt_addr) {
	uint64_t* pml4_ = vmm_current_root(virt_addr);

	if ((pml4_[pml4_index(virt_addr)] & 1) == 0)
		return NULL;

	uint64_t* pdpt = (uint64_t*)P2V(pml4_[pml4_index(virt_addr)] & ~0xFFFUL);

	if ((pdpt[pdpt_index(virt_addr)] & 1) == 0)
		return NULL;

	uint64_t* pd = (uint64_t*)P2V(pdpt[pdpt_index(virt_addr)] & ~0xFFFUL);

	if ((pd[pd_index(virt_addr)] & 1) == 0)
		return NULL;

	uint64_t* pt = (uint64_t*)P2V(pd[pd_index(virt_addr)] & ~0xFFFUL);

	if ((pt[pt_index(virt_addr)] & 1) == 0)
		return NULL;

	uint64_t* page = (uint64_t*)P2V(pt[pt_index(virt_addr)] & ~0xFFFUL);

	if (page)
		return (void*)V2P((uint64_t)page);
	return NULL;
}

/**
 * @brief AuGetPhysicalAddressEx -- returns the physical address
 * from a virtual address
 * @param virt_addr -- Virtual address
 * @return the physical address of respected virtual address
 */
void* AuGetPhysicalAddressEx(uint64_t* pml4_, uint64_t virt_addr) {
	if ((pml4_[pml4_index(virt_addr)] & 1) == 0)
		return NULL;

	uint64_t* pdpt = (uint64_t*)P2V(pml4_[pml4_index(virt_addr)] & ~0xFFFUL);

	if ((pdpt[pdpt_index(virt_addr)] & 1) == 0)
		return NULL;

	uint64_t* pd = (uint64_t*)P2V(pdpt[pdpt_index(virt_addr)] & ~0xFFFUL);

	if ((pd[pd_index(virt_addr)] & 1) == 0)
		return NULL;

	uint64_t* pt = (uint64_t*)P2V(pd[pd_index(virt_addr)] & ~0xFFFUL);

	if ((pt[pt_index(virt_addr)] & 1) == 0)
		return NULL;

	uint64_t* page = (uint64_t*)P2V(pt[pt_index(virt_addr)] & ~0xFFFUL);

	if (page)
		return (void*)V2P((uint64_t)page);
	return NULL;
}

/**
 * @brief AuCreateVirtualAddressSpace -- create a new virtual address space
 * @return pointer to newly created address space
 */
uint64_t* AuCreateVirtualAddressSpace() {
	uint64_t* root_pml = (uint64_t*)P2V((size_t)_RootPagingKe);
	uint64_t* new_pml = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(new_pml, 0, PAGE_SIZE);

	for (int i = 0; i < 512; i++) {
		if (i < 256)
			continue;
		if (root_pml[i] & 1)
			new_pml[i] = root_pml[i];
		else
			new_pml[i] = 0;
	}

	return new_pml;
}

uint64_t* AuGetRootPageTable() {
	return (uint64_t*)P2V((uint64_t)_RootPaging);
}

/**
 * @breif AuVmmngrBootFree -- free up the lower half of
 *  kernel address space
 */
void AuVmmngrBootFree() {
	uint64_t* cr3 = (uint64_t*)_RootPaging;
	dsb_ish();
	isb_flush();

	for (int i = 0; i < 256; i++)
		cr3[i] = 0;
	aa64_data_cache_clean_range((void*)P2V((uint64_t)cr3), 256 * sizeof(uint64_t));
	dsb_ish();

	write_ttbr0_el1(_RootPaging);
	tlb_flush_vmalle1is();
}

/**
 * @brief AuVMmngrUserWalk -- user memory pte check function
 * @param va -- virtual address
 * @param write -- write bit
 * @param span -- bytes from va to the end of mappings
 */
bool AuVmmngrUserWalk(uint64_t va, bool write, uint64_t* span) {
#define PTE_APT_NOUSER (1ULL << 61)
#define PTE_APT_RO	   (1ULL << 62)
#define PTE_AP_USER	   (1ULL << 6)
#define PTE_AP_RO	   (1ULL << 7)

	uint64_t ttbr0;
	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));

	uint64_t* tbl = P2V(ttbr0 & 0x0000FFFFFFFFF000ULL);

	for (int level = 0; level <= 3; level++) {
		unsigned shift = 39 - 9 * level;
		uint64_t pte = tbl[(va >> shift) & 0x1FF];

		if (!(pte & PTE_VALID))
			return false;

		if (level < 3 && (pte & PTE_TABLE)) {
			if (pte & PTE_APT_NOUSER)
				return false;
			if (write && (pte & PTE_APT_RO))
				return false;
			tbl = P2V(pte & 0x0000FFFFFFFFF000ULL);
			continue;
		}

		if (level == 0)
			return false;
		if (level == 3 && !(pte & PTE_TABLE))
			return false;

		if (!(pte & PTE_AF))
			return false;
		if (!(pte & PTE_AP_USER))
			return false;
		if (write && (pte & PTE_AP_RO))
			return false;

		uint64_t size = 1ULL << shift;
		*span = size - (va & (size - 1));
		return true;
	}
	return false;
}