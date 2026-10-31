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

#include <Mm/buddy.h>
#include <Mm/kmalloc.h>
#include <Mm/vmmngr.h>
#include <stdint.h>
#include <aucon.h>
#include <_null.h>

/*
 * NOT COMPLETED YET!!! NOT IN USED
 */

#if 0

#define MAX_LEVELS 7
#define BLOCKS_PER_LEVEL(level) (1<<(level))
#define SIZE_OF_BLOCKS_AT_LEVEL(level,total_size) (total_size / (1<<(level)))
#define MIN_ALLOC 64
#define BUDDY_HEADER_SIZE 32  //32 byte header size

static BuddyFreeList __BuddyFreeList[MAX_LEVELS-1];
void* __BuddyMemoryStart;

#define INDEX_OF_POINTER_IN_LEVEL(pointer,level,memory_start,total_size) \
	((((uint8_t*)pointer) - ((uint8_t*)memory_start)) / (SIZE_OF_BLOCKS_AT_LEVEL(level, total_size)))

/*
 * GetRoundOffNum -- converts a given number to nearest
 * power 2 value
 * @param value -- number to convert
 */
size_t GetRoundOffNum(size_t value) {
	value--;
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	value++;
	return value;
}

/*
 * AuBuddyInsert -- insert a block to free list
 * @param free_list -- Pointer to free list slot
 * @param block -- Block to insert
 */
void AuBuddyInsert(BuddyFreeList *free_list, BuddyBlock *block) {
	block->next = NULL;
	block->prev = NULL;
	block->next = free_list->first;
	if (free_list->first)
		free_list->first->prev = block;
	free_list->first = block;
}

BuddyBlock* AuGetFreeBlock(BuddyFreeList *free_list) {
	return free_list->first;
}

/*
 * AuBuddyIsFreeListEmpty -- checks if the free list is empty or not
 * @param free_list -- free list to check
 */
bool AuBuddyIsFreeListEmpty(BuddyFreeList* free_list) {
	if (!free_list->first)
		return true;
	else
		return false;
}

/*
 * AuBuddyRemoveFreeListHead -- return a free block
 * @param free_list -- Pointer to the free list
 */
void* AuBuddyRemoveFreeListHead(BuddyFreeList* free_list) {
	BuddyBlock* ret_block = free_list->first;
	BuddyBlock *new_block = free_list->first->next;
	if (new_block)
		new_block->prev = NULL;
	free_list->first = new_block;
	return ret_block;
}


/*
 * AuBuddyGetLevel -- Return the buddy level by its size
 * @param size -- required size
 */
size_t AuBuddyGetLevel(size_t size) {
	size_t sz = size + sizeof(BuddyBlock);
	/* Round off the value to nearest power of 2 */
	size_t actual_block_sz = GetRoundOffNum(sz);
	size_t max_buddy = MAX_LEVELS ;
	size_t min_sz = MIN_ALLOC;
	while (min_sz < actual_block_sz) {
		max_buddy--;
		min_sz *= 2;
	}

	return max_buddy - 1;
}

/*
 * AuBuddyInitialise -- initialise the buddy allocator
 */
void AuBuddyInitialise() {

	for (int i = 0; i < MAX_LEVELS - 1; i++)
		__BuddyFreeList[i].first = NULL;

	BuddyBlock* block = (BuddyBlock*)au_request_page(1);
	block->buddy_magic = BUDDY_MAGIC;
	size_t size_at_level = SIZE_OF_BLOCKS_AT_LEVEL(0, PAGE_SIZE);
	block->size = size_at_level;
	AuBuddyInsert(&__BuddyFreeList[0], block);
	__BuddyMemoryStart = (void*)block;
}

/* 
 * __AuBuddyAlloc -- allocates a memory from given level
 * @param level -- level to check
 */
void* __AuBuddyAlloc(size_t level) {
	bool bottom_level = false;
	/* memory should be unsigned char* aligned (1byte) */
	uint8_t* memory = NULL;
	size_t size_at_level = 0;
	if (level == 0)
		bottom_level = true;

	if (AuBuddyIsFreeListEmpty(&__BuddyFreeList[level])) {
		uint8_t grab_level = level - 1;
		
		if (!bottom_level) {
			memory = (uint8_t*)__AuBuddyAlloc(level - 1);
			if (!memory)
				return NULL;
		}
		else {
			memory = (uint8_t*)au_request_page(1);
			if (!memory)
				return NULL;
		}

		size_at_level = SIZE_OF_BLOCKS_AT_LEVEL(level, PAGE_SIZE);

		/*
		 * if bottom level is true, we should allocate a 
		 * new page in it, if false then simply
		 * make division in the memory we get
		 */
		BuddyBlock *block_left = (BuddyBlock*)memory;
		block_left->size = size_at_level;
		block_left->buddy_magic = BUDDY_MAGIC;
		AuBuddyInsert(&__BuddyFreeList[level], block_left);

		if (!bottom_level) {
			BuddyBlock *block_right = (BuddyBlock*)(memory + size_at_level);
			block_right->size = size_at_level;
			block_right->buddy_magic = BUDDY_MAGIC;
			AuBuddyInsert(&__BuddyFreeList[level], block_right);
		}
	}

	BuddyBlock* block = (BuddyBlock*)AuBuddyRemoveFreeListHead(&__BuddyFreeList[level]);
	memory = (uint8_t*)block;
	return memory;
}

/*
 * AuBuddyAlloc -- Allocate some memory
 * @param sz -- Size to allocate
 */
void* AuBuddyAlloc(size_t sz) {
	size_t level = AuBuddyGetLevel(sz);
	AuTextOut("Request level -> %d sz -> %d \n", level, GetRoundOffNum(sz + sizeof(BuddyBlock)));
	uint8_t* mem = (uint8_t*)__AuBuddyAlloc(level);
	return (mem + sizeof(BuddyBlock));
}

#define BUDDY_CHECK_MAX(a,b)   ((a) > (b) ? (a) : (b))
#define BUDDY_CHECK_MIN(a,b)   ((a) < (b) ? (a) : (b))
/*
 * AuBuddyFree -- frees up an allocated
 * object
 * @param p -- Pointer to memory block
 */
void AuBuddyFree(void* p) {
	uint8_t* mem = (uint8_t*)p;
	BuddyBlock* blk = (BuddyBlock*)(mem - sizeof(BuddyBlock));
	void* pointer = (void*)blk;
	if (blk->buddy_magic != BUDDY_MAGIC)
		return;

	size_t level = AuBuddyGetLevel(blk->size - sizeof(BuddyBlock));

	if (!AuBuddyIsFreeListEmpty(&__BuddyFreeList[level]) && level != 0){
		/**
		 *
		 *    NEEDS WORKING HEREEE!!!! NOT PERFECT!!! BUGS ARE THERE
		 *
		 */
		BuddyBlock* mergeable_blk = (BuddyBlock*)BUDDY_CHECK_MAX((size_t)__BuddyFreeList[level].first, (size_t)blk);

		/* Call this two times to remove the next block */
		AuBuddyRemoveFreeListHead(&__BuddyFreeList[level]);
		AuBuddyRemoveFreeListHead(&__BuddyFreeList[level]);

		mergeable_blk->prev = NULL;
		mergeable_blk->size *= 2;
		mergeable_blk->buddy_magic = BUDDY_MAGIC;
		mergeable_blk->head = (void*)((uint8_t*)mergeable_blk + sizeof(BuddyBlock));
		mergeable_blk->next = NULL;
		size_t new_level = AuBuddyGetLevel(mergeable_blk->size - sizeof(BuddyBlock));

		AuBuddyInsert(&__BuddyFreeList[new_level], mergeable_blk);

		AuBuddyFree(((uint8_t*)mergeable_blk + sizeof(BuddyBlock)));
	}
	else if (level == 0)
		AuBuddyInsert(&__BuddyFreeList[level], blk);

}

#endif