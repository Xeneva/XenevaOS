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

#ifndef __BUDDY_H__
#define __BUDDY_H__

#include <stdint.h>

#if 0

#define BUDDY_MAGIC 0x160602
/*
 * BuddyBlock size should be 
 * aligned to 32 byte 
 */
#pragma pack(push,1)
typedef struct _buddy_block_ {
	_buddy_block_ *next;
	_buddy_block_ *prev;
	void* head;
	uint32_t buddy_magic;
	uint32_t size;
}BuddyBlock;
#pragma pack(pop)

typedef struct _buddy_free_list_ {
	BuddyBlock *first;
}BuddyFreeList;


/*
* AuBuddyInitialise -- initialise the buddy allocator
*/
extern void AuBuddyInitialise();

/*
* AuBuddyGetLevel -- Return the buddy level by its size
* @param size -- required size
*/
extern size_t AuBuddyGetLevel(size_t size);

/*
* __AuBuddyAlloc -- allocates a memory from given level
* @param level -- level to check
*/
extern void* __AuBuddyAlloc(size_t level);

/*
* AuBuddyAlloc -- Allocate some memory
* @param sz -- Size to allocate
*/
extern void* AuBuddyAlloc(size_t sz);

/*
* AuBuddyFree -- frees up an allocated
* object
* @param p -- Pointer to memory block
*/
extern void AuBuddyFree(void* p);

#endif

#endif