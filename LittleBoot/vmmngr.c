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
#include "vmmngr.h"
#include "string.h"
#include "physm.h"

static uint64_t l0_table[512] __attribute__((aligned(0x1000)));
static uint64_t l1_table[512] __attribute__((aligned(0x1000)));

#define MMU_FLAGS (PTE_AF | PTE_SH_INNER | PTE_ATTR_IDX_0 | PTE_BLOCK)

size_t pml4_index(uint64_t virt){
    uint64_t l0_index = (virt >> 39) & 0x1FF;
    return l0_index;
}

size_t pdpt_index(uint64_t virt){
    uint64_t l1_index = (virt >> 30) & 0x1FF;
    return l1_index;
}

size_t pd_index(uint64_t virt){
    uint64_t l2_index = (virt >> 21) & 0x1FF;
    return l2_index;
}

size_t pt_index(uint64_t virt){
    uint64_t l3_index = (virt >> 12) & 0x1FF;
    return l3_index;
}

void LBMapMMIO(uint64_t va, uint64_t pa){
    uint64_t l1_index = pdpt_index(va);
    l1_table[l1_index] = (pa & ~((1ULL << 30) - 1)) | 
    PTE_VALID | PTE_BLOCK | PTE_AP_RW | PTE_SH_NONE | PTE_DEVICE | PTE_AF;
    asm volatile ("dc civac, %0"::"r"(&l1_table[l1_index]):"memory");
    asm volatile("dsb ish");
    asm volatile ("isb");
}

/*
 * LBPagingInitialize -- initialize paging
 */
void LBPagingInitialize() {
    LBUartPutString("L0 size: ");
    LBUartPrintInt(sizeof(l0_table));
    LBUartPutString("\r\n");
    memset(l0_table, 0,4096);
    memset(l1_table, 0,4096);

    l0_table[0] = ((uint64_t)l1_table) | PTE_VALID | PTE_TABLE | PTE_AF;


    for (int i = 0; i < 512; ++i){
        uint64_t addr = i << 30;
        l1_table[i] = (addr | PTE_VALID | PTE_BLOCK | PTE_AF | PTE_SH_INNER | (1ULL<<2));
        asm volatile ("dc civac, %0"::"r"(&l1_table[i]):"memory");
        asm volatile("dsb ish");
        asm volatile ("isb");
    }

#ifdef RASPBERRY_PI
    for (int i = 0; i < 16*1024*1024/0x1000; i++)
        LBMapMMIO(0x3F000000 + i * 0x1000, 0x3F000000 + i * 0x1000);

    for (int i = 0; i < 1024*1024 / 0x1000; i++)
        LBMapMMIO(0x40000000 + i * 0x1000, 0x40000000 + i * 0x1000);
#endif


    uint64_t mair = 0x000000000044ff00;
    asm volatile ("msr mair_el1,%0"::"r"(mair));

    uint64_t tcr = ((16UL << 0) | (0b00UL << 14) | (0b11UL << 12) |
        (0b01UL << 10) | (0b01UL << 8) | (12UL << 16) | (0b10 << 30) | 
        (0b11UL << 28) | (0b01UL << 26) | (0b01UL << 24) | (4UL << 32));
    /*0 | 
    (3UL << 32) | 
    (2UL << 30) | 
    (16UL << 16) | 
    (3UL << 28) | 
    (1UL << 26) | 
    (1UL << 24) | 
    (0UL << 14) | 
    (16UL << 0) | 
    (3UL << 12) | 
    (1UL << 10) | 
    (1UL << 8) ;*/
   
    __asm__ volatile ("msr tcr_el1,%0"::"r"(tcr));
    
    __asm__ volatile ("msr ttbr0_el1,%0" :: "r"(&l0_table));
    __asm__ volatile ("msr ttbr1_el1,%0":: "r"(&l0_table));
    asm volatile ("dsb ishst\ntlbi vmalle1is\ndsb ish\nisb":::"memory");
    asm volatile ("isb":::"memory");

    LBUartPutString("Paging initialized upto here $$\n");


    uint64_t sctlr = (1UL<<0) | (1UL<<2) | (1UL<<12) | (1UL << 23) | 
     (1UL << 28) | (1UL << 29)|
    (1UL << 20) | (1UL << 7);
    asm volatile ("msr sctlr_el1,%0"::"r"(sctlr));
    asm volatile ("isb":::"memory");

    LBUartPutString("Paging setup completed -- \n");
    LBUartPutString("Paging l0 table :");
    LBUartPrintHex((uint64_t)l0_table);
    LBUartPutString("Paging ttbr0_el1 : ");
    uint64_t ttbr0 = 0;
    asm volatile ("mrs %0,ttbr0_el1\n": "=r"(ttbr0));
    LBUartPrintHex(ttbr0);
}


typedef union _VPage_{
    struct {
        uint64_t present : 1;
        uint64_t table_page : 1;
        uint64_t attrindex : 3;
        uint64_t ns : 1;
        uint64_t ap : 2;
        uint64_t sh: 2;
        uint64_t af : 1;
        uint64_t ng : 1;
        uint64_t page: 36;
        uint64_t reserved : 4;
        uint64_t contiguous : 1;
        uint64_t pxn : 1;
        uint64_t uxn : 1;
        uint64_t avail : 4;
        uint64_t ignored : 5;
    }bits;

    struct {
        uint64_t valid : 1;
        uint64_t table : 1;
        uint64_t next : 46;
        uint64_t reserved : 4;
        uint64_t ignored : 7;
        uint64_t pxntable : 1;
        uint64_t xntable : 1;
        uint64_t aptable : 2;
        uint64_t nstable : 1;
    }tablebits;
    uint64_t raw;
}VPage;

/*
 * LBPagingMap -- maps a virtual address to its physical
 * address
 * @param virtual -- virtual address to map
 * @param physical -- physical address to map
 */
void LBPagingMap(uint64_t virtual, uint64_t physical){
    uint64_t flags = PTE_VALID | PTE_TABLE | PTE_AF | PTE_SH_INNER | 
    PTE_AP_RW | PTE_NORMAL;

    const long i4 = (virtual >> 39) & 0x1FF;
    const long i3 = (virtual >> 30) & 0x1FF;
    const long i2 = (virtual >> 21) & 0x1FF;
    const long i1 = (virtual >> 12) & 0x1FF;

    uint64_t* pml4i = (uint64_t*)l0_table;

    if (!(pml4i[i4] & 1ULL)){
        uint64_t page = (uint64_t)LBPmmngrAlloc();
        pml4i[i4] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
        memset((void*)page, 0, 4096);
        asm volatile ("dc civac, %0"::"r"(&pml4i[i4]):"memory");
        asm volatile("dsb ish");
        asm volatile ("isb");
    }

    uint64_t* pml3 = (uint64_t*)(pml4i[i4] & ~0xFFFULL);
    
    if (!(pml3[i3] & 1ULL)){
        uint64_t page = (uint64_t)LBPmmngrAlloc();
        pml3[i3] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
        memset((void*)page, 0, 4096);
        asm volatile ("dc civac, %0"::"r"(&pml3[i3]):"memory");
        asm volatile("dsb ish");
        asm volatile("isb");
    }

    uint64_t *pml2 = (uint64_t*)(pml3[i3] & ~0xFFFULL);
    if (!(pml2[i2] & 1ULL)){
        uint64_t page = (uint64_t)LBPmmngrAlloc();
        pml2[i2] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
        memset((void*)page, 0, 4096);
        asm volatile ("dc civac, %0"::"r"(&pml2[i2]):"memory");
        asm volatile("dsb ish");
        asm volatile("isb");
    }

    uint64_t* pml1 = (uint64_t*)(pml2[i2] & ~0xFFFULL);
    if (pml1[i1] & 1){
        LBUartPutString("Already present \n");
        return;
    }

    pml1[i1] = ((uint32_t)physical & ~0xFFFULL) | flags;

    virtual &= ~0xFFFULL;


    asm volatile("dc civac, %0"::"r"(&pml1[i1]):"memory");
    asm volatile("dsb ish");
    asm volatile("isb");

    __asm__ volatile (
        "dsb ishst \n"
        "tlbi vmalle1is \n"
        "dsb ish \n"
        "isb":::"memory");
    return;
}

void LBPageTableWalk(uint64_t virtual){
     const long i4 = (virtual >> 39) & 0x1FF;
    const long i3 = (virtual >> 30) & 0x1FF;
    const long i2 = (virtual >> 21) & 0x1FF;
    const long i1 = (virtual >> 12) & 0x1FF;

    uint64_t* pml4i = (uint64_t*)l0_table;
 
    uint64_t* pml3 = (uint64_t*)(pml4i[i4] & ~0xFFFULL);

    uint64_t *pml2 = (uint64_t*)(pml3[i3] & ~0xFFFULL);

    uint64_t* pml1 = (uint64_t*)(pml2[i2] & ~0xFFFULL);
    VPage *page = (VPage*)&pml1[i1];

    LBUartPutString("Page table walk l3 : ");
    LBUartPrintHex(pml1[i1]);
    LBUartPutString("\r\n");
    LBUartPutString("Present : ");
    LBUartPrintHex(page->bits.present);
    LBUartPutString("Table Page : ");
    LBUartPrintHex(page->bits.table_page);
    LBUartPutString("attrindex : ");
    LBUartPrintHex(page->bits.attrindex);
    LBUartPutString("Avail : ");
    LBUartPrintHex(page->bits.avail);
    LBUartPutString("\r\n");
    LBUartPrintHex((page->bits.page << 12));
}

/*
 * LBPagingFree -- free up a mapping
 * @param virt_addr -- virtual address to free
 * @param free_physical -- boolean value whether to free up
 * the physical address or not
 */
void LBPagingFree(uint64_t virt_addr, bool free_physical){
    const long i1 = pml4_index(virt_addr);
    uint64_t* pml4_ = (uint64_t*)l0_table;
    uint64_t* pdpt = (uint64_t*)(pml4_[i1] & ~0xFFFUL);
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_index(virt_addr)] & ~0xFFFUL);
    uint64_t* pt = (uint64_t*)(pd[pd_index(virt_addr)] & ~0xFFFUL);
    uint64_t* page = (uint64_t*)(pt[pt_index(virt_addr)] & ~0xFFFUL);

    if ((pt[pt_index(virt_addr)] & 1) != 0){
        pt[pt_index(virt_addr)] = 0;
    }

    if (free_physical && page != 0)
       LBPmmngrFree((paddr_t)page);
}
