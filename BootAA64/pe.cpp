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

#include "pe.h"
#include <aurora.h>
#include "clib.h"
#include "physm.h"
#include "xnout.h"
#include "paging.h"
#include "uart0.h"
#include "lowlevel.h"

static void synchronize_executable_image(size_t address, size_t size) {
	size_t start = address & ~(size_t)63;
	size_t end = (address + size + 63) & ~(size_t)63;
	for (size_t line = start; line < end; line += 64)
		dc_cvau(line);
	dsb_ish();
	for (size_t line = start; line < end; line += 64)
		ic_ivau(line);
	dsb_ish();
	isb_flush();
}

static void copy_mem(void* dst, void* src, size_t length) {
	uint8_t* dstp = (uint8_t*)dst;
	uint8_t* srcp = (uint8_t*)src;
	while (length--)
		*dstp++ = *srcp++;
}

static void zero_mem(void* dst, size_t length) {
	uint8_t* dstp = (uint8_t*)dst;
	while (length--)
		*dstp++ = 0;
}

/*
 * XEPELoadImage -- loads PE image into virtual address
 * @param filebuff -- pointer to the pe kernel buffer
 */
void XEPELoadImage(void* filebuff) {
	uint8_t* filebuf = (uint8_t*)filebuff;

	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)filebuf;
	struct _IMAGE_NT_HEADERS_PE32PLUS* ntHeaders =
		raw_offset<struct _IMAGE_NT_HEADERS_PE32PLUS*>(dosHeader, dosHeader->e_lfanew);

	PSECTION_HEADER sectionHeader = raw_offset<PSECTION_HEADER>(
		&ntHeaders->OptionalHeader, ntHeaders->FileHeader.SizeOfOptionaHeader);
	size_t ImageBase = 0xFFFFC00000000000; // 0xFFFFFFFC00000000;
	void* ImBase = (void*)ImageBase;

	paddr_t phys = XEPmmngrAllocate();
	if (!phys)
		XEGuiPrint("XELDR: phys returned zero \r\n");
	XEPagingMap(ImageBase, phys);

	XEUARTPrint("Paging mapped : %x -- %x \r\n", ImageBase, phys);

	copy_mem((void*)ImBase, filebuf, ntHeaders->OptionalHeader.SizeOfHeaders);

	XEUARTPrint("Copied first 4KiB \r\n");

	for (size_t i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i) {
		//CHAR16 buf[9];
		//copy_mem(buf, sectionHeader[i].Name, 8);
		//ASCIIToChar16(sectionHeader[i].Name,(wchar_t*)buf);
		//buf[8] = 0;
		size_t load_addr = ImageBase + sectionHeader[i].VirtualAddress;
		void* sect_addr = (void*)load_addr;

		size_t sectsz = sectionHeader[i].VirtualSize;
		if (sectionHeader[i].SizeOfRawData > sectsz) {
			sectsz = sectionHeader[i].SizeOfRawData;
		}
		size_t aligned_addr = load_addr & ~0xFFFULL;
		int req_pages = (sectsz + (load_addr & 0xFFF)) / 4096 +
						(((sectsz + (load_addr & 0xFFF)) % 4096) ? 1 : 0);
		uint64_t* block = 0;
		for (int j = 0; j < req_pages; j++) {
			uint64_t alloc = (aligned_addr + j * PAGESIZE);
			if (!XEPagingIsMapped(alloc)) {
				XEPagingMap(alloc, XEPmmngrAllocate());
				memset((void*)alloc, 0, 4096);
			}
			if (!block)
				block = (uint64_t*)alloc;
		}

		if (sectionHeader[i].PointerToRawData != 0 && sectionHeader[i].SizeOfRawData != 0)
			copy_mem(sect_addr,
					 raw_offset<void*>(filebuf, sectionHeader[i].PointerToRawData),
					 sectionHeader[i].SizeOfRawData);
		if (sectionHeader[i].VirtualSize > sectionHeader[i].SizeOfRawData)
			zero_mem(raw_offset<void*>(sect_addr, sectionHeader[i].SizeOfRawData),
					 sectionHeader[i].VirtualSize - sectionHeader[i].SizeOfRawData);
	}

	/* The PE source buffer is data. Synchronize the final mapped image only,
	 * after every section has been copied, before control can transfer to it. */
	synchronize_executable_image(ImageBase, ntHeaders->OptionalHeader.SizeOfImage);
}
