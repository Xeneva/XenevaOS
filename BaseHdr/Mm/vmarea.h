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

#ifndef __VM_AREA_H__
#define __VM_AREA_H__

#include <stdint.h>
#include <stdio.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Fs/vfs.h>
#include <process.h>

/* protection flags */
#define VM_PRESENT  (1<<0)
#define VM_WRITE    (1<<1)
#define VM_EXEC     (1<<2)
#define VM_SHARED   (1<<3)

/* types of the area */
#define VM_TYPE_TEXT     1
#define VM_TYPE_DATA     2
#define VM_TYPE_STACK    3
#define VM_TYPE_HEAP     4
#define VM_TYPE_RESOURCE 5


/* AuVMArea -- virtual memory mapping area */
typedef struct _vm_area_ {
	size_t start;
	size_t end;
	size_t len;
	uint8_t prot_flags;
	uint8_t type;
	AuVFSNode* file;
}AuVMArea;

/*
* AuInsertVMArea -- insert a memory segment to the given process
* @param proc -- pointer to the process
* @param area -- pointer to the vm area
*/
extern void AuInsertVMArea(AuProcess* proc, AuVMArea* area);

/*
* AuRemoveVMArea -- removes a memory segment from the given
* process
* @param proc -- pointer to the process
* @param area -- pointer to the vm area
*/
extern void AuRemoveVMArea(AuProcess* proc, AuVMArea* area);

/*
* AuVMAreaCreate -- create virtual memory segment for
* current process
* @param start -- starting address
* @param end -- ending address
* @param prot -- protection flags
* @param len -- length of the area
* @param type -- type of the area
*/
extern AuVMArea* AuVMAreaCreate(size_t start, size_t end, uint8_t prot, size_t len, uint8_t type);

/*
* AuVMAreaGet -- finds a virtual memory area from the
* given address
* @param proc -- pointer to the process
* @param address -- address to search
*/
extern AuVMArea* AuVMAreaGet(AuProcess* proc, size_t address);

#endif