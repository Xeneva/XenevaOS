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

#include "paging.h"
#include "physm.h"
#include "clib.h"
#include "xnout.h"
#include "lowlevel.h"
#include <Board/imx8mp/imx8mp_uart.h>

uint64_t* l0_table_base;

void PRINTTCR(uint64_t rtc) {
	XEGuiPrint("T0SZ : %d \n", ((rtc >> 0) & 0x3F));
	XEGuiPrint("EPD0 : %d \n", ((rtc >> 7) & 0x1));
	XEGuiPrint("IRGN0 : %d \n", ((rtc >> 8) & 0x3));
	XEGuiPrint("ORGN0 : %d \n", ((rtc >> 10) & 0x3));
	XEGuiPrint("SH0 : %d \n", ((rtc >> 12) & 0x3));
	XEGuiPrint("TG0 : %d \n", ((rtc >> 14) & 0x3));
	XEGuiPrint("T1SZ : %d \n", ((rtc >> 16) & 0x3F));
	XEGuiPrint("EPD1 : %d \n", ((rtc >> 23) & 0x1));
	XEGuiPrint("IRGN1 : %d \n", ((rtc >> 24) & 0x3));
	XEGuiPrint("ORGN1 : %d \n", ((rtc >> 26) & 0x3));
	XEGuiPrint("SH1 : %d \n", ((rtc >> 28) & 0x3));
	XEGuiPrint("TG1 : %d \n", ((rtc >> 30) & 0x3));
	XEGuiPrint("IPS : %d \n", ((rtc >> 32) & 0x7));
	XEGuiPrint("AS : %d \n", ((rtc >> 36) & 0x1));
	XEGuiPrint("TBI0 : %d \n", ((rtc >> 37) & 0x1));
	XEGuiPrint("TBI1 : %d \n", ((rtc >> 38) & 0x1));
	XEGuiPrint("HA : %d \n", ((rtc >> 39) & 0x1));
	XEGuiPrint("HPD0 : %d \n", ((rtc >> 40) & 0x1));
}

size_t pml4_index(uint64_t virt) {
	uint64_t l0_index = (virt >> 39) & 0x1FF;
	return l0_index;
}
size_t pdpt_index(uint64_t virt) {
	uint64_t l1_index = (virt >> 30) & 0x1FF;
	return l1_index;
}
void XEMapMMIO(uint64_t* l1_table, uint64_t va, uint64_t pa) {
	uint64_t l1_index = pdpt_index(va);
	l1_table[l1_index] = (pa & ~((1ULL << 30) - 1)) | PAGE_TABLE_ENTRY_PRESENT |
						 PAGE_TABLE_ENTRY_BLOCK | PAGE_TABLE_ENTRY_AP_RW | PAGE_TABLE_ENTRY_SH |
						 PAGE_TABLE_ENTRY_DEVICE | PAGE_TABLE_ENTRY_AF;
	dsb_ish();
	isb_flush();
}

extern "C" void tlb_flush_all();
/*
 * XEPagingInitialize -- initialize paging
 */
void XEPagingInitialize() {
	uint64_t previousBase = 0;
	if (_getCurrentEL() == 1)
		previousBase = read_ttbr0_el1();
	else
		previousBase = read_ttbr0_el2();

	bool required_sctrl = 0;
	l0_table_base = (uint64_t*)previousBase;
	if (previousBase == 0) {
		previousBase = XEPmmngrAllocate();
		XEGuiPrint("pREVIOUS Base : %x \r\n", previousBase);
		l0_table_base = (uint64_t*)previousBase;

		uint64_t* l0_el2_base = (uint64_t*)read_ttbr0_el2();

		XEGuiPrint("EL2Base : %x \r\n", l0_el2_base);

		for (int i = 0; i < 512; i++) {
			l0_table_base[i] = l0_el2_base[i];
		}

		/*	for (int i = 0; i < 16 * 1024 * 1024 / 0x1000; i++) {
			uint64_t* l1_table = (uint64_t*)(l0_el2_base[pml4_index(0x3F000000 + i * 0x1000)] & ~0xFFFULL);
			XEMapMMIO(l1_table, 0x3F000000 + i * 0x1000, 0x3F000000 + i * 0x1000);
		}*/
		required_sctrl = 1;
	}

#ifdef __TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__
	XEMapMMIO((uint64_t*)l0_table_base[pml4_index(IMX8MP_UART3_BASE_ADDRESS)],
			  IMX8MP_UART3_BASE_ADDRESS,
			  IMX8MP_UART3_BASE_ADDRESS);
#endif

	if (_getCurrentEL() != 1) {
		uint64_t tcr1 = read_tcr_el2();

		XEGuiPrint("TCR1 : %x \r\n", tcr1);
		write_tcr_el1(tcr1);
	} else {
		uint64_t tcr = ((16UL << 0) | (0UL << 14) | (0b11UL << 12) | (0b01UL << 10) |
						(0b01UL << 8) | (16UL << 16) | (0b10UL << 30) | (0b11UL << 28) |
						(0b01UL << 26) | (0b01UL << 24) | (4ULL << 32));
		write_tcr_el1(tcr);
	}

	uint64_t mair = 0x000000000044ff00;
	write_mair_el1(mair);
	mair = read_mair_el1();
	write_ttbr0_el1(l0_table_base);
	write_ttbr1_el1(l0_table_base);
	tlb_flush_all();

	//if (required_sctrl) {
	uint64_t sctlr = (1UL << 0) | (1UL << 2) | (1UL << 12) | (1UL << 23) | (1UL << 28) |
					 (1UL << 29) | (1UL << 20) | (1UL << 7);
	write_sctlr_el1(sctlr);
	//XEPrintf(const_cast<wchar_t*>(L"SCTLR_EL1 setup completed \n"));
	//}

	isb_flush();

#ifdef __TARGET_BOARD_RPI3__
	uint64_t ttbr0_el2 = read_ttbr0_el2();
#endif
	/*char pa[16];
	sztoa(ttbr0_el2, pa, 16);
	wchar_t pa16[16];
	ASCIIToChar16(pa, pa16);
	XEPrintf(const_cast<wchar_t*>(L"TTRB0_el2 : "));
	XEPrintf(const_cast<wchar_t*>(pa16));
	XEPrintf(const_cast<wchar_t*>(L"\r\n"));

	XEPrintf(const_cast<wchar_t*>(L"Paging initialized \r\n"));*/
	XEGuiPrint("Paging initialized \r\n");
	/*if (_getCurrentEL() != 1) 
		l0_table_base = (uint64_t*)read_ttbr0_el2();*/
}

void XEPagingInstallPhysicalDirectMap() {
	const uint64_t directBase = 0xFFFF800000000000ULL;
	const uint64_t l0Index = pml4_index(directBase);
	uint64_t* l1_table = (uint64_t*)XEPmmngrAllocate();
	if (!l1_table)
		return;
	memset(l1_table, 0, PAGESIZE);
	for (uint64_t i = 0; i < 512; ++i) {
		uint64_t phys = i << 30;
		l1_table[i] = phys | PAGE_TABLE_ENTRY_PRESENT | PAGE_TABLE_ENTRY_BLOCK |
			PAGE_TABLE_ENTRY_AF | PAGE_TABLE_ENTRY_SH | PAGE_TABLE_ENTRY_AP_RW |
			PAGE_TABLE_ENTRY_MEMATTR | PAGE_TABLE_ENTRY_PXN | PAGE_TABLE_ENTRY_UXN;
	}
	l0_table_base[l0Index] = ((uint64_t)l1_table & ~0xFFFULL) |
		PAGE_TABLE_ENTRY_PRESENT | PAGE_TABLE_ENTRY_PAGE;
	/* the heap slot is owned by Aurora, make sure it doesnt inherit some
	 * arbitrary firmware mapping just because its L0 entry has the valid
	 * bit set --axiss */
	l0_table_base[pml4_index(0xFFFFE00000000000ULL)] = 0;
	dsb_ish();
	tlb_flush_all();
	isb_flush();
}

void XEPagingInit2() {
	uint64_t* l0_table = (uint64_t*)XEPmmngrAllocate();
	memset(l0_table, 0, 4096);
	uint64_t* l1_table = (uint64_t*)XEPmmngrAllocate();
	memset(l1_table, 0, 4096);

	l0_table[0] = ((uint64_t)l1_table) | 0x3;

	for (int i = 0; i < 512; ++i) {
		uint64_t addr = (uint64_t)i << 30;
		l1_table[i] = (addr | PAGE_TABLE_ENTRY_PRESENT | PAGE_TABLE_ENTRY_BLOCK |
					   PAGE_TABLE_ENTRY_AF | PAGE_TABLE_ENTRY_SH | (1ULL << 2));
		isb_flush();
	}

#ifdef __TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__
	XEGuiPrint("L0 Index for UART : %d \r\n", pml4_index(IMX8MP_UART3_BASE_ADDRESS));
	XEMapMMIO(l1_table, IMX8MP_UART3_BASE_ADDRESS, IMX8MP_UART3_BASE_ADDRESS);
#endif

	uint64_t tcr1 = ((16UL << 0) | (0UL << 14) | (0b11UL << 12) | (0b01UL << 10) | (0b01UL << 8) |
					 (16UL << 16) | (0b10UL << 30) | (0b11UL << 28) | (0b01UL << 26) |
					 (0b01UL << 24) | (4ULL << 32));

	XEGuiPrint("TCR1 : %x \r\n", tcr1);
	uint64_t mair = 0x000000000044ff00;
	write_mair_el1(mair);

	write_tcr_el1(tcr1);

	write_ttbr0_el1(l0_table);
	write_ttbr1_el1(l0_table);
	isb_flush();

	l0_table_base = l0_table;

	uint64_t sctlr = (1ULL << 0) | (1ull << 2) | (1ull << 12) | (1ull << 23) | (1ull << 28) |
					 (1ull << 29) | (1ull << 20) | (1ull << 7);
	write_sctlr_el1(sctlr);
	isb_flush();
}

void XEPagingCopy() {
	uint64_t previousBase = 0;
	previousBase = read_ttbr0_el1();

	uint64_t* l0_el2_base = (uint64_t*)read_ttbr0_el2();
	uint64_t* ttbr0_base = (uint64_t*)previousBase;

	for (int i = 0; i < 512; i++) {
		ttbr0_base[i] = l0_el2_base[i];
	}

	l0_table_base = (uint64_t*)previousBase;
}

/*
 * XEPagingMap -- maps a physical address to virtual address
 * @param virtualAddr -- virtual address
 * @param physAddr -- physical address
 */
void XEPagingMap(uint64_t virtualAddr, uint64_t physAddr) {
	uint64_t l0_index = (virtualAddr >> 39) & 0x1FF;
	uint64_t l1_index = (virtualAddr >> 30) & 0x1FF;
	uint64_t l2_index = (virtualAddr >> 21) & 0x1FF;
	uint64_t l3_index = (virtualAddr >> 12) & 0x1FF;

	uint64_t* l1_table;
	uint64_t* l2_table;
	uint64_t* l3_table;

	if (!(l0_table_base[l0_index] & 1)) {
		l1_table = (uint64_t*)XEPmmngrAllocate();
		memset(l1_table, 0, PAGESIZE);
		l0_table_base[l0_index] = ((uint64_t)l1_table & ~0xFFFUL) | PAGE_TABLE_ENTRY_PRESENT |
								  PAGE_TABLE_ENTRY_PAGE | PAGE_TABLE_ENTRY_AF;
		isb_flush();
		dsb_ish();
	} else {
		l1_table = (uint64_t*)(l0_table_base[l0_index] & ~0xFFFULL);
	}

	if (!(l1_table[l1_index] & 1)) {
		l2_table = (uint64_t*)XEPmmngrAllocate();
		memset(l2_table, 0, PAGESIZE);
		l1_table[l1_index] = ((uint64_t)l2_table & ~0xFFFUL) | PAGE_TABLE_ENTRY_PRESENT |
							 PAGE_TABLE_ENTRY_PAGE | PAGE_TABLE_ENTRY_AF;
		isb_flush();
		dsb_ish();
	} else {
		l2_table = (uint64_t*)(l1_table[l1_index] & ~0xFFFULL);
	}

	if (!(l2_table[l2_index] & 1)) {
		l3_table = (uint64_t*)XEPmmngrAllocate();
		memset(l3_table, 0, PAGESIZE);
		l2_table[l2_index] = ((uint64_t)l3_table & ~0xFFFULL) | PAGE_TABLE_ENTRY_PRESENT |
							 PAGE_TABLE_ENTRY_PAGE | PAGE_TABLE_ENTRY_AF;
		isb_flush();
		dsb_ish();
	} else {
		l3_table = (uint64_t*)(l2_table[l2_index] & ~0xFFFULL);
	}

	l3_table[l3_index] = (physAddr & ~0xFFFULL) | PAGE_FLAGS;

	tlb_flush((virtualAddr & ~0xFFFULL) >> 12);
	tlb_flush_all(); // Extra safety for AArch64
	isb_flush();
	dsb_ish();
}

// Checks if a virtual address is already mapped to prevent PE section overlaps
bool XEPagingIsMapped(uint64_t virtualAddr) {
	uint64_t l0_index = (virtualAddr >> 39) & 0x1FF;
	uint64_t l1_index = (virtualAddr >> 30) & 0x1FF;
	uint64_t l2_index = (virtualAddr >> 21) & 0x1FF;
	uint64_t l3_index = (virtualAddr >> 12) & 0x1FF;

	if (!(l0_table_base[l0_index] & 1))
		return false;
	uint64_t* l1_table = (uint64_t*)(l0_table_base[l0_index] & ~0xFFFULL);

	if (!(l1_table[l1_index] & 1))
		return false;
	uint64_t* l2_table = (uint64_t*)(l1_table[l1_index] & ~0xFFFULL);

	if (!(l2_table[l2_index] & 1))
		return false;
	uint64_t* l3_table = (uint64_t*)(l2_table[l2_index] & ~0xFFFULL);

	return (l3_table[l3_index] & 1) != 0;
}
