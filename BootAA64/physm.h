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

#ifndef __PHYSM_H__
#define __PHYSM_H__

#include "xnldr.h"
#include "xnout.h"
#include <Uefi.h>

typedef EFI_PHYSICAL_ADDRESS paddr_t;
#define PAGESIZE	4096
#define PADDR_T_MAX UINT64_MAX

/*
 * XEInitialisePmmngr - Initialise Physical Memory Manager
 * @param memmap -- Pointer to memory map
 * @param buffer -- Pointer to memory area where
 * physical addresses will get stored
 * @param bufsize -- size of the buffer
 */
extern void XEInitialisePmmngr(const struct EfiMemoryMap memmap, void* buffer, size_t bufsize);

/*
 * XEPmmngrAllocate -- Allocates a physical block
 */
extern paddr_t XEPmmngrAllocate();

/*
 * XEPmmngrFree -- free up
 * previously allocated physical block
 * @param addr -- Pointer to physical address
 */
extern void XEPmmngrFree(paddr_t addr);

/*
 * XEPmmngrList -- list all available physical block
 */
extern void XEPmmngrList();

/*
 *XEGetAlstack -- return the allocated stack ptr
 */
extern paddr_t* XEGetAlstack();

/*
 * XEGetAlstackptr -- return the previously allocated
 * pointer
 */
extern paddr_t* XEGetAlstackptr();

/*
 * XEReserveMemCount -- returns reserved memory area,
 * i.e all allocated memory for the kernel and files
 */
extern uint64_t XEReserveMemCount();

/*
 * XEGetPgStack -- return the entire page stack
 */
extern paddr_t* XEGetPgStack();

extern paddr_t* XEGetStackPtr();

/*
 * XEGetPhysicalStart -- return the starting address
 * from where all usable ram starts
 */
extern uint64_t XEGetPhysicalStart();

/*
 * XEGetPhysicalSize -- return the size of entire
 * usable area
 */
extern uint64_t XEGetPhysicalSize();

/*
 * XEGetRamSize -- get the total physical memory
 * size
 */
extern uint64_t XEGetRamSize();
#endif
