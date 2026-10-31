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

#include <aurora.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Mm/liballoc/liballoc.h>
#include <_null.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <Hal/AA64/profile.h>
#include <Hal/AA64/aa64cpu.h>

/**  Durand's Ridiculously Amazing Super Duper Memory functions.  */

//#define DEBUG

#define LIBALLOC_MAGIC 0xc001c0de
#define MAXCOMPLETE	   5
#define MAXEXP		   32
#define MINEXP		   8

#define MODE_BEST	 0
#define MODE_INSTANT 1

#define MODE MODE_BEST

#ifdef DEBUG
#include <stdio.h>
#endif

struct boundary_tag* l_freePages[MAXEXP]; //< Allowing for 2^MAXEXP blocks
int l_completePages[MAXEXP];			  //< Allowing for 2^MAXEXP blocks

#ifdef DEBUG
unsigned int l_allocated = 0; //< The real amount of memory allocated.
unsigned int l_inuse = 0;	  //< The amount of memory in use (malloc'ed).
#endif

static int l_initialized = 0; //< Flag to indicate initialization.
static int l_pageSize = 4096; //< Individual page size
static int l_pageCount = 1;	  //< Minimum number of pages to allocate.

// ***********   HELPER FUNCTIONS  *******************************

/** Returns the exponent required to manage 'size' amount of memory.
*
*  Returns n where  2^n <= size < 2^(n+1)
*/
static inline int getexp(unsigned int size) {
	if (size < (1 << MINEXP)) {
#ifdef DEBUG
		printf("getexp returns -1 for %i less than MINEXP\n", size);
#endif
		return -1; // Smaller than the quantum.
	}

	int shift = MINEXP;

	while (shift < MAXEXP) {
		if ((1 << shift) > size)
			break;
		shift += 1;
	}

#ifdef DEBUG
	printf("getexp returns %i (%i bytes) for %i size\n", shift - 1, (1 << (shift - 1)), size);
#endif

	return shift - 1;
}

static void* liballoc_memset(void* s, int c, size_t n) {
	int i;
	for (i = 0; i < n; i++)
		((char*)s)[i] = c;

	return s;
}

static void* liballoc_memcpy(void* s1, const void* s2, size_t n) {
	char* cdest;
	char* csrc;
	unsigned int* ldest = (unsigned int*)s1;
	unsigned int* lsrc = (unsigned int*)s2;

	while (n >= sizeof(unsigned int)) {
		*ldest++ = *lsrc++;
		n -= sizeof(unsigned int);
	}

	cdest = (char*)ldest;
	csrc = (char*)lsrc;

	while (n > 0) {
		*cdest++ = *csrc++;
		n -= 1;
	}

	return s1;
}

#ifdef DEBUG
static void dump_array() {
	int i = 0;
	struct boundary_tag* tag = NULL;

	printf("------ Free pages array ---------\n");
	printf("System memory allocated: %i\n", l_allocated);
	printf("Memory in used (malloc'ed): %i\n", l_inuse);

	for (i = 0; i < MAXEXP; i++) {
		printf("%.2i(%i): ", i, l_completePages[i]);

		tag = l_freePages[i];
		while (tag != NULL) {
			if (tag->split_left != NULL)
				printf("*");
			printf("%i", tag->real_size);
			if (tag->split_right != NULL)
				printf("*");

			printf(" ");
			tag = tag->next;
		}
		printf("\n");
	}

	printf("'*' denotes a split to the left/right of a tag\n");
	fflush(stdout);
}
#endif

static inline void insert_tag(struct boundary_tag* tag, int index) {
	int realIndex = 0;

	if (index < 0) {
		realIndex = getexp(tag->real_size - sizeof(struct boundary_tag));

		if (realIndex < MINEXP)
			realIndex = MINEXP;
	} else
		realIndex = index;

	tag->index = realIndex;

	if (l_freePages[realIndex] != NULL) {
		l_freePages[realIndex]->prev = tag;
		tag->next = l_freePages[realIndex];
	}

	l_freePages[realIndex] = tag;
	dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();
}

static inline void remove_tag(struct boundary_tag* tag) {
	if (l_freePages[tag->index] == tag)
		l_freePages[tag->index] = tag->next;

	if (tag->prev != NULL)
		tag->prev->next = tag->next;
	if (tag->next != NULL)
		tag->next->prev = tag->prev;

	tag->next = NULL;
	tag->prev = NULL;
	tag->index = -1;
	dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();
}

static inline struct boundary_tag* melt_left(struct boundary_tag* tag) {
	struct boundary_tag* left = tag->split_left;

	dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();

	left->real_size += tag->real_size;
	left->split_right = tag->split_right;

	if (tag->split_right != NULL)
		tag->split_right->split_left = left;

	dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();

	return left;
}

static inline struct boundary_tag* absorb_right(struct boundary_tag* tag) {
	struct boundary_tag* right = tag->split_right;

	dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();

	remove_tag(right); // Remove right from free pages.

	tag->real_size += right->real_size;

	tag->split_right = right->split_right;
	if (right->split_right != NULL)
		right->split_right->split_left = tag;

	dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();

	return tag;
}

static inline struct boundary_tag* split_tag(struct boundary_tag* tag) {
	unsigned int alignedSz = (tag->size + 7) & ~7;
	unsigned int remainder = tag->real_size - sizeof(struct boundary_tag) - alignedSz;

	size_t new_tag_addr = (size_t)tag + sizeof(struct boundary_tag) + alignedSz;
	new_tag_addr = (new_tag_addr + 7) & ~7;
	struct boundary_tag* new_tag = (struct boundary_tag*)new_tag_addr;

	//(struct boundary_tag*)((size_t)tag + sizeof(struct boundary_tag) + tag->size);
	new_tag->magic = LIBALLOC_MAGIC;
	new_tag->real_size = remainder;

	new_tag->next = NULL;
	new_tag->prev = NULL;

	new_tag->split_left = tag;
	new_tag->split_right = tag->split_right;

	dmb_sy();
	dsb_sy_barrier();
	dsb_ish();

	if (new_tag->split_right != NULL)
		new_tag->split_right->split_left = new_tag;
	tag->split_right = new_tag;

	tag->real_size -= new_tag->real_size;

	dmb_sy();
	dsb_sy_barrier();
	dsb_ish();

	insert_tag(new_tag, -1);
	return new_tag;
}

// ***************************************************************

extern bool isSyscall();

static struct boundary_tag* allocate_new_tag(unsigned int size) {
	size = (size + 0xF) & ~0xF;

	unsigned int pages;
	unsigned int usage;
	struct boundary_tag* tag;

	// This is how much space is required.
	usage = size + sizeof(struct boundary_tag);

	// Perfect amount of space
	pages = usage / l_pageSize;
	if ((usage % l_pageSize) != 0)
		pages += 1;

	// Make sure it's >= the minimum size.
	if (pages < l_pageCount)
		pages = l_pageCount;
	/*if (isSyscall())
		UARTDebugOut("allocate_new_tag calling liballoc_alloc \n");*/
	tag = (struct boundary_tag*)liballoc_alloc(pages);

	/*if (isSyscall()) {
		UARTDebugOut("isSyscall() allocate_new_tag \n");
		return;
	}*/

	if (tag == NULL)
		return NULL; // uh oh, we ran out of memory.

	tag->magic = LIBALLOC_MAGIC;
	tag->size = size;
	tag->real_size = pages * l_pageSize;
	tag->index = -1;

	tag->next = NULL;
	tag->prev = NULL;
	tag->split_left = NULL;
	tag->split_right = NULL;

#ifdef DEBUG
	printf("Resource allocated %x of %i pages (%i bytes) for %i size.\n",
		   tag,
		   pages,
		   pages * l_pageSize,
		   size);

	l_allocated += pages * l_pageSize;

	printf("Total memory usage = %i KB\n", (int)((l_allocated / (1024))));
#endif

	dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();

	return tag;
}

void* port_malloc(unsigned int size) {
	size = (size + 7) & ~7;

	int index;
	void* ptr;
	struct boundary_tag* tag = NULL;

	liballoc_lock();

	if (l_initialized == 0) {
#ifdef DEBUG
		printf("%s\n", "liballoc initializing.");
#endif
		for (index = 0; index < MAXEXP; index++) {
			l_freePages[index] = NULL;
			l_completePages[index] = 0;
		}
		l_initialized = 1;
	}

	index = getexp(size) + MODE;

	if (index < MINEXP)
		index = MINEXP;

	// Find one big enough.
	tag = l_freePages[index]; // Start at the front of the list.

	while (tag != NULL) {
		// If there's enough space in this tag.
		if ((tag->real_size - sizeof(struct boundary_tag)) >=
			(size + sizeof(struct boundary_tag))) {
#ifdef DEBUG
			printf("Tag search found %i >= %i\n",
				   (tag->real_size - sizeof(struct boundary_tag)),
				   (size + sizeof(struct boundary_tag)));
#endif
			break;
		}

		tag = tag->next;
	}

	// No page found. Make one.
	if (tag == NULL) {
		if ((tag = allocate_new_tag(size)) == NULL) {
			liballoc_unlock();
			return NULL;
		}
		/*if (isSyscall())
			return;*/
		index = getexp(tag->real_size - sizeof(struct boundary_tag));
	} else {
		remove_tag(tag);

		if ((tag->split_left == NULL) && (tag->split_right == NULL))
			l_completePages[index] -= 1;
	}

	// We have a free page.  Remove it from the free pages list.
	tag->size = size;

	// Removed... see if we can re-use the excess space.

#ifdef DEBUG
	printf("Found tag with %i bytes available (requested %i bytes, leaving %i), which has "
		   "exponent: %i (%i bytes)\n",
		   tag->real_size - sizeof(struct boundary_tag),
		   size,
		   tag->real_size - size - sizeof(struct boundary_tag),
		   index,
		   1 << index);
#endif
	unsigned int remainder =
		tag->real_size - size - sizeof(struct boundary_tag) * 2; // Support a new tag + remainder

	if (remainder > 0 /*&& ( (tag->real_size - remainder) >= (1<<MINEXP))*/) {
		int childIndex = getexp(remainder);

		if (childIndex >= 0) {
#ifdef DEBUG
			printf("Seems to be splittable: %i >= 2^%i .. %i\n",
				   remainder,
				   childIndex,
				   (1 << childIndex));
#endif
			struct boundary_tag* new_tag = split_tag(tag);

			new_tag = new_tag; // Get around the compiler warning about unused variables.
#ifdef DEBUG
			printf("Old tag has become %i bytes, new tag is now %i bytes (%i exp)\n",
				   tag->real_size,
				   new_tag->real_size,
				   new_tag->index);
#endif
		}
	}

	ptr = (void*)((size_t)tag + sizeof(struct boundary_tag));
#ifdef DEBUG
	l_inuse += size;
	printf("malloc: %x,  %i, %i\n", ptr, (int)l_inuse / 1024, (int)l_allocated / 1024);
	dump_array();
#endif
	dmb_sy();
	liballoc_unlock();
	return ptr;
}

void port_free(void* ptr) {
	int index;
	struct boundary_tag* tag;

	if (ptr == NULL)
		return;

	if (((size_t)ptr & 0xF) != 0) {
		//liballoc_unlock();
		UARTDebugOut("liballoc:free: unaligned pointer returning \n");
		return;
	}
	liballoc_lock();

	tag = (struct boundary_tag*)((size_t)ptr - sizeof(struct boundary_tag));

	if (((size_t)tag & 0xF) != 0) {
		liballoc_unlock();
		UARTDebugOut("liballoc:free: tag unaligned returning \n");
	}

	if (tag->magic != LIBALLOC_MAGIC) {
		liballoc_unlock(); // release the lock
		return;
	}

#ifdef DEBUG
	l_inuse -= tag->size;
	printf("free: %x, %i, %i\n", ptr, (int)l_inuse / 1024, (int)l_allocated / 1024);
#endif

	// MELT LEFT...
	while ((tag->split_left != NULL) && (tag->split_left->index >= 0)) {
#ifdef DEBUG
		printf("Melting tag left into available memory. Left was %i, becomes %i (%i)\n",
			   tag->split_left->real_size,
			   tag->split_left->real_size + tag->real_size,
			   tag->split_left->real_size);
#endif
		tag = melt_left(tag);
		remove_tag(tag);
	}

	// MELT RIGHT...
	while ((tag->split_right != NULL) && (tag->split_right->index >= 0)) {
#ifdef DEBUG
		printf("Melting tag right into available memory. This was was %i, becomes %i (%i)\n",
			   tag->real_size,
			   tag->split_right->real_size + tag->real_size,
			   tag->split_right->real_size);
#endif
		tag = absorb_right(tag);
	}

	// Where is it going back to?
	index = getexp(tag->real_size - sizeof(struct boundary_tag));
	//UARTDebugOut("liballoc free index : %d \n", index);
	if (index < MINEXP)
		index = MINEXP;

	// A whole, empty block?
	if ((tag->split_left == NULL) && (tag->split_right == NULL)) {
		if (l_completePages[index] == MAXCOMPLETE) {
			// Too many standing by to keep. Free this one.
			unsigned int pages = tag->real_size / l_pageSize;

			if ((tag->real_size % l_pageSize) != 0)
				pages += 1;
			//if (pages < l_pageCount) pages = l_pageCount;
			//UARTDebugOut("liballoc: Freeing page for address : %x \n", ptr);
			liballoc_free(tag, pages);

#ifdef DEBUG
			l_allocated -= pages * l_pageSize;
			printf("Resource freeing %x of %i pages\n", tag, pages);
			dump_array();
#endif
			liballoc_unlock();
			return;
		}

		l_completePages[index] += 1; // Increase the count of complete pages.
	}

	// ..........

	insert_tag(tag, index);

#ifdef DEBUG
	printf("Returning tag with %i bytes (requested %i bytes), which has exponent: %i\n",
		   tag->real_size,
		   tag->size,
		   index);
	dump_array();
#endif
	liballoc_unlock();
}

void* port_calloc(unsigned long long nobj, unsigned long long size) {
	int real_size;
	void* p;

	real_size = nobj * size;

	p = port_malloc(real_size);

	liballoc_memset(p, 0, real_size);

	return p;
}

void* port_realloc(void* p, unsigned int size) {
	void* ptr;
	struct boundary_tag* tag;
	int real_size;

	if (size == 0) {
		port_free(p);
		return NULL;
	}
	if (p == NULL)
		return port_malloc(size);

	if (liballoc_lock != NULL)
		liballoc_lock(); // lockit
	tag = (struct boundary_tag*)((size_t)p - sizeof(struct boundary_tag));
	real_size = tag->size;
	if (liballoc_unlock != NULL)
		liballoc_unlock();

	if (real_size > size)
		real_size = size;

	ptr = port_malloc(size);
	liballoc_memcpy(ptr, p, real_size);
	port_free(p);
	return ptr;
}

int liballoc_lock() {
	return 0;
}

int liballoc_unlock() {
	return 0;
}

void* liballoc_alloc(int pages) {
	size_t size = pages * 4096;
	char* page = (char*)AuGetFreePage(0, false);
	uint64_t page_ = (uint64_t)page;
	for (size_t i = 0; i < pages; i++) {
		void* p = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		AuMapPage((uint64_t)p, page_ + i * 4096, PTE_NORMAL_MEM);
	}
	memset(page, 0, pages * PAGE_SIZE);

	/*if (isSyscall()) {
		UARTDebugOut("LibAlloc : %x \n", page);
		return;
	}*/
	//UARTDebugOut("New page allocated %x %d\n", page, pages);
	if ((page_ % PAGE_SIZE) != 0) {
		//AuTextOut("Request page not aligned to page boundary \r\n");
		for (;;)
			;
	}
	isb_flush();
	return page;
}

int liballoc_free(void* ptr, int pages) {
	UARTDebugOut("Liballoc free: %d \n", pages);
	//AuFreePages((uint64_t)ptr, true, (pages*4096));
	return 0;
}
