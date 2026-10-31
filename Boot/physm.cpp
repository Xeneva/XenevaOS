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

#include "xnldr.h"
#include "xnout.h"
#include "physm.h"
#include <Common.hpp>

paddr_t* pagestack;
paddr_t* stackptr;
paddr_t* allocatedStack;
paddr_t* allocatedPtr;
uint32_t allocatedCount;
uint64_t usableRam;
uint64_t usableSize;
uint64_t ramSize;

/*
 * XEInitialisePmmngr - Initialise Physical Memory Manager
 * @param memmap -- Pointer to memory map
 * @param buffer -- Pointer to memory area where 
 * physical addresses will get stored
 * @param bufsize -- size of the buffer
 */
void XEInitialisePmmngr(const struct EfiMemoryMap memmap, void* buffer, size_t bufsize){
	uint64_t mem_blocks = 0;
	//! Build a physical page stack
	stackptr = pagestack = (paddr_t*)buffer;

	bufsize /= 2;
	allocatedPtr = allocatedStack = raw_offset<paddr_t*>(buffer, bufsize);
	allocatedCount = 1;

	EFI_MEMORY_DESCRIPTOR* current = memmap.memmap;
	while (static_cast<long unsigned int>(raw_diff(current, memmap.memmap)) < memmap.MemMapSize)
	{
		ramSize += current->NumberOfPages * 4096;
		if (current->Type == EfiConventionalMemory){//|| current->Type == EfiPersistentMemory){
			paddr_t addr = current->PhysicalStart;
			size_t  numpages = current->NumberOfPages;
			usableRam = current->PhysicalStart;
			usableSize = current->NumberOfPages * 4096;

			if (EFI_PAGE_SIZE < PAGESIZE)
				numpages /= (PAGESIZE / EFI_PAGE_SIZE);
			else if (EFI_PAGE_SIZE > PAGESIZE)
				numpages *= (EFI_PAGE_SIZE / PAGESIZE);

			mem_blocks++;
			while (numpages > 0 && static_cast<long unsigned int>(raw_diff(stackptr, pagestack)) < bufsize){
				*stackptr++ = addr;
				--numpages;
				addr += PAGESIZE;
			}
			if (static_cast<long unsigned int>(raw_diff(stackptr, pagestack)) >= bufsize)
				break;
		}
		current = raw_offset<EFI_MEMORY_DESCRIPTOR*>(current, memmap.DescriptorSize);

	}

}

/*
 * XEPmmngrAllocate -- Allocates a physical block
 */
paddr_t XEPmmngrAllocate(){
	if (stackptr == pagestack)
		return 0;
	else
	{
		paddr_t allocated = *--stackptr;
		*allocatedPtr++ = allocated;
		allocatedCount++;
		return allocated;
	}

}

/*
 * XEPmmngrFree -- free up 
 * previously allocated physical block
 * @param addr -- Pointer to physical address
 */
void XEPmmngrFree(paddr_t addr) {
	*stackptr++ = addr;
}

/*
 * XEPmmngrList -- list all available physical block
 */
void XEPmmngrList() {
	while (allocatedCount) {
		uint64_t addr = *allocatedPtr--;
		XEGuiPrint("Address -> %x \n", addr);
		allocatedCount--;
	}
}

[[maybe_unused]] static struct _pmmngr_boot_info_ {
	paddr_t* pgstack;
	//paddr_t* pgstack;
	paddr_t* alstack;
	paddr_t* alstackptr;
} pmmngr_boot_info;

/*
 *XEGetAlstack -- return the allocated stack ptr
 */
paddr_t* XEGetAlstack() {
	return allocatedStack;
}

/*
 * XEGetAlstackptr -- return the previously allocated
 * pointer
 */
paddr_t* XEGetAlstackptr() {
	return allocatedPtr;
}

/*
 * XEReserveMemCount -- returns reserved memory area,
 * i.e all allocated memory for the kernel and files
 */
uint64_t XEReserveMemCount() {
	return allocatedCount;
}

/*
 * XEGetPgStack -- return the entire page stack
 */
paddr_t* XEGetPgStack() {
	return pagestack;
}


paddr_t* XEGetStackPtr() {
	return stackptr;
}

/*
 * XEGetPhysicalStart -- return the starting address
 * from where all usable ram starts
 */
uint64_t XEGetPhysicalStart() {
	return usableRam;
}

/*
 * XEGetPhysicalSize -- return the size of entire
 * usable area
 */
uint64_t XEGetPhysicalSize() {
	return usableSize;
}

/*
 * XEGetRamSize -- get the total physical memory
 * size
 */
uint64_t XEGetRamSize() {
	return ramSize;
}