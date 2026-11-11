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

/*
 * Physical layout of the BCM2837 (Raspberry Pi 3B/3B+) as seen by the ARM:
 *
 *   0x00000000 .. ram_end      SDRAM given to the ARM (DTB /memory)
 *   ram_end    .. 0x3F000000   VideoCore (GPU) memory, framebuffer lives here
 *   0x3F000000 .. 0x40000000   peripherals (UART, mailbox, IRQ ctrl, ...)
 *   0x40000000 .. 0x40040000   ARM local peripherals (core timers/mailboxes)
 *
 * The tables built here are shared by TTBR0 (identity, used by LittleBoot
 * and the kernel's early boot) and TTBR1 (the kernel's physical direct map
 * at LB_DIRECT_MAP_BASE, same layout as XNLDR's). Only SDRAM is Normal
 * write-back cacheable; only the two peripheral windows are Device-nGnRnE.
 * GPU memory is left unmapped: the kernel maps the framebuffer itself as
 * Normal non-cacheable, and a second cacheable alias would be unsafe.
 */
static uint64_t l2_low[512] __attribute__((aligned(0x1000)));   /* 0..1 GiB */
static uint64_t l2_high[512] __attribute__((aligned(0x1000)));  /* 1..2 GiB */

#define BLOCK_2M          (1ULL << 21)
#define PTE_PXN           (1ULL << 53)
#define PTE_UXN           (1ULL << 54)
#define RPI_PERIPH_BASE   0x3F000000ULL
#define RPI_PERIPH_END    0x40000000ULL
#define RPI_LOCAL_BASE    0x40000000ULL

#define NORMAL_BLOCK  (PTE_VALID | PTE_BLOCK | PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_NORMAL)
#define DEVICE_BLOCK  (PTE_VALID | PTE_BLOCK | PTE_AF | PTE_AP_RW | PTE_DEVICE | PTE_PXN | PTE_UXN)

/*
 * LBPagingInitialize -- build the identity + direct map and turn on
 * the MMU with data and instruction caches enabled
 * @param ram_end -- end of ARM SDRAM (from the device tree)
 */
void LBPagingInitialize(uint64_t ram_end) {
    memset(l0_table, 0, 4096);
    memset(l1_table, 0, 4096);
    memset(l2_low, 0, 4096);
    memset(l2_high, 0, 4096);

    if (ram_end > RPI_PERIPH_BASE)
        ram_end = RPI_PERIPH_BASE;

    for (uint64_t i = 0; i < 512; i++) {
        uint64_t pa = i * BLOCK_2M;
        if (pa + BLOCK_2M <= ram_end)
            l2_low[i] = pa | NORMAL_BLOCK;
        else if (pa >= RPI_PERIPH_BASE && pa < RPI_PERIPH_END)
            l2_low[i] = pa | DEVICE_BLOCK;
        /* else: GPU memory, unmapped */
    }
    /* ARM local peripherals: one 2 MiB device block at 1 GiB */
    l2_high[0] = RPI_LOCAL_BASE | DEVICE_BLOCK;

    l1_table[0] = (uint64_t)l2_low | PTE_VALID | PTE_TABLE;
    l1_table[1] = (uint64_t)l2_high | PTE_VALID | PTE_TABLE;

    /* identity map (TTBR0 half) and direct map (TTBR1 half) share l1 */
    l0_table[pml4_index(0)] = (uint64_t)l1_table | PTE_VALID | PTE_TABLE;
    l0_table[pml4_index(LB_DIRECT_MAP_BASE)] = (uint64_t)l1_table | PTE_VALID | PTE_TABLE;

    /* MMU is still off: the tables were written straight to memory */
    asm volatile ("dsb sy" ::: "memory");

    /* attr0: Device-nGnRnE, attr1: Normal WB RA/WA, attr2: Normal NC */
    uint64_t mair = 0x000000000044ff00;
    asm volatile ("msr mair_el1,%0"::"r"(mair));

    /* T0SZ=T1SZ=16 (48-bit), 4K granules, inner shareable write-back
     * walks, IPS=44 bit */
    uint64_t tcr = (16UL << 0) | (0b00UL << 14) | (0b11UL << 12) |
        (0b01UL << 10) | (0b01UL << 8) | (16UL << 16) | (0b10UL << 30) |
        (0b11UL << 28) | (0b01UL << 26) | (0b01UL << 24) | (4UL << 32);
    asm volatile ("msr tcr_el1,%0"::"r"(tcr));
    asm volatile ("msr ttbr0_el1,%0" :: "r"(&l0_table));
    asm volatile ("msr ttbr1_el1,%0":: "r"(&l0_table));
    asm volatile ("isb");
    asm volatile ("tlbi vmalle1\n dsb nsh\n ic iallu\n dsb nsh\n isb":::"memory");

    LBUartPutString("Paging tables ready \n");

    /* M | C | I, plus the RES1 bits the kernel has always been entered with */
    uint64_t sctlr = (1UL<<0) | (1UL<<2) | (1UL<<12) | (1UL << 23) |
     (1UL << 28) | (1UL << 29) | (1UL << 20) | (1UL << 7);
    asm volatile ("msr sctlr_el1,%0"::"r"(sctlr));
    asm volatile ("isb":::"memory");

    LBUartPutString("MMU on, RAM cacheable, MMIO device memory \n");
    LBUartPutString("Paging l0 table :");
    LBUartPrintHex((uint64_t)l0_table);
}

/*
 * LBCacheSyncICache -- make freshly written code visible to instruction
 * fetch: clean D-cache to PoU over the range, then invalidate I-cache
 */
void LBCacheSyncICache(uint64_t start, uint64_t end) {
    uint64_t ctr;
    asm volatile ("mrs %0, ctr_el0" : "=r"(ctr));
    uint64_t line = 4ULL << ((ctr >> 16) & 0xF);
    for (uint64_t va = start & ~(line - 1); va < end; va += line)
        asm volatile ("dc cvau, %0" :: "r"(va) : "memory");
    asm volatile ("dsb ish\n ic ialluis\n dsb ish\n isb" ::: "memory");
}

/*
 * LBCacheCleanRange -- clean D-cache to PoC over a range (data that a
 * core running with caches off, or another bus master, will read)
 */
void LBCacheCleanRange(uint64_t start, uint64_t end) {
    for (uint64_t va = start & ~63ULL; va < end; va += 64)
        asm volatile ("dc cvac, %0" :: "r"(va) : "memory");
    asm volatile ("dsb sy" ::: "memory");
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
        memset((void*)page, 0, 4096);
        pml4i[i4] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
        asm volatile ("dc civac, %0"::"r"(&pml4i[i4]):"memory");
        asm volatile("dsb ish");
        asm volatile ("isb");
    }

    uint64_t* pml3 = (uint64_t*)(pml4i[i4] & ~0xFFFULL);
    
    if (!(pml3[i3] & 1ULL)){
        uint64_t page = (uint64_t)LBPmmngrAlloc();
        memset((void*)page, 0, 4096);
        pml3[i3] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
        asm volatile ("dc civac, %0"::"r"(&pml3[i3]):"memory");
        asm volatile("dsb ish");
        asm volatile("isb");
    }

    uint64_t *pml2 = (uint64_t*)(pml3[i3] & ~0xFFFULL);
    if (!(pml2[i2] & 1ULL)){
        uint64_t page = (uint64_t)LBPmmngrAlloc();
        memset((void*)page, 0, 4096);
        pml2[i2] = (page & ~0xFFFUL) | PTE_VALID | PTE_TABLE | PTE_AF;
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
        asm volatile ("dsb ishst\n tlbi vaae1is, %0\n dsb ish\n isb"
                      :: "r"(virt_addr >> 12) : "memory");
    }

    if (free_physical && page != 0)
       LBPmmngrFree((paddr_t)page);
}
