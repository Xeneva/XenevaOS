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

#include "physm.h"
#include "paging.h"
#include "lowlevel.h"
#include "xnldr.h"
#include "xnout.h"
#include <Common.hpp>

void* pdptr;
void* pml4ptr;
size_t  recursive_slot;

static_assert(sizeof(size_t) == 8, "X86 Paging Size Mismatch");

typedef size_t PD_ENTRY;
typedef size_t PTAB_ENTRY;
typedef size_t PDPT_ENTRY;
typedef size_t PML4_ENTRY;




#define PAGING_PRESENT 0x1
#define PAGING_WRITABLE 0x2
#define PAGING_NOCACHE  0x10
#define PAGING_XENEVA_NOSWAP 0x200
#define PAGING_SIZEBIT  0x80
#define PAGING_NXE  0x8000000000000000

static void* make_canonical_i(size_t addr)
{
	if (addr & ((size_t)1 << 47))
		addr |= 0xFFFF000000000000;
	return (void*)addr;
}

static void* make_canonical(void* addr)
{
	return make_canonical_i((size_t)addr);
}

static size_t decanonical(void* addr)
{
	return (size_t)addr & 0x0000FFFFFFFFFFFF;
}


typedef size_t* (*get_tab_ptr)(void*);
typedef size_t(*get_tab_index)(void*);

static PML4_ENTRY* getPML4([[maybe_unused]] void* addr)
{
	if (pml4ptr);
	else {
		void* pml4 = get_paging_root();
		pml4ptr = (void*)((size_t)pml4 & ~(size_t)0xFFF);
	}
	return (PML4_ENTRY*)pml4ptr;
}

static size_t getPML4index(void* addr)
{
	return (decanonical(addr) >> 39) & 0x1FF;
}

static PD_ENTRY* getPD(void* addr)
{
	return (PD_ENTRY*)make_canonical((void*)((recursive_slot << 39) | (recursive_slot << 30) | ((decanonical(addr) >> 18) & 0x3FFFF000)));
}

static PDPT_ENTRY* getPDPT(void* addr)
{
	return (PDPT_ENTRY*)make_canonical((void*)((recursive_slot << 39) | (recursive_slot << 30) | (recursive_slot << 21) | (((decanonical(addr) >> 27) & 0x1FF000))));
}

static size_t getPDindex(void* addr)
{
	return (decanonical(addr) >> 21) & 0x1FF;
}

[[maybe_unused]] static PD_ENTRY* getPDIR( [[maybe_unused]] void* addr)
{
	if (pdptr);
	else
	{
		void* pd = get_paging_root();
		pdptr = (void*)((size_t)pd & ~(size_t)0xFFF);
	}
	return (PD_ENTRY*)pdptr;
}


[[maybe_unused]] static size_t getPDIRindex(void* addr)
{
	return ((size_t)addr >> 21) & 0x3FF;
}

[[maybe_unused]] static PTAB_ENTRY* getPTAB(void* addr)
{
	return (PD_ENTRY*)make_canonical((void*)((recursive_slot << 39) | ((decanonical(addr) >> 9) & 0x7FFFFFF000)));
}


static size_t getPTABindex(void* addr)
{
	return (decanonical(addr) >> 12) & 0x1FF;
}

static size_t getPDPTindex(void* addr)
{
	return (decanonical(addr) >> 30) & 0x1FF;
}


static get_tab_ptr get_tab_dispatch[] =
{
	nullptr,
	&getPTAB,
	&getPD,
	&getPDPT,
	&getPML4
};

static get_tab_index get_index_dispatch[] = {
	nullptr,
	&getPTABindex,
	&getPDindex,
	&getPDPTindex,
	&getPML4index
};


static PML4_ENTRY* setPML4_recursive(PML4_ENTRY* pml4, size_t slot)
{
	recursive_slot = slot;
	pml4[recursive_slot] = (size_t)pml4 | PAGING_PRESENT | PAGING_WRITABLE;
	//now calculate new pml4 address
	void* new_pml4 = make_canonical((void*)((recursive_slot << 39) | (recursive_slot << 30) | (recursive_slot << 21) | (recursive_slot << 12)));
	//flush the tlb
	set_paging_root((uint64_t)get_paging_root());
	pml4ptr = new_pml4;
	return (PML4_ENTRY*)new_pml4;
}

#pragma pack(1)


struct mapping_table
{
	size_t entries[512];
};

#pragma pack()
uint64_t pml4e[512];

#define PAGE_ADDR_MASK  0x000ffffffffff000

void identity_map_4kb(uint64_t logical)
{
	int flags = PAGING_PRESENT | PAGING_WRITABLE;

	int pml4_index = (logical >> 39) & 0x1ff;
	int pdp_idx = (logical >> 30) & 0x1ff;
	int pd_idx = (logical >> 21) & 0x1ff;
	int pt_idx = (logical >> 12) & 0x1ff;
	[[maybe_unused]] int p_idx = logical & 0x7ff;

	if (!(pml4e[pml4_index] & PAGING_PRESENT))
	{
		uint64_t pdpt_alloc = (uint64_t)XEPmmngrAllocate();

		//memset((void*)pdpt_alloc, 0, 4096);

		pml4e[pml4_index] = (pdpt_alloc & PAGE_ADDR_MASK) | flags;

		identity_map_4kb(pdpt_alloc);
	}

	//printf_gui("PML4 Entry setupped\n");
	mapping_table* pdpt = (mapping_table*)(pml4e[pml4_index] & PAGE_ADDR_MASK);

	if (!(pdpt->entries[pdp_idx] & PAGING_PRESENT))
	{
		uint64_t pdt_alloc = (uint64_t)XEPmmngrAllocate();
		//memset((void*)pdt_alloc, 0, 4096);
		pdpt->entries[pdp_idx] = (pdt_alloc & PAGE_ADDR_MASK) | flags;
		identity_map_4kb(pdt_alloc);
		//printf_gui("PDPT Entry setupped\n");
	}

	mapping_table* pdt = (mapping_table*)(pdpt->entries[pdp_idx] & PAGE_ADDR_MASK);
	if (!(pdt->entries[pd_idx] & PAGING_PRESENT))
	{
		uint64_t pt_alloc = (uint64_t)XEPmmngrAllocate();
		//memset((void*)pt_alloc, 0, 4096);
		pdt->entries[pd_idx] = (pt_alloc & PAGE_ADDR_MASK) | flags;
		identity_map_4kb(pt_alloc);

	}

	mapping_table* pt = (mapping_table*)(pdt->entries[pd_idx] & PAGE_ADDR_MASK);

	if (!(pt->entries[pt_idx] & PAGING_PRESENT))
	{
		pt->entries[pt_idx] = (logical & PAGE_ADDR_MASK) | flags;

	}

}

/*
 * XEInitialisePaging -- initialise paging 
 */
void XEInitialisePaging() {
	pml4ptr = 0;
	pdptr = 0;
	recursive_slot = 0;

	PML4_ENTRY* pml4 = getPML4(nullptr);

	size_t cr0 = read_cr0();
	write_cr0(cr0 & (0xffffffff - (1 << 16)));

	for (size_t x = (PAGESIZE / sizeof(PML4_ENTRY)); x > 0; --x){
		if ((pml4[x - 1] & PAGING_PRESENT) == 0){
			pml4 = setPML4_recursive(pml4, x - 1);
			break;
		}
	}

	write_cr0(cr0);
}

static void clear_ptabs(void* addr)
{

	PTAB_ENTRY* pt = (PTAB_ENTRY*)addr;
	for (size_t n = 0; n < PAGESIZE / sizeof(PTAB_ENTRY); ++n)
	{
		pt[n] = 0;
	}
}

#define PAGING_USER (1<<2)



bool XEPagingMap_(void* vaddr, paddr_t paddr, size_t attributes){
	size_t flags = PAGING_PRESENT | PAGING_WRITABLE | attributes;

	PML4_ENTRY* pml4 = (PML4_ENTRY*)getPML4(vaddr);
	PDPT_ENTRY* pdpt = getPDPT(vaddr);
	PD_ENTRY* pdir = getPD(vaddr);
	PTAB_ENTRY* ptab = getPTAB(vaddr);

	PML4_ENTRY& pml4ent = pml4[getPML4index(vaddr)];
	if ((pml4ent & PAGING_PRESENT) == 0) {

		paddr_t addr = XEPmmngrAllocate();
		if (addr == 0)
		{
			return false;
		}
		pml4ent = addr | flags;
		tlbflush(pdpt);
		memory_barrier();
		clear_ptabs(pdpt);
	}
	else if (pml4ent & PAGING_SIZEBIT) {
		return false;
	}
	PDPT_ENTRY& pdptent = pdpt[getPDPTindex(vaddr)];
	if ((pdptent & PAGING_PRESENT) == 0)
	{
		paddr_t addr = XEPmmngrAllocate();
		if (addr == 0)
		{
			return false;
		}
		pdptent = addr | flags; //PAGING_PRESENT | PAGING_WRITABLE; 
		tlbflush(pdir);
		memory_barrier();
		clear_ptabs(pdir);
	}
	else if (pdptent & PAGING_SIZEBIT)
		return false;
	PD_ENTRY& pdent = pdir[getPDindex(vaddr)];
	if ((pdent & PAGING_PRESENT) == 0)
	{
		paddr_t addr = XEPmmngrAllocate();
		if (addr == 0)
		{
			return false;
		}
		pdent = addr | flags; // PAGING_PRESENT | PAGING_WRITABLE; 
		tlbflush(ptab);
		memory_barrier();
		clear_ptabs(ptab);
	}
	else if (pdent & PAGING_SIZEBIT)
		return false;
	PTAB_ENTRY& ptabent = ptab[getPTABindex(vaddr)];
	if ((ptabent & PAGING_PRESENT) != 0)
		return false;
	if (paddr == PADDR_T_MAX)
	{
		if (!(paddr = XEPmmngrAllocate()))
			return false;
	}
	ptabent = (size_t)paddr | flags; //PAGING_PRESENT; 
	/*if (attributes & PAGE_ATTRIBUTE_WRITABLE)
		ptabent |= PAGING_WRITABLE;*/
	tlbflush(vaddr);
	memory_barrier();
	return true;
}

/*
 * check_free -- check free memory area
 */
bool check_free(int level, void* start_addr, void* end_addr){

	if (level == 0)
		return false;
	size_t* paging_entry = get_tab_dispatch[level](start_addr);
	get_tab_index getindex = get_index_dispatch[level];

	size_t cur_addr = (size_t)start_addr;
	size_t end_index = getindex(end_addr);
	if (get_tab_dispatch[level](end_addr) != get_tab_dispatch[level](start_addr))
		end_index = 0x1FF;
	for (size_t pindex = getindex(start_addr); pindex <= end_index; ++pindex){
		if ((paging_entry[pindex] & PAGING_PRESENT) == 0){
			cur_addr >>= (level * 9 + 3);
			cur_addr += 1;
			cur_addr <<= (level * 9 + 3);
			continue;
		}
		else{
			if (!check_free((level - 1), (void*)cur_addr, end_addr))
				return false;
		}
	}
	return true;
}

/*
 * XEPagingMap -- maps range of physical memory to virtual memory
 * @param vaddr -- virtual address start
 * @param paddr -- Physical address start
 * @param length -- length of the area
 * @param attributes -- paging attributes to set
 */
bool XEPagingMap(void* vaddr, paddr_t paddr, size_t length, size_t attributes) {

	if (!check_free(vaddr, length)) 
		return false;
	
	size_t vptr = (size_t)vaddr;
	size_t pgoffset = vptr & (PAGESIZE - 1);
	vaddr = (void*)(vptr ^ pgoffset);
	length += pgoffset;
	if (paddr != PADDR_T_MAX)
	{
		if ((paddr & (PAGESIZE - 1)) != pgoffset)
			return false;
		paddr ^= pgoffset;
	}

	for (size_t i = 0; i < (length + PAGESIZE - 1) / PAGESIZE; ++i) {

		if (!XEPagingMap_(vaddr, paddr, attributes))
			return false;

		vaddr = raw_offset<void*>(vaddr, PAGESIZE);
		if (paddr != PADDR_T_MAX)
			paddr = raw_offset<paddr_t>(paddr, PAGESIZE);
	}
	return true;
}


void set_paging_attributes(void* vaddr, size_t length, size_t attrset, size_t attrclear){

	PTAB_ENTRY* ptab = getPTAB(vaddr);
	for (size_t index = getPTABindex(vaddr); index < (length + PAGESIZE - 1) / PAGESIZE; ++index){
		if ((attrset & PAGE_ATTRIBUTE_WRITABLE) != 0){
			ptab[index] |= PAGING_WRITABLE;
		}

		if ((attrclear & PAGE_ATTRIBUTE_WRITABLE) != 0){
			ptab[index] &= ~((size_t)PAGING_WRITABLE);
		}
		if ((attrset & PAGE_ATTRIBUTE_NO_EXECUTE) != 0){
			ptab[index] = PAGING_NXE;
		}
		if ((attrclear & PAGE_ATTRIBUTE_NO_EXECUTE) != 0){
			ptab[index] &= ~((size_t)PAGING_NXE);
		}

		if ((attrset & PAGE_ATTRIBUTE_NO_CACHING) != 0){
			ptab[index] |= PAGING_NOCACHE;
		}
		if ((attrclear & PAGE_ATTRIBUTE_NO_CACHING) != 0){
			ptab[index] &= ~((size_t)PAGING_NOCACHE);
		}
		if ((attrset & PAGE_ATTRIBUTE_NO_PAGING) != 0){
			ptab[index] |= PAGING_XENEVA_NOSWAP;
		}
		if ((attrclear & PAGE_ATTRIBUTE_NO_PAGING) != 0){
			ptab[index] &= ~((size_t)PAGING_XENEVA_NOSWAP);
		}
	}
}


static struct {
	size_t recursive_slot;
	void* pml4ptr;
}paging_info;


size_t arch_get_recursive_slot() {
	return recursive_slot;
}

void* arch_get_pml4ptr() {
	return pml4ptr;
}


void fill_arch_paging_info([[maybe_unused]] void* info)
{
	info = &paging_info;
	paging_info.recursive_slot = recursive_slot;
	paging_info.pml4ptr = pml4ptr;
}


bool check_free(void* vaddr, size_t length)
{
	void* endaddr = raw_offset<void*>(vaddr, length - 1);
	return check_free(4, vaddr, endaddr);
}



