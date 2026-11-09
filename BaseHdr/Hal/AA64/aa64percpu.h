/**
* @file aa64percpu.h
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

#ifndef __AA64_PER_CPU_H__
#define __AA64_PER_CPU_H__

#include "aa64smp.h"
#include <stdint.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64cpu.h>

#define AA64_PERCPU_MAGIC 0x50455243505531ULL

#define AA64_CPU_STATE_OFFLINE 0
#define AA64_CPU_STATE_BOOTING 1
#define AA64_CPU_STATE_ONLINE  2

typedef struct __aa64_per_cpu__ {
	struct __aa64_per_cpu__* self;
	uint64_t cpu_num;
	AA64Thread* currentThread;
	uint64_t exceptionStackTop;
	/* &on_cpu of the thread this core is switching away from, or NULL.
	 * The switch path in aa64switch.s clears that claim once the core is
	 * off the old stack. Offset 32 is hard coded there */
	volatile uint32_t* switchRelease;

	uint64_t mpidr;
	uint64_t magic;
	uint32_t state;
	uint32_t is_boot;

	uint64_t timer_freq;
	uint64_t tick_count;
	uint64_t next_deadline;

	uint64_t gic_rdist_base;
	uint64_t ipi_pending;

	uint64_t tlb_flush_va;
	uint64_t context_switches;
	uint64_t idle_tick;

	/* this core's own idle thread, never on the shared ready list */
	AA64Thread* idleThread;
	/* set by the boot core (AA64SMPStartSchedulers) once this core may
	 * leave its wait loop and enter its idle thread */
	volatile uint32_t sched_release;
	/* GICv2 CPU interface bit of this core, target of SGIs */
	uint32_t gic_target_mask;
	/* set by the timer interrupt right before it calls the scheduler, so
	 * system tick bookkeeping runs once per tick and not on every IPI or
	 * syscall driven reschedule */
	volatile uint32_t sched_tick;
	uint32_t _pad0;

	/* CPU time accounting, in generic timer counter ticks, written only
	 * by the owning core at every switch (sched.c schedAccount) */
	uint64_t acct_last;
	volatile uint64_t busy_cnt; /* running anything but its idle thread */
	volatile uint64_t idle_cnt; /* running its idle thread */
	/* name of the thread the core switched to last, for monitors */
	char cur_name[8];

	/* core reservation (sched.c): isolated cores run bound threads only */
	volatile uint32_t isolated;
	/* the core's scheduler timer is off (isolated core, one bound thread) */
	volatile uint32_t tick_stopped;
	/* times an isolated core switched to a thread not bound to it: must
	 * stay 0, checked by the isolation test */
	volatile uint64_t foreign_runs;
} __attribute__((aligned(64))) AA64PerCPU;

extern AA64PerCPU _aa64_percpu[AA64_SMP_MAX_CPUS];

static inline AA64PerCPU* AA64GetPerCPU() {
	AA64PerCPU* p;
	__asm__ volatile("mrs %0, tpidr_el1" : "=r"(p));
	return p;
}

/**
 * @brief AA64GetCPUNum -- logical number of the calling core, 0 while
 * per cpu data is not set up yet (only the boot core runs then)
 */
static inline uint32_t AA64GetCPUNum() {
	AA64PerCPU* p = AA64GetPerCPU();
	return p ? (uint32_t)p->cpu_num : 0;
}

/**
 * @brief AA64PerCPUInitialize --initialize the per cpu data structure for specified
 * cpu
 * @param cpu_num -- current cpu number
 * @param mpidr -- MPIDR reg
 * @param exceptionStack -- kernel mode stack to use when getting system exception 
 * transition
 */
extern void AA64PerCPUInitialize(uint32_t cpu_num, uint64_t mpidr, uint64_t exception_stack);

/**
 * @brief AA64PerCPUBootInitialize -- set up logical cpu 0 (the boot core).
 * Needs the physical memory manager and the higher half map, so call it
 * right after AuVmmngrInitialize. Before this AA64GetPerCPU() returns NULL.
 */
extern void AA64PerCPUBootInitialize();

#endif