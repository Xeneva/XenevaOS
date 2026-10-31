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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
	 * _getCurrentEL -- returns the current exception level
	 */
uint64_t _getCurrentEL();

/*
	 * read_ttbr0_el1 -- reads the current exception's 
	 * page mapping base address
	 */
uint64_t read_ttbr0_el1();

/*
	 * write_ttbr0_el1 -- writes current page mapping base
	 * address
	 */
void write_ttbr0_el1(uint64_t* base);

uint64_t read_ttbr1_el1();

void write_ttbr1_el1(uint64_t* base);

uint64_t read_ttbr1_el2();

uint64_t read_ttbr0_el2();

uint64_t read_tcr_el1();

uint64_t read_tcr_el2();

void write_tcr_el1(uint64_t tcr);

uint64_t read_mair_el1();

void write_mair_el1(uint64_t mair);

/*
	 * read_sctlr_el1 -- reads current system control
	 * register of el1
	 */
uint64_t read_sctlr_el1();

/*
	 * write_sctlr_el1 -- write current system control
	 * register of el1
	 * @param sctlr -- system control register value
	 */
void write_sctlr_el1(uint64_t sctlr);

uint64_t read_spsr_el2();

void write_spsr_el2(uint64_t spsr);

/*
	 * dsb_ish -- data syncronization barrier inner shareablity memory
	 * flush, which makes the previous memory access visible for
	 * multi core system
	 */
void dsb_ish();

/*
	 * isb_flush -- instruction synchronization flush, flush previous
	 * instruction set
	 */
void isb_flush();

void tlb_flush(uint64_t virtul_addr);

uint64_t read_esr_el1();

uint64_t read_far_el1();

uint64_t read_elr_el1();

void write_vbar_el1(uint64_t vecbase);

void dc_cvau(uint64_t a);

void ic_ivau(uint64_t a);

void callKernel(void* param, uint64_t stack, uint64_t stacksize, void* entry);

#ifdef __cplusplus
}
#endif

#endif
