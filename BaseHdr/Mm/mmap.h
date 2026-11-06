/**
* @file mmap.h
* 
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

#ifndef __MMAP_H__
#define __MMAP_H__

#include <stdint.h>
#include <string.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <process.h>

#define MAP_ANONYMOUS 0
#define MAP_PRIVATE 0
#define MAP_FAILED ((void*)-1)
#define PROT_READ 0
#define PROT_WRITE 0
#define O_RDWR 0

/**
 * @brief SharedMemMapListInitialise -- initialise
 * the shared memory map list
 */
extern void SharedMemMapListInitialise();

/**
 * @brief CreateMemMapping -- Create a memory mapping of just memory, file or device
 * @param address -- address from where mapping start, if null, kernel will
 * find by its own
 * @param len -- length of the address
 * @param prot -- protection flags
 * @param flags -- flags
 * @param fd -- file descriptor, -1 for no file descriptor
 * @param offset -- byte offset for file and device
 * @return Pointer to starting
 */
extern void* CreateMemMapping(void* address, size_t len, int prot, int flags, int fd,
	uint64_t offset);

/**
 * @brief MemMapDirty -- dirty update previously allocated memory map
 * @param startingVaddr -- starting address
 * @param len -- length in bytes
 * @param flags -- memory map flags
 * @param prot -- protection flags
 */
extern void MemMapDirty(void* startingVaddr, size_t len, int flags, int prot);
/**
 * @brief UnmapMemMapping -- unmaps a memory mapping
 * @param address -- address from where mapping starts
 * @param len -- length of the mapping
 */
extern void UnmapMemMapping(void* address, size_t len);
#endif