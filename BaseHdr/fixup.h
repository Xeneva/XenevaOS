/**
* @file fixup.h
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#ifndef __FIXUP_H__
#define __FIXUP_H__

#include <stdint.h>
#include <stddef.h>

struct _fixup_entry {
	uint64_t instruction;
	uint64_t fixup_address;
};

/**
 * @brief __search_fixup_table -- searches alternative address to jump
 * when kernel hits data abort exception
 * @param fault_pc -- faulting instruction pointer
 */
extern uint64_t __search_fixup_table(uint64_t fault_pc);

/**
 * @brief __bordoisila_copy_from_user -- copy data from user to kernel 
 * memory
 * @param dst -- destination kernel memory
 * @param usrc -- user source memory
 * @param n -- number of bytes to copy
 */
extern long __bordoisila_copy_from_user(void* dst, const void* usrc, size_t n);

/**
 * @brief __bordoisila_copy_to_user -- copy to user memory from kernel 
 * memory
 * @param udst -- user space destination memory
 * @param src -- kernel source memory
 * @param n -- number of bytes to copy
 */
extern long __bordoisila_copy_to_user(void* udst, const void* src, size_t n);

/**
 * @brief __bordoisila_fixup_table_check -- could be called
 * during kernel initialization to check fixup addresses
 */
extern void __bordoisila_fixup_table_check();

extern bool __bordoisila_mm_user_range_mapped(uint64_t addr, size_t len, bool write);

extern long _bordoisila_strncpy_to_user(char* udst, const char* ksrc, size_t max);

extern long _bordoisila_strncpy_from_user(char* dst, const char* usrc, size_t max);

#endif