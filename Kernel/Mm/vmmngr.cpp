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

#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <aucon.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/x86_64_cpu.h>
#include <string.h>
#include <_null.h>
#include <Hal/serial.h>

uint64_t *_RootPaging;
uint64_t *_MmioBase;

#define CANONICAL_MASK  0xFFFFffffFFFFUL

static void* x86_64_make_canonical_i(size_t addr){
	if (addr & ((size_t)1 << 47))
		addr |= 0xFFFF000000000000;
	return (void*)addr;
}

static void* x86_64_make_canonical(void* addr)
{
	return x86_64_make_canonical_i((size_t)addr);
}

size_t  x86_64_pml4_index(uint64_t addr){
	return (addr >> 39) & 0x1ff;
}

size_t x86_64_pdp_index(uint64_t addr){
	return (addr >> 30) & 0x1ff;
}

size_t x86_64_pd_index(uint64_t addr){
	return (addr >> 21) & 0x1ff;
}

size_t x86_64_pt_index(uint64_t addr){
	return (addr >> 12) & 0x1ff;
}

size_t x86_64_p_index(uint64_t addr){
	return (addr & 0x7ff);
}


/*
 * Au_x86_64_Paging_Init -- Initialise x86_64 paging and setup
 * kernel required pagings
 */
void Au_x86_64_Paging_Init() {
	uint64_t *OldCR3 = (uint64_t*)x64_read_cr3();
	uint64_t *NewCR3 = (uint64_t*)AuPmmngrAlloc();
	if (NewCR3)
		AuTextOut("NewCR3-> %x \n", NewCR3);
	memset(NewCR3, 0, 4096);
	
	for (int i = 0; i < 512; ++i) {
		if (i == 511)
			continue;

		if ((OldCR3[i] & X86_64_PAGING_PRESENT))
			NewCR3[i] = OldCR3[i];
		else
			NewCR3[i] = 0;

	}

	uint64_t* PDPT = (uint64_t*)AuPmmngrAlloc();
	memset(PDPT, 0, 4096);

	NewCR3[x86_64_pml4_index(PHYSICAL_MEM_BASE)] = (uint64_t)PDPT | X86_64_PAGING_PRESENT | X86_64_PAGING_WRITABLE;

	for (size_t i = 0; i < 512; i++)
		PDPT[x86_64_pdp_index(PHYSICAL_MEM_BASE) + i] = i * 512 * 512 * 4096 | 0x80 | X86_64_PAGING_PRESENT | X86_64_PAGING_WRITABLE;


	x64_write_cr3((size_t)NewCR3);

	_RootPaging = NewCR3;

	AuPmmngrMoveHigher();

	_MmioBase = (uint64_t*)MMIO_BASE;
}

/*
 * AuVmmngrGetPage -- Returns virtual page from virtual address
 * in AuVPage format
 * @param virt_addr -- Virtual address
 * @param _flags -- extra virtual page flags, this is set only if 
 * mode is set to VMMNGR_GETPAGE_CREATE
 * @param mode -- specifies whether to create a virtual page if its
 * not present
 */
AuVPage * AuVmmngrGetPage(uint64_t virt_addr, uint8_t _flags, uint8_t mode) {
	uint8_t flags = X86_64_PAGING_PRESENT | X86_64_PAGING_WRITABLE | _flags;


	const long i4 = (virt_addr >> 39) & 0x1FF;
	const long i3 = (virt_addr >> 30) & 0x1FF;
	const long i2 = (virt_addr >> 21) & 0x1FF;
	const long i1 = (virt_addr >> 12) & 0x1FF;

	uint64_t *pml4i = (uint64_t*)P2V(x64_read_cr3());

	if (!(pml4i[i4] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml4i[i4] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml4i);
		x64_mfence();
	}
	uint64_t* pml3 = (uint64_t*)(P2V(pml4i[i4]) & ~(4096 - 1));

	if (!(pml3[i3] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml3[i3] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml3);
		x64_mfence();

	}


	uint64_t* pml2 = (uint64_t*)(P2V(pml3[i3]) & ~(4096 - 1));

	if (!(pml2[i2] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml2[i2] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml2);
		x64_mfence();

	}

	uint64_t* pml1 = (uint64_t*)(P2V(pml2[i2]) & ~(4096 - 1));
	if (pml1[i1] & X86_64_PAGING_PRESENT)
	{
		AuVPage *page = (AuVPage*)&pml1[i1];
		return page;
	}
	else {
		if (mode & VIRT_GETPAGE_CREATE && !(mode & VIRT_GETPAGE_ONLY_RET)) {
			uint64_t phys_addr = (uint64_t)AuPmmngrAlloc();
			memset((void*)P2V(phys_addr), 0, 4096);
			pml1[i1] = phys_addr | flags;
			flush_tlb((void*)virt_addr);
			x64_mfence();
			AuVPage *vpage = (AuVPage*)&pml1[i1];
			SeTextOut("Creating vpage \r\n");
			return vpage;
		}
		if (mode & VIRT_GETPAGE_ONLY_RET)
			return NULL;
	}
}


/*
 * AuMapPage -- Maps a virtual page to physical frame
 * @param phys_addr -- physical address
 * @param virt_addr -- virtual address
 * @param attrib -- Page attributes
 */
bool AuMapPage(uint64_t phys_addr, uint64_t virt_addr, uint8_t attrib) {
	uint8_t flags = X86_64_PAGING_PRESENT | X86_64_PAGING_WRITABLE | attrib;

	const long i4 = (virt_addr >> 39) & 0x1FF;
	const long i3 = (virt_addr >> 30) & 0x1FF;
	const long i2 = (virt_addr >> 21) & 0x1FF;
	const long i1 = (virt_addr >> 12) & 0x1FF;

	uint64_t *pml4i = (uint64_t*)P2V(x64_read_cr3());

	if (!(pml4i[i4] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml4i[i4] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml4i);
		x64_mfence();
	}
	uint64_t* pml3 = (uint64_t*)(P2V(pml4i[i4]) & ~(4096 - 1));

	if (!(pml3[i3] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml3[i3] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml3);
		x64_mfence();

	}


	uint64_t* pml2 = (uint64_t*)(P2V(pml3[i3]) & ~(4096 - 1));

	if (!(pml2[i2] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml2[i2] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml2);
		x64_mfence();

	}

	uint64_t* pml1 = (uint64_t*)(P2V(pml2[i2]) & ~(4096 - 1));
	if (pml1[i1] & X86_64_PAGING_PRESENT)
	{
		//AuPmmngrFree((void*)phys_addr);
		return false;
	}

	pml1[i1] = phys_addr | flags;
	flush_tlb((void*)pml1);
	x64_mfence();
	return true;
}


/*
* AuMapPageEx -- Maps a virtual page to physical frame in given
* page level
* @param pml4i -- root page level pointer
* @param phys_addr -- physical address
* @param virt_addr -- virtual address
* @param attrib -- Page attributes
*/
bool AuMapPageEx(uint64_t *pml4i, uint64_t phys_addr, uint64_t virt_addr, uint8_t attrib) {
	uint8_t flags = X86_64_PAGING_PRESENT | X86_64_PAGING_WRITABLE | attrib;

	const long i4 = (virt_addr >> 39) & 0x1FF;
	const long i3 = (virt_addr >> 30) & 0x1FF;
	const long i2 = (virt_addr >> 21) & 0x1FF;
	const long i1 = (virt_addr >> 12) & 0x1FF;


	if (!(pml4i[i4] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml4i[i4] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml4i);
		x64_mfence();
	}
	uint64_t* pml3 = (uint64_t*)(P2V(pml4i[i4]) & ~(4096 - 1));

	if (!(pml3[i3] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml3[i3] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml3);
		x64_mfence();

	}


	uint64_t* pml2 = (uint64_t*)(P2V(pml3[i3]) & ~(4096 - 1));

	if (!(pml2[i2] & X86_64_PAGING_PRESENT))
	{
		const uint64_t page = (uint64_t)AuPmmngrAlloc();
		pml2[i2] = page | flags;
		memset((void*)P2V(page), 0, 4096);
		flush_tlb((void*)pml2);
		x64_mfence();

	}

	uint64_t* pml1 = (uint64_t*)(P2V(pml2[i2]) & ~(4096 - 1));
	if (pml1[i1] & X86_64_PAGING_PRESENT)
	{
		AuPmmngrFree((void*)phys_addr);
		return false;
	}

	pml1[i1] = phys_addr | flags;
	flush_tlb((void*)pml1);
	x64_mfence();
	return true;
}

/*
 * AuMapMMIO -- Maps Memory Mapper I/O addresses
 * @param phys_addr -- MMIO physical address
 * @param page_count -- number of pages
 */
void* AuMapMMIO(uint64_t phys_addr, size_t page_count) {
	uint64_t out = (uint64_t)_MmioBase;
	for (size_t i = 0; i < page_count; i++)
		AuMapPage(phys_addr + i * 4096, out + i * 4096, 0x04 | 0x80000 | 0x200000);

	uint64_t address = out;
	_MmioBase = (uint64_t*)(address + (page_count * 4096));
	return (void*)out;
}


/*
* AuGetFreePage -- Checks for free page
* @param user -- specifies if it needs to look from
* user base address
* @param ptr -- if it is non-null, than lookup
* begins from given pointer
*/
uint64_t* AuGetFreePage(bool user, void* ptr) {
	uint64_t* page = 0;
	uint64_t start = 0;
	if (user) {
		if (ptr)
			start = (uint64_t)ptr;
		else
			start = USER_BASE_ADDRESS;
	}
	else {
		if (ptr)
			start = (uint64_t)ptr;
		else
			start = KERNEL_BASE_ADDRESS;
	}

	uint64_t* end = 0;
	uint64_t *pml4 = (uint64_t*)P2V(x64_read_cr3());

	/* Walk through every page tables */
	for (;;) {
		if (!(pml4[x86_64_pml4_index(start)] & X86_64_PAGING_PRESENT))
			return (uint64_t*)start;

		uint64_t *pdpt = (uint64_t*)(P2V(pml4[x86_64_pml4_index(start)]) & ~(4096 - 1));
		if (!(pdpt[x86_64_pdp_index(start)] & X86_64_PAGING_PRESENT))
			return (uint64_t*)start;

		uint64_t *pd = (uint64_t*)(P2V(pdpt[x86_64_pdp_index(start)]) & ~(4096 - 1));
		if (!(pd[x86_64_pd_index(start)] & X86_64_PAGING_PRESENT))
			return (uint64_t*)start;

		uint64_t *pt = (uint64_t*)(P2V(pd[x86_64_pd_index(start)]) & ~(4096 - 1));

		if (!(pt[x86_64_pt_index(start)] & X86_64_PAGING_PRESENT))
			return (uint64_t*)start;

		start += 4096;
	}
	return 0;
}

/*
 * AuFreePages -- frees up contiguous pages
 * @param virt_addr -- starting virtual address
 * @param free_physical -- free up physical frame
 * @param size_t s -- size of area to be freed
 */
void AuFreePages(uint64_t virt_addr, bool free_physical, size_t s){
	const long i1 = x86_64_pml4_index(virt_addr);

	for (int i = 0; i < (s / 4096) + 1; i++) {
		uint64_t *pml4_ = (uint64_t*)P2V(x64_read_cr3());
		uint64_t *pdpt = (uint64_t*)(P2V(pml4_[x86_64_pml4_index(virt_addr)]) & ~(4096 - 1));
		uint64_t *pd = (uint64_t*)(P2V(pdpt[x86_64_pdp_index(virt_addr)]) & ~(4096 - 1));
		uint64_t *pt = (uint64_t*)(P2V(pd[x86_64_pd_index(virt_addr)]) & ~(4096 - 1));
		uint64_t *page = (uint64_t*)(P2V(pt[x86_64_pt_index(virt_addr)]) & ~(4096 - 1));

		if ((pt[x86_64_pt_index(virt_addr)] & X86_64_PAGING_PRESENT) != 0) {
			pt[x86_64_pt_index(virt_addr)] = 0;
		}

		if (free_physical && page != 0) {
			AuPmmngrFree((void*)V2P((size_t)page));
		}
		
		virt_addr = virt_addr + static_cast<uint64_t>(i) * 4096;
	}

}


/*
* AuGetPhysicalAddress -- translates logical address
* to its physical address
* @param virt_addr -- virtual address
*/
void* AuGetPhysicalAddress(uint64_t virt_addr){

	const long i1 = x86_64_pml4_index(virt_addr);

	uint64_t *pml4_ = (uint64_t*)P2V(x64_read_cr3());
	uint64_t *pdpt = (uint64_t*)(P2V(pml4_[x86_64_pml4_index(virt_addr)]) & ~(4096 - 1));
	uint64_t *pd = (uint64_t*)(P2V(pdpt[x86_64_pdp_index(virt_addr)]) & ~(4096 - 1));
	uint64_t *pt = (uint64_t*)(P2V(pd[x86_64_pd_index(virt_addr)]) & ~(4096 - 1));
	uint64_t *page = (uint64_t*)(P2V(pt[x86_64_pt_index(virt_addr)]) & ~(4096 - 1));

	if (page)
		return page;
	else
		return NULL;

}

/*
* AuGetPhysicalAddressEx -- translates logical address
* to its physical address
* @param cr3 -- Page level governor
* @param virt_addr -- virtual address 
*/
void* AuGetPhysicalAddressEx(uint64_t* cr3, uint64_t virt_addr){

	const long i1 = x86_64_pml4_index(virt_addr);

	uint64_t *pml4_ = cr3;
	uint64_t *pdpt = (uint64_t*)(P2V(pml4_[x86_64_pml4_index(virt_addr)]) & ~(4096 - 1));
	uint64_t *pd = (uint64_t*)(P2V(pdpt[x86_64_pdp_index(virt_addr)]) & ~(4096 - 1));
	uint64_t *pt = (uint64_t*)(P2V(pd[x86_64_pd_index(virt_addr)]) & ~(4096 - 1));
	uint64_t *page = (uint64_t*)(P2V(pt[x86_64_pt_index(virt_addr)]) & ~(4096 - 1));

	if (page)
		return page;
	else
		return NULL;

}

/*
 * AuCreateVirtualAddressSpace -- creates a new virtual address
 * space and return the new address space in a linear virtual
 * address form
 */
uint64_t* AuCreateVirtualAddressSpace() {
	uint64_t* root_cr3 = (uint64_t*)P2V((size_t)_RootPaging);
	uint64_t* new_cr3 = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
	memset(new_cr3, 0, PAGE_SIZE);

	for (int i = 0; i < 512; i++) {
		if (i < 256)
			continue;
		if (root_cr3[i] & X86_64_PAGING_PRESENT)
			new_cr3[i] = root_cr3[i];
		else
			new_cr3[i] = 0;
	}

	return new_cr3;
}

/*
 * AuGetRootPageTable -- returns the root
 * page table 
 */
uint64_t* AuGetRootPageTable() {
#ifdef ARCH_X64
	return _RootPaging;
#endif
}

/*
 * AuVmmngrInitialize -- initialize the virtual
 * memory manager
 */
void AuVmmngrInitialize() {
#ifdef ARCH_X64
	Au_x86_64_Paging_Init();
#endif
}


/*
 * AuVmmngrBootFree -- free up the lower half
 */
void AuVmmngrBootFree() {
#ifdef ARCH_X64
	uint64_t* cr3 = (uint64_t*)x64_read_cr3();
	for (int i = 0; i < 256; i++)
		cr3[i] = 0;
	x64_write_cr3((uint64_t)cr3);
#endif
}

/*
 * AuVmmngrCloneAddressSpace -- clones a given address space
 * @param destcr3 -- destination cr3
 * @param srccr3 -- source cr3
 */
void AuVmmngrCloneAddressSpace(uint64_t *destcr3, uint64_t* srccr3){
	for (int i = 0; i < 256; ++i) {
		if ((srccr3[i] & X86_64_PAGING_PRESENT)) {
			/* allocate a new pdp */
			uint64_t* pdp_new = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
			memset(pdp_new, 0, PAGE_SIZE);
			destcr3[i] = V2P((size_t)pdp_new) | X86_64_PAGING_PRESENT | X86_64_PAGING_USER;
			uint64_t *pdp_in = (uint64_t*)(P2V(srccr3[i]) & ~(4096 - 1));
			/* inside page directory pointer */
			for (int j = 0; j < 512; ++j) {
				if ((pdp_in[j] & X86_64_PAGING_PRESENT)) {
					uint64_t* pd_new = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
					memset(pd_new, 0, PAGE_SIZE);
					pdp_new[j] = V2P((size_t)pd_new) | X86_64_PAGING_PRESENT | X86_64_PAGING_USER;
					uint64_t* pd_in = (uint64_t*)(P2V(pdp_in[j]) & ~(4096 - 1));

					/* inside page directory*/
					for (int k = 0; k < 512; ++k) {
						if ((pd_in[k] & X86_64_PAGING_PRESENT)) {
							uint64_t* pt_new = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
							memset(pt_new, 0, PAGE_SIZE);
							pd_new[k] = V2P((size_t)pt_new) | X86_64_PAGING_PRESENT | X86_64_PAGING_USER;
							uint64_t* pt_in = (uint64_t*)(P2V(pd_in[k]) & ~(4096 - 1));

							for (int m = 0; m < 512; ++m) {
								if ((pt_in[m] & X86_64_PAGING_PRESENT)) {
									size_t addr = ((i << (9 * 3 + 12)) | (j << (9 * 2 + 12)) | (k << (9 + 12)) |
										(m << PAGE_SHIFT));
									uint64_t *page = (uint64_t*)(P2V(pt_in[m]) & ~(4096 - 1));
									/* here clone the pages to new pages */
									uint64_t* new_page = (uint64_t*)P2V((size_t)AuPmmngrAlloc());
									memcpy(new_page, page, PAGE_SIZE);
									pt_new[m] = V2P((size_t)new_page) | X86_64_PAGING_PRESENT | X86_64_PAGING_WRITABLE | X86_64_PAGING_USER;
									
								}
							}
						}
					}
				}
			}
		}
	}
}

