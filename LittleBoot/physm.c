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

#include "physm.h"
#include "string.h"

paddr_t* pagestack;
paddr_t* stackptr;
paddr_t* allocatedStack;
paddr_t* allocatedPtr;
uint32_t allocatedCount;
void* stackBuffer;
#define RAW_DIFF(p1,p2) ((uint64_t)p1 - (uint64_t)p2)
/*
 * LBInitializePmmngr -- initialize the physical memory manager
 * @param map -- pointer to memory map area
 * @param num_entries -- number of memory map entries
 * @param buffer -- Pointer to buffer where to store pmmngr data
 * @param buffsize -- pmmngr internal data size
 */
void LBInitializePmmngr(memory_region_t* map, uint64_t num_entries, void*buffer, size_t buffsize){
    memset(buffer, 0, buffsize);
    stackptr = pagestack = (paddr_t*)buffer;
    stackBuffer = buffer;
    buffsize /= 2;
    allocatedPtr = allocatedStack = ((paddr_t*)buffer + buffsize);
    allocatedCount = 1;
    
    for (int i = 0;  i < num_entries; i++){
        uint64_t base = map[i].base;
        uint64_t size = map[i].size;
        uint64_t numpages = map[i].page_count;
        while(numpages > 0 && RAW_DIFF(stackptr, pagestack) < buffsize){
            *stackptr++ = base;
            --numpages;
            base += 0x1000;
        }
        if (RAW_DIFF(stackptr, pagestack) >= buffsize)
           break;
    }
    LBUartPutString("Physical Memory Manager initialized \n");
}

/*
 * LBPmmngrAlloc -- allocate a physical block
 */
paddr_t LBPmmngrAlloc(){
    if (stackptr == pagestack)
        return 0;
    else {
        paddr_t allocated = *--stackptr;
        *allocatedPtr++ = allocated;
        allocatedCount++;
        return allocated;
    }
}

void LBPmmngrFree(paddr_t addr){
    uint64_t *allocScan = stackBuffer;
    char found = 0;
    for (uint64_t* p = stackBuffer; p < allocatedPtr; ++p){
        if (*p == addr){
            found = 1;
            allocScan = p;
            break;
        }
    }

    if (!found)
        return;

    allocatedPtr--;
    *allocScan = * allocatedPtr;
    *stackptr++ = addr;

}

void* LBPmmngrGetAllocatedPtr(){
    return allocatedPtr;
}

void* LBPmmngrGetStackPtr(){
    return stackptr;
}

uint32_t LBPmmngrGetReservedMemCount(){
    return allocatedCount;
}