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

#ifndef __X86_64_IDT_H__
#define __X86_64_IDT_H__

#include <stdint.h>
#include <aurora.h>

//! Interrupt Descriptor Table
#pragma pack(push, 1)
typedef struct _idt {
	uint16_t offset_1;
	uint16_t selector;
	uint8_t  ist;
	uint8_t  type_attr;
	uint16_t offset_2;
	uint32_t offset_3;
	uint32_t zero;
}IDT;

typedef struct _idtr {
	uint16_t length;
	void*    idtaddr;
} IDTR;
#pragma pack(push,1)


/*
* x86_64_idt_init -- initialize the interrupt descriptor
* table
*/
extern void  x86_64_idt_init();

/*
 * setvect -- Registers an interrupt handler to its vector
 * @param vector -- vector number
 * @param function -- handler function pointer
 */
AU_EXTERN AU_EXPORT void setvect(size_t vector, void(*function)(size_t vector, void* param));

/*
* x86_64_idt_init_ap -- initialize the interrupt descriptor
* table of ap's
*/
extern void  x86_64_idt_init_ap();
#endif