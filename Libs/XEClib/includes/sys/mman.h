/**
* @file mman.h
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

#ifndef __MMAN_H__
#define __MMAN_H__

#include <stdint.h>
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#define PAGE_SIZE 4096

#define MEMMAP_NO_FILEDESC -1

	/* memmap -- flags */
#define PROTECTION_FLAG_READONLY 1 << 0
#define PROTECTION_FLAG_WRITE	 1 << 1
#define PROTECTION_FLAG_NO_EXEC	 1 << 2
#define PROTECTION_FLAG_NO_CACHE 1 << 3

#define MEMMAP_FLAG_SHARED			  1 << 0
#define MEMMAP_FLAG_COW				  1 << 1
#define MEMMAP_FLAG_PRIVATE			  1 << 2
#define MEMMAP_FLAG_DISCARD_FILE_READ 1 << 3

	/**
	* @brief _KeMemMap -- memory map
	* @param address -- starting address
	* @param length -- length of the mappings
	* @param protect -- protection flags
	* @param flags -- memory map flags
	* @param filedesc -- file descriptor to map
	* @param offset -- offset from where to begin, it should be multiple of PAGE_SIZE
	*/
	XE_LIB void* _KeMemMap(
		void* address, size_t length, int protect, int flags, int filedesc, uint64_t offset);

	/**
	 * @brief _KeMemUnmap -- unmaps a memory segment
	 * @param address -- starting address of mapped address range
	 * @param len -- length of the address mapped range
	 */
	XE_LIB void _KeMemUnmap(void* address, size_t len);

	/**
	* @brief _KeCreateSharedMem -- create a shared memory chunk
	* @param key -- key to use
	* @param sz -- memory size
	* @param flags -- shared memory flags
	*/
	XE_LIB int _KeCreateSharedMem(uint16_t key, size_t sz, uint8_t flags);

	/**
	* @brief _KeObtainSharedMem -- obtain a shared memory
	* @param id -- segment id
	* @param shmaddr -- user specified address
	* @param shmflg -- flags to use for protection
	*/
	XE_LIB void* _KeObtainSharedMem(uint16_t id, void* shmaddr, int shmflg);

	/**
	* @brief _KeUnmapSharedMem -- unmap shared memory segment
	* @param key -- key to search
	*/
	XE_LIB void _KeUnmapSharedMem(uint16_t key);

	/**
	 * @brief _KeGetProcessHeapMem -- request a new memory from
	 * process heap
	 * @param sz -- size in bytes
	 */
	XE_LIB uint64_t _KeGetProcessHeapMem(size_t sz);

	/**
	* @brief _KeMemMapDirty -- dirty update previously allocated memory map
	* @param startAddr -- starting address
	* @param len -- length in bytes
	* @param flags -- memory map flags
	* @param prot -- protection flags
	*/
	XE_LIB void _KeMemMapDirty(void* startAddr, size_t len, int flags, int prot);

	/**
	* @brief _KeProcessHeapUnmap -- unmaps previosly allocated
	* heap memory
	* @param ptr -- Pointer to freeable address
	* @param sz -- size in bytes to unallocate
	*/
	XE_LIB int _KeProcessHeapUnmap(void* ptr, size_t sz);

#ifdef __cplusplus
}
#endif

#endif
