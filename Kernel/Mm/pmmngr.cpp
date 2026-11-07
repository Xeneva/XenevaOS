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

#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/x86_64_cpu.h>
#include <aucon.h>
#include <efi.h>
#include <string.h>
#include <Hal/serial.h>
#include <stdint.h>

uint64_t _FreeMemory;
uint64_t _ReservedMemory;
uint64_t _UsedMemory;
uint64_t _RamBitmapIndex;
uint64_t _TotalRam;
uint64_t _BitmapSize;
bool _HigherHalf;
bool debugon;
uint8_t* BitmapBuffer;


/*
* AuPmmngrInitBitmap -- Initialize the Ram bitmap with
* all zeros
* @param BitmapSize -- total bitmap size
* @param Buffer -- Pointer to bitmap buffer
*/
void AuPmmngrInitBitmap(size_t BSize, void* Buffer) {
	BitmapBuffer = (uint8_t*)Buffer;
	memset(BitmapBuffer, 0, BSize * 8);
	_BitmapSize = BSize;
}

bool AuPmmngrBitmapCheck(uint64_t index) {
	if (index >= _BitmapSize * 8) return false;
	size_t ByteIndex = index / 8;
	uint8_t BitIndex = index % 8;
	uint8_t BitIndexer = 0x80 >> BitIndex;

	if ((BitmapBuffer[ByteIndex] & BitIndexer) > 0) {
		return true;
	}
	if (debugon)
		AuTextOut("Bit -> %d Index -> %d \n", (BitmapBuffer[ByteIndex] & BitIndexer), index);
	//return ((Buffer[ByteIndex] & BitIndexer) != 0);
	return false;
}

bool AuPmmngrBitmapSet(uint64_t index, bool value) {
	if (index >= _BitmapSize * 8) return false;
	size_t ByteIndex = index / 8;
	uint8_t BitIndex = index % 8;
	uint8_t BitIndexer = 0x80 >> BitIndex;

	BitmapBuffer[ByteIndex] &= ~BitIndexer;
	if (value)
		BitmapBuffer[ByteIndex] |= BitIndexer;

	return true;
}


/*
* AuPmmngrLockPage -- Lock a given page
* @param Address -- Pointer to page
*/
void AuPmmngrLockPage(uint64_t Address) {
	uint64_t Index = (Address / 4096);
	if (AuPmmngrBitmapCheck(Index)) return;
	if (AuPmmngrBitmapSet(Index, true)) {
		_FreeMemory--;
		_ReservedMemory++;
	}
}

/*
* AuPmmngrLockPage -- locks a set of pages
* @param Address -- Starting address of the first page
* @param Size -- Size of the set
*/
void AuPmmngrLockPages(void *Address, size_t Size) {
	uint64_t addr = (uint64_t)Address;
	for (size_t i = 0; i < Size; i++) {
		AuPmmngrLockPage(addr + i * 4096);
	}
}

/*
* AuPmmngrUnreservePage -- Marks a page as free
* @param Address -- Pointer to the page
*/
void AuPmmngrUnreservePage(void* Address) {
	uint64_t Index = (uint64_t)Address / 4096;
	if (AuPmmngrBitmapCheck(Index) == false) return;
	if (AuPmmngrBitmapSet(Index, false)) {
		_FreeMemory++;
		_ReservedMemory--;
		if (_RamBitmapIndex > Index) _RamBitmapIndex = Index;
	}
}

/*
* AuPmmngrInitialise -- initialise the physical memory
* manager
* @param info -- Pointer to kernel boot info structure
*/
void AuPmmngrInitialize(KERNEL_BOOT_INFO *info) {
	debugon = 0;
	_FreeMemory = 0;
	_BitmapSize = 0;
	_TotalRam = 0;
	_RamBitmapIndex = 0;
	BitmapBuffer = 0;

	uint64_t MemMapEntries = info->mem_map_size / info->descriptor_size;
	void* BitmapArea = 0;
	/* Scan a suitable area for the bitmap */
	for (size_t i = 0; i < MemMapEntries; i++) {
		EFI_MEMORY_DESCRIPTOR *EfiMem = (EFI_MEMORY_DESCRIPTOR*)((uint64_t)info->map + i * info->descriptor_size);
		_TotalRam += EfiMem->num_pages;
		if (EfiMem->type == 7 && EfiMem->attrib & 0x00000001)  {
			if (((EfiMem->num_pages * 4096) > 0x100000) && BitmapArea == 0)  {
				if ((EfiMem->phys_start & (PAGE_SIZE - 1)) == 0) {
					BitmapArea = (void*)EfiMem->phys_start; // ((EfiMem->phys_start + 0xFFF) & ~0xFFF);
					break;
				}
			}
		}
	}

	_FreeMemory = _TotalRam;
 
	uint64_t BitmapSize = (_TotalRam / 8) + 1; // (_TotalRam * 4096) / 4096 / 8 + 1;

	/* now initialise the bitmap */
	AuPmmngrInitBitmap(BitmapSize, BitmapArea);

	AuPmmngrLockPages((void*)BitmapArea, BitmapSize);

	for (size_t i = 0; i < MemMapEntries; i++) {
		EFI_MEMORY_DESCRIPTOR *EfiMem = (EFI_MEMORY_DESCRIPTOR*)((uint64_t)info->map + i * info->descriptor_size);
		//_TotalRam += EfiMem->num_pages;
		if (EfiMem->type != 7) {
			uint64_t PhysStart = EfiMem->phys_start;
			for (size_t j = 0; j < EfiMem->num_pages; j++){
				AuPmmngrLockPage(PhysStart + j * 4096);
			}
		}
	}


	/* Lock addresses below 1MiB mark */
	for (size_t i = 0; i < (1 * 1024 * 1024)/ 4096; i++)
		AuPmmngrLockPage(i * 4096);

	uint32_t AllocCount = info->reserved_mem_count;
	uint64_t* AllocStack = (uint64_t*)info->allocated_stack;
	while (AllocCount) {
		uint64_t Address = *AllocStack--;
		AuPmmngrLockPage(Address);
		AllocCount--;
	}

	/* Copy the SMP Code and mark that address
	* as unusable, [no doubt: this address is already
	* marked unusable above
	*/
	uint64_t* SMPAddress = (uint64_t*)0xA000;
	AuPmmngrLockPage(0xA000);
	memset(SMPAddress, 0, 4096);
	memcpy(SMPAddress, info->apcode, 4096);


}

/*
 * AuPmmngrAlloc -- Allocate a single physical page
 * frame and return it to the caller
 */
void* AuPmmngrAlloc() {
	//debugon = true;
	for (; _RamBitmapIndex < _BitmapSize * 8; _RamBitmapIndex++) {
		//AuTextOut("  RamBitmap[%d] -> %d   ",_RamBitmapIndex, RamBitmap[_RamBitmapIndex]);
		if (AuPmmngrBitmapCheck(_RamBitmapIndex)) continue;
		AuPmmngrLockPage(_RamBitmapIndex * 4096);
		_UsedMemory++;
		return (void*)(_RamBitmapIndex * 4096);
	}

	x64_cli();
	AuTextOut("Kernel Panic!!! No more physical memory \n");
	for (;;);
}

/*
 * AuPmmngrAllocBlocks -- Allocate multiple physical page frames
 * and return the first page pointer to the caller
 * @param size -- Number of blocks to allocate
 */
void* AuPmmngrAllocBlocks(int num) {
	void* First = AuPmmngrAlloc();
	for (int i = 1; i < num; i++)
		AuPmmngrAlloc();

	return First;
}

/*
 * AuPmmngrFree -- Free a physical page frame
 * @param Address -- Pointer to physical page
 */
void AuPmmngrFree(void* Address) {
	//uint64_t ShiftAddr = (uint64_t)Address >> PAGE_SHIFT;
	uint64_t Index = (uint64_t)Address / 4096;
	if (AuPmmngrBitmapCheck(Index) == false) return;
	if (AuPmmngrBitmapSet(Index, false)) {
		_FreeMemory++;
		_UsedMemory--;
		if (_RamBitmapIndex > Index)
			_RamBitmapIndex = Index;
	}
}

/*
 * AuPmmngrFreeBlocks -- Free multiple page frames
 * @param Addr -- Address of the first page frame
 * @param Count -- Number of blocks to be freed
 */
void AuPmmngrFreeBlocks(void* Addr, int Count) {
	uint64_t Address = (uint64_t)Addr;
	for (uint32_t i = 0; i < Count; i++) {
		AuPmmngrFree((void*)Address);
		Address += 0x1000;
	}
}

/*
 * P2V -- Physical to Virtual conversion
 * @param addr -- Address to convert
 */
uint64_t P2V(uint64_t addr) {
	if (_HigherHalf)
		return (PHYSICAL_MEM_BASE + addr);
	else
		return addr;
}

/*
 * V2P -- Virtual to Physical conversion
 * @param vaddr -- Address to convert
 */
uint64_t V2P(uint64_t vaddr) {
	if (_HigherHalf)
		return (vaddr - PHYSICAL_MEM_BASE);
	else
		return vaddr;
}

/*
* AuPmmngrMoveHigher -- moves the kernel to higher half
* of memory
*/
void AuPmmngrMoveHigher() {
	_HigherHalf = true;
	BitmapBuffer = (uint8_t*)P2V((uint64_t)BitmapBuffer);
}

/*
 * AuPmmngrGetFreeMem -- returns the total free amount
 * of RAM
 */
uint64_t AuPmmngrGetFreeMem() {
	return _FreeMemory;
}

/*
 * AuPmmngrGetTotalMem -- returns the total amount of
 * RAM
 */
uint64_t AuPmmngrGetTotalMem() {
	return _TotalRam;
}