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

#include <Mm/dma.h>
#include <Mm/kmalloc.h>
#include <_null.h>
#include <string.h>
#include <Drivers/uart.h>

#define DMA_DEFAULT_PAGE_BOUNDARY PAGE_SIZE

static inline size_t dma_align_up(size_t val, size_t align) {
	return (val + align - 1) & ~(align - 1);
}

static inline int is_power_of_two(size_t v) {
	return v && !(v & (v - 1));
}

static inline uint32_t __bitmap_bytes(uint32_t bits) {
	return (bits + 7) / 8;
}

static inline void __bitmap_set(uint8_t* bm, uint32_t bit) {
	bm[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

static inline void __bitmap_clear(uint8_t* bm, uint32_t bit) {
	bm[bit / 8] &= (uint8_t)~(1u << (bit % 8));
}

static inline int __bitmap_test(const uint8_t* bm, uint32_t bit) {
	return (bm[bit / 8] >> (bit % 8)) & 1;
}

/**
 * @brief _dma_pool_add_page -- add a backing page to pool
 * @param pool -- pointer to dma pool
 */
static AuDMAPage* _dma_pool_add_page(AuDMAPool* pool) {
	AuDMAPage* page = (AuDMAPage*)kmalloc(sizeof(AuDMAPage));
	if (!page)
		return NULL;
	memset(page, 0, sizeof(AuDMAPage));

	/**
	 * design should be such that, allocate a physical page
	 * map it to dma virtual address, with non cacheable bit
	 * and return, but for now aurora following very straigt
	 * forward method
	 */
	page->phys = (uint64_t)AuPmmngrAllocPages(1, 1, 0, AURORA_PAGE_DMA);
	if (!page->phys) {
		kfree(page);
		return NULL;
	}
	page->virt = (void*)P2V((uint64_t)page->phys);

	page->slots = pool->slots_per_page;
	page->bitmap = (uint8_t*)kmalloc(__bitmap_bytes(page->slots));
	if (!page->bitmap) {
		UARTDebugOut("[aurora]: dma-pool failed to allocate bitmap \r\n");
		AuPmmngrReleasePage((uint64_t)page->phys);
		kfree(page);
		return NULL;
	}
	memset(page->bitmap, 0, __bitmap_bytes(page->slots));
	page->in_use = 0;
	page->next = NULL;

	if (!pool->pages)
		pool->pages = page;
	else {
		AuDMAPage* tail = pool->pages;
		while (tail->next)
			tail = tail->next;
		tail->next = page;
	}

	return page;
}

/**
 * @brief _dma_page_find_slot -- find a free slot within a pool
 * @param page -- pointer to backing page struct
 * @param pool -- pointer to pool
 */
static int _dma_page_find_slot(const AuDMAPage* page, const AuDMAPool* pool) {
	for (uint32_t i = 0; i < page->slots; i++) {
		if (__bitmap_test(page->bitmap, i))
			continue; //already in use

		if (pool->boundary) {
			uint64_t slot_start = page->phys + (uint64_t)i * pool->alloc_sz;
			uint64_t slot_end = slot_start + pool->alloc_sz - 1;
			uint64_t mask = ~(uint64_t)(pool->boundary - 1);

			if ((slot_start & mask) != (slot_end & mask))
				continue;
		}
		return (int)i;
	}

	return -1;
}

/**
 * @brief AuDMAPoolCreate -- create a new pool 
 * @param name -- name of the pool
 * @param alloc_sz -- allocation unit size
 * @param align -- alignment to check
 * @param boundary -- ofcourse page boundary
 */
AuDMAPool* AuDMAPoolCreate(const char* name, size_t alloc_sz, size_t align, size_t boundary) {
	if (!alloc_sz)
		return NULL;
	if (!align)
		align = 1;
	if (!is_power_of_two(align))
		return NULL;
	if (boundary && !is_power_of_two(boundary))
		return NULL;

	if (align > 4096)
		return NULL;

	alloc_sz = dma_align_up(alloc_sz, align);

	if (alloc_sz > 4096)
		return NULL;

	if (boundary && alloc_sz > boundary)
		return NULL;

	AuDMAPool* pool = (AuDMAPool*)kmalloc(sizeof(AuDMAPool));
	if (!pool)
		return NULL;

	memset(pool, 0, sizeof(AuDMAPool));

	for (int i = 0; i < 31 && name && name[i]; i++)
		pool->name[i] = name[i];

	pool->alloc_sz = alloc_sz;
	pool->align = align;
	pool->boundary = boundary;
	pool->slots_per_page = (uint32_t)(0x1000 / alloc_sz);
	pool->pages = NULL;
	pool->total_allocs = 0;
	pool->total_frees = 0;

	return pool;
}

/**
 * @brief AuDMAPoolAlloc -- allocate a slot from desired pool
 * @param pool -- pointer to pool memory
 * @param phys_out -- where to store the physical address
 */
void* AuDMAPoolAlloc(AuDMAPool* pool, uint64_t* phys_out) {
	if (!pool || !phys_out)
		return NULL;

	AuDMAPage* page = pool->pages;
	int slot = -1;

	while (page) {
		if (page->in_use < page->slots) {
			slot = _dma_page_find_slot(page, pool);
			if (slot >= 0)
				break;
		}
		page = page->next;
	}

	if (slot < 0) {
		page = _dma_pool_add_page(pool);
		if (!page)
			return NULL;

		slot = _dma_page_find_slot(page, pool);
		if (slot < 0)
			return NULL;
	}

	__bitmap_set(page->bitmap, (uint32_t)slot);
	page->in_use++;
	pool->total_allocs++;

	uint64_t offset = (uint64_t)slot * pool->alloc_sz;
	void* virt = (uint8_t*)page->virt + offset;
	*phys_out = page->phys + offset;

	memset(virt, 0, pool->alloc_sz);
	return virt;
}

/**
 * @brief AuDMAPoolFree -- free up a slot within the pool
 * @param pool -- pointer to dma pool
 * @param virt -- virtual address to free
 * @param phys -- physical address to free
 */
void AuDMAPoolFree(AuDMAPool* pool, void* virt, uint64_t phys) {
	if (!pool || !virt)
		return;

	//LOCK acquire
	AuDMAPage* page = pool->pages;
	while (page) {
		if (phys >= page->phys && phys < page->phys + 4096)
			break;
		page = page->next;
	}

	if (!page) {
		//release lock
		return;
	}

	uint64_t offset = phys - page->phys;

	if (offset % pool->alloc_sz != 0) {
		//release lock
		return;
	}

	uint32_t slot = (uint32_t)(offset / pool->alloc_sz);

	if (slot >= page->slots || !__bitmap_test(page->bitmap, slot)) {
		//release lock
		return;
	}

	__bitmap_clear(page->bitmap, slot);
	page->in_use--;
	pool->total_frees++;
}

/**
 * @brief AuDMAPoolDestroy -- completely destroy a pool
 * @param pool -- pointer to pool
 */
void AuDMAPoolDestroy(AuDMAPool* pool) {
	if (!pool)
		return;

	//lock acquire

	AuDMAPage* page = pool->pages;
	while (page) {
		AuDMAPage* next = page->next;
		kfree(page->bitmap);
		AuPmmngrReleasePage((uint64_t)page->phys);
		kfree(page);
		page = next;
	}

	pool->pages = NULL;

	//release lock
	kfree(pool);
}

static const size_t szClasses[DMA_NUM_CLASSES] = {8, 16, 32, 64, 128, 256, 512};

/**
 * @brief AuDMAGlobalClassInitialize -- initialize and populate global
 * class structure
 * @param gclass -- Pointer to allocated gclass 
 * @param name -- suitable gclass name
 */
void AuDMAGlobalClassInitialize(AuDMAGlobalClass* gclass, char* name) {
	gclass->szClass[0] = 8;
	gclass->szClass[1] = 16;
	gclass->szClass[2] = 32;
	gclass->szClass[3] = 64;
	gclass->szClass[4] = 128;
	gclass->szClass[5] = 256;
	gclass->szClass[6] = 512;

	for (int i = 0; i < DMA_NUM_CLASSES; i++) {
		size_t sz = gclass->szClass[i];
		char title[32];
		int strcount = strlen(name);
		if (strcount >= 29)
			strcount = 29;

		title[strcount + 1] = '0' + (char)i;
		title[strcount + 2] = '\0';

		gclass->pools[i] = AuDMAPoolCreate(title, sz, sz, DMA_DEFAULT_PAGE_BOUNDARY);
	}
}

static int _dma_gclass_find_class(AuDMAGlobalClass* gClass, size_t sz) {
	if (sz == 0)
		return 1;
	uint64_t size = sz;
	size--;
	size |= size >> 1;
	size |= size >> 2;
	size |= size >> 4;
	size |= size >> 8;
	size |= size >> 16;
	size |= size >> 32;
	size++;

	if (size < 8)
		size = 8;

	if (size > DMA_MAX_POOL_SIZE)
		return -1;

	for (int i = 0; i < DMA_NUM_CLASSES; i++) {
		if (size <= gClass->szClass[i])
			return i;
	}
	return -1;
}

/**
 * @brief AuDMAGClassAlloc -- allocate memory from DMA Global Class
 * @param gClass -- pointer to gClass
 * @param sz -- size to allocate
 * @param physOut -- where to put the physical output
 */
void* AuDMAGClassAlloc(AuDMAGlobalClass* gClass, size_t sz, uint64_t* physOut) {
	if (!sz || !physOut)
		return NULL;

	int idx = _dma_gclass_find_class(gClass, sz);

	if (idx < 0) {
		/** return a full 4KiB page for larger than MAX_POOL_SZ */
		uint64_t phys = (uint64_t)AuPmmngrAllocPages(1, 1, 0, AURORA_PAGE_DMA);
		if (!phys)
			return NULL;
		*physOut = phys;
		return (void*)P2V(phys);
	}

	/** or allocate it using pool allocator */
	return AuDMAPoolAlloc(gClass->pools[idx], physOut);
}

/**
 * @brief AuDMAGClassFree -- free a memory and put it to gclass pool
 * @param gClass -- pointer to global class
 * @param virt -- virtual address
 * @param physOut -- physical address to put
 * @param sz -- size of the allocated memory
 */
void AuDMAGClassFree(AuDMAGlobalClass* gClass, void* virt, uint64_t physOut, size_t sz) {
	if (!virt)
		return;

	int idx = _dma_gclass_find_class(gClass, sz);

	if (idx < 0) {
		AuPmmngrReleasePage((uint64_t)physOut);
		return;
	}

	AuDMAPoolFree(gClass->pools[idx], virt, physOut);
}

/**
 * @brief AuDMAGClassDestroy -- destroy entire dma global class
 * @param gClass -- pointer to gClass
 */
void AuDMAGClassDestroy(AuDMAGlobalClass* gClass) {
	for (int i = 0; i < DMA_NUM_CLASSES; i++) {
		AuDMAPoolDestroy(gClass->pools[i]);
		gClass->pools[i] = 0;
	}
}
