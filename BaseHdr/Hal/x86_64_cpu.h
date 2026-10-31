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

#ifndef __X86_64_CPU_H__
#define __X86_64_CPU_H__

#include <stdint.h>
#include <process.h>
#include <time.h>

#define IA32_EFER 0xC0000080
#define IA32_STAR 0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_CSTAR 0xC0000083
#define IA32_SFMASK 0xC0000084

#define IA32_SYSENTER_CS 0x174
#define IA32_SYSENTER_ESP 0x175
#define IA32_SYSENTER_EIP 0x176

#define IA32_EFLAGS_INTR (1<<9)
#define IA32_EFLAGS_DIRF (1<<10)

#define MSR_IA32_MISC_ENABLE 0x1A0
#define MSR_IA32_ENERGY_PERF_BIAS 0x1B0
#define MSR_IA32_PERF_CTL 0x199
#define MSR_IA32_PERF_STATUS 0x198
#define MSR_IA32_PM_ENABLE 0x770
#define MSR_IA32_HWP_CAPABILITIES 0x771
#define MSR_IA32_HWP_REQUEST_PKG 0x772
#define MSR_IA32_HWP_INTERRUPT 0x773
#define MSR_IA32_HWP_REQUEST 0x774
#define MSR_IA32_HWP_PECI_REQUEST_INFO 0x775
#define MSR_IA32_HWP_STATUS 0x777
#define MSR_IA32_FS_BASE 0xC0000100
#define MSR_IA32_GS_BASE 0xC0000101
#define MSR_IA32_KERNELGS_BASE 0xC0000102
#define IA32_EFER  0xC0000080


#pragma pack(push,1)
typedef struct _stack_frame_ {
	_stack_frame_* baseptr;
	size_t  rip;
}stack_frame;
#pragma pack(pop)

/* Needed for Signal Handling */
#pragma pack(push,1)
typedef struct _regs_ctx_ {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rbp;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
}x86_64_cpu_regs_t;
#pragma pack(pop)

#pragma pack(push,1)
struct interrupt_stack_frame {
	stack_frame* baseptr;
	size_t  error;
	size_t  rip;
	size_t  cs;
	size_t  rflags;
	size_t  rsp;
	size_t  ss;
};
#pragma pack (pop)


#pragma pack(push,1)
typedef struct _tss {
	uint32_t reserved;
	size_t   rsp[3];
	size_t reserved2;
	size_t   IST[7];
	size_t   resv3;
	uint16_t  resv4;
	uint16_t  iomapbase;
}TSS;
#pragma pack(pop)

#pragma pack(push,1)
/* cpu structure */
typedef struct _cpu_ {
	uint8_t cpu_id;     // 0
	uint64_t*   au_current_thread; // offset -> 1
	TSS*    kernel_tss; //offset -> 9
}CPUStruc;
#pragma pack(pop)

/*
* x86_64_enable_syscall_ext -- enablse syscall
* extension
*/
extern void x86_64_enable_syscall_ext();

/*
* x86_64_init_user -- initialise the user land
* @param bit -- either 32 or 64 bit
*/
extern void x86_64_init_user(size_t bit);

/*
* x86_64_init_user_ap -- initialise user land for application
* processor's
* @param bit -- cpu bit
*/
extern void x86_64_init_user_ap(size_t bit);

/*
* x86_64_get_tss -- returns the tss
*/
extern TSS* x86_64_get_tss();

/*
* x86_64_hal_cpu_feature_enable -- enable cpu features
*/
extern void x86_64_hal_cpu_feature_enable();

extern bool x86_64_is_cpu_fxsave_supported();

/*
* x86_64_cpu_msi_address -- calculates the cpu msi address
* @param data -- msi data to return
* @param vector -- interrupt vector number
* @param processor -- processor number
* @param edge -- edge triggered or level triggered
* @param deassert -- deassert bit
*/
extern uint64_t x86_64_cpu_msi_address(uint64_t* data, size_t vector, uint32_t processor, uint8_t edge, uint8_t deassert);

/*
* x86_64_cpu_initialize -- initialise all Application processor
* except the BSP processor
* @param num_cpu -- cpu number to initialise except 0
*/
extern void x86_64_cpu_initialize(uint8_t num_cpu);

/*
* x86_64_set_ap_start_bit -- set a bit in __ApStarted
* @param value -- bit to set
*/
extern void x86_64_set_ap_start_bit(bool value);

/*
* x86_64_initialise_syscall -- initialise the syscall service
*/
extern void x86_64_initialise_syscall();

/*
* cpu_read_tsc -- read the tsc count
*/
extern uint64_t cpu_read_tsc();

extern void x86_64_measure_cpu_speed();

extern uint64_t x86_64_cpu_get_mhz();

extern int x86_64_gettimeofday(timeval *t);

extern void updateTicks(uint64_t ticks, uint64_t* timerTick, uint64_t* timerSubticks);

/*
 *  x86_64_calculate_ticks -- calculate the number of ticks from given milliseconds
 * @param milliseconds -- amount of milliseconds
 * @param out_milliseconds -- where to store the number of ticks
 */
void x86_64_calculate_ticks(uint64_t milliseconds, uint64_t subsec, uint64_t* out_milliseconds, uint64_t* out_subsec);

/*
 * x86_64_udelay -- udelay
 */
AU_EXTERN AU_EXPORT void x86_64_udelay(uint64_t usec);

#endif