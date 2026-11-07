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

#ifndef __X86_64_GDT_H__
#define __X86_64_GDT_H__

#include <stdint.h>
#include <stddef.h>

#define GDT_ENTRY_NULL 0
#define GDT_ENTRY_KERNEL_CODE 1
#define GDT_ENTRY_KERNEL_DATA 2
#define GDT_ENTRY_USER_CODE32 3
#define GDT_ENTRY_USER_DATA 4
#define GDT_ENTRY_USER_CODE 5
#define GDT_ENTRY_KERNEL_CODE32 6
#define GDT_ENTRY_TSS 7
#define GDT_ENTRY_MAX 9		//TSS takes two entries



#define SEGVAL(gdtent, rpl) \
	((gdtent << 3) | rpl)

#define SEGVAL_LDT(ldtent, rpl) \
	((ldtent << 3) | 0x4 | rpl)

enum sregs {
	SREG_CS,
	SREG_DS,
	SREG_ES,
	SREG_FS,
	SREG_GS,
	SREG_SS
};

#define GDT_FLAG_GRAN 0x8
#define GDT_FLAG_32BT 0x4
#define GDT_FLAG_64BT 0x2

#define GDT_ACCESS_PRESENT 0x80
#define GDT_ACCESS_PRIVL(x) (x << 5)
#define GDT_ACCESS_TYPE 0x10
#define GDT_ACCESS_EX 0x8
#define GDT_ACCESS_DC 0x4
#define GDT_ACCESS_RW 0x2
#define GDT_ACCESS_AC 0x1

#pragma pack(push, 1)

typedef struct _gdt {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t  base_mid;
	uint8_t  access;
	uint8_t  flags_limit;
	uint8_t  base_high;
}gdt_entry, *pgdt_entry;

typedef struct _gdtr{
	uint16_t size;
	pgdt_entry gdtaddr;
}gdtr, *pgdtr;

#pragma pack(pop)

/*
 * x86_64_hal_init_gdt -- initialize the global descriptor
 * table 
 */
extern "C" void x86_64_hal_init_gdt();

/*
* x86_64_hal_init_gdt_ap -- initialize gdt for Application
* processors
*/
extern void x86_64_hal_init_gdt_ap();

extern void set_gdt_entry(gdt_entry& entry, size_t base, size_t limit, uint8_t access, uint8_t flags);

#endif