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

#ifndef __PHYSICAL_MMNGR_H__
#define __PHYSICAL_MMNGR_H__

#include "littleboot.h"

typedef uint64_t paddr_t;

/*
 * LBInitializePmmngr -- initialize the physical memory manager
 * @param map -- pointer to memory map area
 * @param num_entries -- number of memory map entries
 * @param buffer -- Pointer to buffer where to store pmmngr data
 * @param buffsize -- pmmngr internal data size
 */
extern void LBInitializePmmngr(memory_region_t* map, uint64_t num_entries, void*buffer, size_t buffsize);

/*
 * LBPmmngrAlloc -- allocate a physical block
 */
extern paddr_t LBPmmngrAlloc();

extern void LBPmmngrFree(paddr_t addr);

extern void* LBPmmngrGetAllocatedPtr();

extern void* LBPmmngrGetStackPtr();

extern uint32_t LBPmmngrGetReservedMemCount();

#endif