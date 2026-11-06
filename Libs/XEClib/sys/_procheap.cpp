/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2024, Manas Kamal Choudhury
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

#include <stdlib.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_heap.h>
#include <sys/mman.h>

#define PROC_HEAP_AREA_DEFAULT_SZ (16 * 4096)

static uint64_t process_heap_end = 0;
static uint64_t process_heap_start = 0;
static size_t proc_heap_max = 0;
static size_t proc_heap_len = 0;
bool _initialized = 0;

#define PAGE_ALIGN(value)                                                                          \
	(((PAGE_SIZE - 1) & value) ? ((value + PAGE_SIZE) & ~(PAGE_SIZE - 1)) : value)

/*
 * _ProcCreateHeap -- create a new process heap area 
 *  @param sz -- size to create by default
 */
void ProcCreateHeapArea(size_t sz) {
	if (_initialized)
		return;
	_KePrint("Creating heap mem \r\n");
	process_heap_end = (uint64_t)_KeGetProcessHeapMem(sz);
	_KePrint("Process Heap segment -> %x \r\n", process_heap_end);
	process_heap_start = process_heap_end;
	_initialized = true;
	proc_heap_max = sz;
	proc_heap_len = sz;
}

/*
 * _ProcHeapAreaDestroy -- destroy entire heap area
 */
void _ProcHeapAreaDestroy() {
	if (!_initialized)
		return;
	if (!process_heap_start)
		return;
	_KeProcessHeapUnmap((void*)process_heap_start, proc_heap_max);
	_initialized = false;
	_KePrint("Process Heap Area destroyed \r\n");
}

void _ProcHeapAreaExpand(int sz) {
	uint64_t addr = _KeGetProcessHeapMem(sz);
	_KePrint("Process Heap Segment %x\r\n", addr);
	proc_heap_max += sz;
}

/*
 * _ProcGetHeapMem -- returns a heap memory from process
 * heap area
 * @param sz -- size in bytes
 */
uint64_t _ProcGetHeapMem(size_t sz) {
	if (!_initialized) {
		ProcCreateHeapArea(PROC_HEAP_AREA_DEFAULT_SZ);
	}
	/* check if size is page aligned */
	if ((sz % PAGE_SIZE) != 0) {
		_KePrint("Returning error heap mem -> %d \r\n", sz);
		sz = PAGE_ALIGN(sz);
	}

	uint64_t start_addr = process_heap_end;
	if ((process_heap_end + sz) > (process_heap_start + proc_heap_max)) {
		_KePrint("Out of Process Heap Memory \r\n");
		_ProcHeapAreaExpand(PROC_HEAP_AREA_DEFAULT_SZ);
		return _ProcGetHeapMem(sz);
	}
	if (start_addr == 0) {
		_KePrint("ProcHeap end -> %d \r\n", process_heap_end);
		_KePrint("Proc Heap len -> %d \r\n", proc_heap_len);
		for (;;)
			;
	}
	process_heap_end += sz;
	proc_heap_len += sz;
	return start_addr;
}

void _ProcHeapMemUnmap(void* ptr, size_t sz) {
	proc_heap_len -= sz;
	process_heap_end = (uint64_t)ptr;
	if (process_heap_end == 0) {
		_KePrint("_ProcHeapMem is 0 -> %d %d\r\n", proc_heap_len, sz);
		_KePrint("ptr -> %x  %x\r\n", ptr, process_heap_end);
		for (;;)
			;
	}
}
