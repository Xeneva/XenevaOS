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

#ifndef __LOW_LEVEL_H__
#define __LOW_LEVEL_H__

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

	uint8_t inportb(uint16_t port);
	uint16_t inportw(uint16_t port);
	uint16_t inportd(uint16_t port);
	void outportb(uint16_t port, uint8_t val);
	void outportw(uint16_t port, uint16_t val);
	void outportd(uint16_t port, uint32_t val);

	void cpuid(size_t page, size_t* a, size_t* b, size_t* c, size_t* d, size_t subpage = 0);
	void cacheflush();
	void tlbflush(void* addr);
	void memory_barrier();
	void* get_paging_root();
	void set_paging_root(uint64_t);
	void cpu_startup();
	uint64_t rdmsr(size_t reg);
	void wrmsr(size_t reg, uint64_t val);
	size_t read_cr0();
	void write_cr0(size_t);
	void call_kernel(void* param, void* entry, void* stack, size_t stacksz);

#ifdef __cplusplus
}
#endif

#endif