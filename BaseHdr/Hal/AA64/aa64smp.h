/**
* @file aa64smp.h
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

#ifndef __AA64_SMP_H__
#define __AA64_SMP_H__

#include <stdint.h>
#include <aurora.h>

#define AA64_SMP_MAX_CPUS 8

/* number of 4K pages given to each secondary core as its boot stack */
#define AA64_SMP_AP_STACK_PAGES 4

/* PSCI 0.2+ function ids (SMC64 calling convention where needed) */
#define PSCI_FN_VERSION	 0x84000000
#define PSCI_FN64_CPU_ON 0xC4000003

#define PSCI_RET_SUCCESS		  0
#define PSCI_RET_NOT_SUPPORTED	  -1
#define PSCI_RET_INVALID_PARAMS	  -2
#define PSCI_RET_DENIED			  -3
#define PSCI_RET_ALREADY_ON		  -4
#define PSCI_RET_ON_PENDING		  -5
#define PSCI_RET_INTERNAL_FAILURE -6
#define PSCI_RET_NOT_PRESENT	  -7
#define PSCI_RET_DISABLED		  -8
#define PSCI_RET_INVALID_ADDRESS  -9

/* MPIDR_EL1 affinity fields Aff3..Aff0 */
#define AA64_MPIDR_AFF_MASK 0xFF00FFFFFFULL

/*
 * AA64SMPBootParam -- handed to a secondary core through
 * PSCI CPU_ON context_id (x0). The trampoline reads it with
 * the MMU off, so the BSP cleans it to PoC before CPU_ON.
 *
 * idmap_ttbr0/idmap_tcr are only live inside the trampoline,
 * AA64SMPAPEntry swaps them for ttbr0/tcr (the boot core's)
 *
 * NOTE: field offsets are hard coded in Hal/aa64smpboot.s,
 * keep both in sync
 */
typedef struct _aa64_smp_boot_param_ {
	uint64_t entry;		  //0   -- higher half C entry
	uint64_t stack_top;	  //8   -- higher half stack top
	uint64_t param_virt;  //16  -- higher half address of this block
	uint64_t idmap_ttbr0; //24  -- private identity map of the boot page
	uint64_t ttbr1;		  //32
	uint64_t idmap_tcr;	  //40  -- boot core TCR with TTBR0 fields forced
	uint64_t mair;		  //48
	uint64_t sctlr;		  //56
	uint64_t vbar;		  //64
	uint64_t cpu_num;	  //72  -- logical cpu number
	uint64_t mpidr;		  //80
	uint64_t online;	  //88  -- written by the AP once it reaches C
	uint64_t ttbr0;		  //96  -- boot core's TTBR0, restored in C
	uint64_t tcr;		  //104 -- boot core's TCR, restored in C
} AA64SMPBootParam;

/**
 * @brief AA64SMPInitialize -- start every secondary core described
 * in the device tree through PSCI CPU_ON
 */
extern void AA64SMPInitialize();

/**
 * @brief AA64SMPGetCPUCount -- returns the number of online cores,
 * boot core included
 */
AU_EXTERN AU_EXPORT uint32_t AA64SMPGetCPUCount();

#endif
