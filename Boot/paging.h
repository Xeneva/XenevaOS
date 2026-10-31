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

#ifndef __PAGING_H__
#define __PAGING_H__

#include "physm.h"

#define PAGE_ATTRIBUTE_WRITABLE 0x2
#define PAGE_ATTRIBUTE_NO_EXECUTE 0x80000
#define PAGE_ATTRIBUTE_NO_PAGING  0x100000
#define PAGE_ATTRIBUTE_NO_CACHING 0x200000

/*
 * XEInitialisePaging -- initialise paging
 */
extern void XEInitialisePaging();


/*
 * check_free -- check free memory area
 */
extern bool check_free(void* vaddr, size_t length);
/*
 * XEPagingMap -- maps range of physical memory to virtual memory
 * @param vaddr -- virtual address start
 * @param paddr -- Physical address start
 * @param length -- length of the area
 * @param attributes -- paging attributes to set
 */
extern bool XEPagingMap(void* vaddr, paddr_t paddr, size_t length, size_t attributes);

extern void set_paging_attributes(void* vaddr, size_t length, size_t attrset, size_t attrclear);

#endif

