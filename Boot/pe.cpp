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
#include "clib.h"
#include "paging.h"
#include "xnout.h"
#include "physm.h"
#include <Common.hpp>

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
 * XEPELoadImage -- loads a executable image to memory
 * @param filebuf -- file buffer
 */
void* XEPELoadImage(void* filebuf1){

	uint8_t* filebuf = (uint8_t*)filebuf1;

	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)filebuf;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);

	PSECTION_HEADER sectionHeader = raw_offset<PSECTION_HEADER>(&ntHeaders->OptionalHeader, ntHeaders->FileHeader.SizeOfOptionaHeader);
	size_t ImageBase = ntHeaders->OptionalHeader.ImageBase;
	void* ImBase = (void*)ImageBase;

	if (!XEPagingMap(ImBase, PADDR_T_MAX, ntHeaders->OptionalHeader.SizeOfHeaders, PAGE_ATTRIBUTE_WRITABLE)) {
		XEGuiPrint("Could not allocate enough memory for Kernel\n");
		return NULL;
	}

	

	copy_mem((void*)ImBase, filebuf, ntHeaders->OptionalHeader.SizeOfHeaders);

	for (size_t i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i) {
		CHAR16 buf[9];
		copy_mem(buf, sectionHeader[i].Name, 8);
		buf[8] = 0;
		size_t load_addr = ImageBase + sectionHeader[i].VirtualAddress;
		void* sect_addr = (void*)load_addr;
		size_t sectsz = sectionHeader[i].VirtualSize;
		if (!XEPagingMap((void*)load_addr, PADDR_T_MAX, sectsz, PAGE_ATTRIBUTE_WRITABLE)) {
			XEGuiPrint("failed to map section %s \n", buf);
			return nullptr;
		}

		copy_mem(sect_addr, raw_offset<void*>(filebuf, sectionHeader[i].PointerToRawData), sectionHeader[i].SizeOfRawData);
		if (sectionHeader[i].VirtualSize > sectionHeader[i].SizeOfRawData)
			zero_mem(raw_offset<void*>(sect_addr, sectionHeader[i].SizeOfRawData), sectionHeader[i].VirtualSize - sectionHeader[i].SizeOfRawData);
	}

	return (void*)ImBase;
}

uint64_t _dll_base_ = 0xFFFFC00000400000;

/*
 * XEPELoadDLLImage -- load dll image to memory
 @param filebuf -- Pointer to file buffer
 */
uint64_t XEPELoadDLLImage(void* filebuf1) {
	uint8_t* filebuf = (uint8_t*)filebuf1;
	int _dll_base_offset_ = 0;
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)filebuf;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	PSECTION_HEADER secthdr = raw_offset<PSECTION_HEADER>(&ntHeaders->OptionalHeader, ntHeaders->FileHeader.SizeOfOptionaHeader);
	size_t imageBase = _dll_base_;
	if (!XEPagingMap((void*)imageBase, PADDR_T_MAX, ntHeaders->OptionalHeader.SizeOfHeaders, PAGE_ATTRIBUTE_WRITABLE)) {
		XEGuiPrint("Could not allocate enough memory for Xeneva Kernel\n");
		for(;;);
		return 0;
	}
	copy_mem((void*)imageBase, filebuf, ntHeaders->OptionalHeader.SizeOfHeaders);
	_dll_base_offset_++;
	for (size_t i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
		size_t load_addr = imageBase + secthdr[i].VirtualAddress;
		size_t sectSize = secthdr[i].VirtualSize;
		int req_pages = sectSize / 4096 +
			((sectSize % 4096) ? 1 : 0);
		if (!XEPagingMap((void*)load_addr, PADDR_T_MAX, sectSize, PAGE_ATTRIBUTE_WRITABLE)) {
			XEGuiPrint("Failed to map physical address to virtual address -> %x \n", load_addr);
			for (;;);
		}
		copy_mem((void*)load_addr, raw_offset<void*>(filebuf, secthdr[i].PointerToRawData), secthdr[i].SizeOfRawData);
		_dll_base_offset_ += req_pages;
	}
	_dll_base_ = _dll_base_ + (_dll_base_offset_ * PAGESIZE);
	return imageBase;
}

/*
 * XEPEGetEntryPoint -- returns the entry point 
 * of loaded image
 * @param image -- Pointer to file buffer
 */
XEImageEntry XEPEGetEntryPoint(void* image){
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	//if (ntHeaders->FileHeader.Machine != MACHINE_NATIVE || ntHeaders->OptionalHeader.Magic != MAGIC_NATIVE)
		//return NULL;
	return raw_offset<XEImageEntry>(image, ntHeaders->OptionalHeader.AddressOfEntryPoint);
}