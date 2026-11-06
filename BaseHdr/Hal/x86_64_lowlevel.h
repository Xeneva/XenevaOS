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

#ifndef __X86_64_LOW_LEVEL_H__
#define __X86_64_LOW_LEVEL_H__

#include <stdint.h>
#include <aurora.h>

//!=======================================================
//! G L O B A L     F U N C T I O N S
//!=======================================================

AU_EXTERN AU_EXPORT void x64_cli();
AU_EXTERN AU_EXPORT void x64_sti();
extern "C" void x64_hlt();

//! in & out port functions
extern "C" uint8_t x64_inportb(uint16_t port);
extern "C" uint16_t x64_inportw(uint16_t port);
extern "C" uint32_t x64_inportd(uint16_t port);

extern "C" void x64_outportb(uint16_t port, uint8_t data);
extern "C" void x64_outportw(uint16_t port, uint16_t data);
extern "C" void x64_outportd(uint16_t port, uint32_t data);

//! MSR functions
extern "C" uint64_t x64_read_msr(size_t msr);
extern "C" void x64_write_msr(size_t msr, uint64_t data);

//!Mfench
extern "C" void x64_mfence();

//!Pause
extern "C" void x64_pause();

//!x64_cpuid
extern "C" void x64_cpuid(size_t page, size_t* a, size_t* b, size_t* c, size_t* d, size_t subpage = 0);

//!Control Register
extern "C" size_t x64_read_cr0();
extern "C" size_t x64_read_cr2();
extern "C" size_t x64_read_cr3();
extern "C" size_t x64_read_cr4();
extern "C" void x64_write_cr0(size_t);
extern "C" void x64_write_cr3(size_t);
extern "C" void x64_write_cr4(size_t);

//!GDT &  IDT Function
extern "C" void x64_lgdt(void* location);
extern "C" void x64_sgdt(void* location);
extern "C" void x86_64_idt_test();

//! TLB Flush
extern "C" void flush_tlb(void* addr);
extern "C" void cache_flush();

extern "C" void x64_atom_exchange(size_t r1, size_t r2);

//!------------------------------------
//!  FXSAVE/FXRSTOR
//!------------------------------------
extern "C" void x64_fxsave(uint8_t* location);
extern "C" void x64_fxrstor(uint8_t* location);

/*
*  FS & GS Base
*/
extern "C" uint8_t x64_fs_readb(size_t offset);
extern "C" uint16_t x64_fs_readw(size_t offset);
extern "C" uint32_t x64_fs_readd(size_t offset);
extern "C" uint64_t x64_fs_readq(size_t offset);
extern "C" void x64_fs_writeb(size_t offset, uint8_t val);
extern "C" void x64_fs_writew(size_t offset, uint16_t val);
extern "C" void x64_fs_writed(size_t offset, uint32_t val);
extern "C" void x64_fs_writeq(size_t offset, uint64_t val);
extern "C" uint8_t x64_gs_readb(size_t offset);
extern "C" uint16_t x64_gs_readw(size_t offset);
extern "C" uint32_t x64_gs_readd(size_t offset);
extern "C" uint64_t x64_gs_readq(size_t offset);
extern "C" void x64_gs_writeb(size_t offset, uint8_t val);
extern "C" void x64_gs_writew(size_t offset, uint16_t val);
extern "C" void x64_gs_writed(size_t offset, uint32_t val);
extern "C" void x64_gs_writeq(size_t offset, uint64_t val);
extern "C" void x64_ldmxcsr(uint32_t *location);
extern "C" void x64_stmxcsr(uint32_t *location);

extern "C" void x64_set_kstack(void* ktss, size_t stack);
extern "C" size_t x64_get_kstack(void* ktss);

extern "C" uint64_t x64_kesp_get_top();

extern "C" void x64_rdtsc(uint32_t *hi, uint32_t *lo);

//! Interrupt Descriptor Table functions
extern "C" void x64_lidt(void* location);
extern "C" void x64_sidt(void* location);
extern "C" void x64_lgdt(void* location);
extern "C" void x64_sgdt(void* location);
extern "C" void x64_ltr(uint16_t seg);
extern "C" void x64_enter_user(uint64_t stack, uint64_t entry_addr, uint64_t cs, uint64_t ss);
extern "C" void x64_force_sched();
extern "C" bool x64_lock_test(volatile size_t *lock, size_t old_value, size_t new_value);
#endif