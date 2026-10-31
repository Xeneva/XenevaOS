/**
* @file dma.c
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

#ifndef __DMA_H__
#define __DMA_H__

#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <aurora.h>

typedef struct _au_dma_page_ {
	void* virt;
	uint64_t phys;
	uint32_t in_use;
	uint32_t slots;
	uint8_t* bitmap;
	struct _au_dma_page_* next;
}AuDMAPage;

typedef struct _au_dma_pool_ {
	char name[32];
	size_t alloc_sz;
	size_t align;
	size_t boundary;
	uint32_t slots_per_page;
	AuDMAPage* pages;
	uint64_t total_allocs;
	uint64_t total_frees;
}AuDMAPool;

#define DMA_NUM_CLASSES 7
#define DMA_MAX_POOL_SIZE 512

typedef struct _au_dma_global_class_{
	AuDMAPool* pools[DMA_NUM_CLASSES];
	size_t szClass[DMA_NUM_CLASSES];
}AuDMAGlobalClass;


/**
 * @brief AuDMAPoolCreate -- create a new pool
 * @param name -- name of the pool
 * @param alloc_sz -- allocation unit size
 * @param align -- alignment to check
 * @param boundary -- ofcourse page boundary
 */
AU_EXTERN AU_EXPORT AuDMAPool* AuDMAPoolCreate(const char* name, size_t alloc_sz, size_t align, size_t boundary);

/**
 * @brief AuDMAPoolAlloc -- allocate a slot from desired pool
 * @param pool -- pointer to pool memory
 * @param phys_out -- where to store the physical address
 */
AU_EXTERN AU_EXPORT void* AuDMAPoolAlloc(AuDMAPool* pool, uint64_t* phys_out);

/**
 * @brief AuDMAPoolFree -- free up a slot within the pool
 * @param pool -- pointer to dma pool
 * @param virt -- virtual address to free
 * @param phys -- physical address to free
 */
AU_EXTERN AU_EXPORT void AuDMAPoolFree(AuDMAPool* pool, void* virt, uint64_t phys);

/**
 * @brief AuDMAPoolDestroy -- completely destroy a pool
 * @param pool -- pointer to pool
 */
AU_EXTERN AU_EXPORT void AuDMAPoolDestroy(AuDMAPool* pool);

/**
 * @brief AuDMAGlobalClassInitialize -- initialize and populate global
 * class structure
 * @param gclass -- Pointer to allocated gclass
 * @param name -- suitable gclass name
 */
AU_EXTERN AU_EXPORT void AuDMAGlobalClassInitialize(AuDMAGlobalClass* gclass, char* name);

/**
 * @brief AuDMAGClassAlloc -- allocate memory from DMA Global Class
 * @param gClass -- pointer to gClass
 * @param sz -- size to allocate
 * @param physOut -- where to put the physical output
 */
AU_EXTERN AU_EXPORT void* AuDMAGClassAlloc(AuDMAGlobalClass* gClass, size_t sz, uint64_t* physOut);

/**
 * @brief AuDMAGClassFree -- free a memory and put it to gclass pool
 * @param gClass -- pointer to global class
 * @param virt -- virtual address
 * @param physOut -- physical address to put
 * @param sz -- size of the allocated memory
 */
AU_EXTERN AU_EXPORT void AuDMAGClassFree(AuDMAGlobalClass* gClass, void* virt, uint64_t physOut, size_t sz);

/**
 * @brief AuDMAGClassDestroy -- destroy entire dma global class
 * @param gClass -- pointer to gClass
 */
AU_EXTERN AU_EXPORT void AuDMAGClassDestroy(AuDMAGlobalClass* gClass);


#endif

