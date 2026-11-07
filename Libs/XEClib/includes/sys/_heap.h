/**
* @file _heap.h
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

#ifndef _LIBALLOC_H
#define _LIBALLOC_H

#include <stdint.h>
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	/** This is a boundary tag which is prepended to the
	* page or section of a page which we have allocated. It is
	* used to identify valid memory blocks that the
	* application is trying to free.
	*/
	//#pragma pack(push,1)
	struct boundary_tag {
		unsigned int magic;		//< It's a kind of ...
		unsigned int size;		//< Requested size.
		unsigned int real_size; //< Actual size.
		int index;				//< Location in the page table.

		struct boundary_tag* split_left;  //< Linked-list info for broken pages.
		struct boundary_tag* split_right; //< The same.

		struct boundary_tag* next; //< Linked list info.
		struct boundary_tag* prev; //< Linked list info.
	};
	//#pragma pack(pop)

	/** This function is supposed to lock the memory data structures. It
	* could be as simple as disabling interrupts or acquiring a spinlock.
	* It's up to you to decide.
	*
	* \return 0 if the lock was acquired successfully. Anything else is
	* failure.
	*/
	XE_LIB int liballoc_lock();

	/** This function unlocks what was previously locked by the liballoc_lock
	* function.  If it disabled interrupts, it enables interrupts. If it
	* had acquiried a spinlock, it releases the spinlock. etc.
	*
	* \return 0 if the lock was successfully released.
	*/
	XE_LIB int liballoc_unlock();

	/** This is the hook into the local system which allocates pages. It
	* accepts an integer parameter which is the number of pages
	* required.  The page size was set up in the liballoc_init function.
	*
	* \return NULL if the pages were not allocated.
	* \return A pointer to the allocated memory.
	*/
	XE_LIB void* liballoc_alloc(int);

	/** This frees previously allocated memory. The void* parameter passed
	* to the function is the exact same value returned from a previous
	* liballoc_alloc call.
	*
	* The integer value is the number of pages to free.
	*
	* \return 0 if the memory was successfully freed.
	*/
	XE_LIB int liballoc_free(void*, int);

	XE_LIB void* malloc(unsigned int);							 //< The standard function.
	XE_LIB void* realloc(void*, unsigned int);					 //< The standard function.
	XE_LIB void* calloc(unsigned long long, unsigned long long); //< The standard function.
	XE_LIB void free(void*);									 //< The standard function.

#ifdef __cplusplus
}
#endif

#endif
