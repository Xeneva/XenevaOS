/**
* @file aa64percpu.c
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

#include <Hal/AA64/aa64percpu.h>
#include <string.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <aucon.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

AA64PerCPU _aa64_percpu[AA64_SMP_MAX_CPUS];

_Static_assert(offsetof(AA64PerCPU, self) == 0, "percpu layout");
_Static_assert(offsetof(AA64PerCPU, cpu_num) == 8, "percpu layout");
_Static_assert(offsetof(AA64PerCPU, currentThread) == 16, "percpu layout");
_Static_assert(offsetof(AA64PerCPU, exceptionStackTop) == 24, "percpu layout");
/* sched_finish_switch in Hal/aa64switch.s */
_Static_assert(offsetof(AA64PerCPU, switchRelease) == 32, "percpu layout");

/**
 * @brief AA64PerCPUInitialize --initialize the per cpu data structure for specified
 * cpu
 * @param cpu_num -- current cpu number
 * @param mpidr -- MPIDR reg
 * @param exceptionStack -- kernel mode stack to use when getting system exception 
 * transition
 */
void AA64PerCPUInitialize(uint32_t cpu_num, uint64_t mpidr, uint64_t exception_stack) {
	AA64PerCPU* p = &_aa64_percpu[cpu_num];
	memset(p, 0, sizeof(AA64PerCPU));
	p->self = p;
	p->cpu_num = cpu_num;
	p->mpidr = mpidr;
	p->exceptionStackTop = exception_stack;
	p->is_boot = (cpu_num == 0);
	p->timer_freq = get_cntfrq_el0();
	p->state = AA64_CPU_STATE_BOOTING;
	p->magic = AA64_PERCPU_MAGIC;

	__asm__ volatile("msr tpidr_el1,%0" ::"r"(p));
	__asm__ volatile("isb");
	AuTextOut("[aurora]: per cpu data structure for cpu : %d initialized \r\n", cpu_num);
}

/**
 * @brief AA64PerCPUBootInitialize -- set up logical cpu 0 (the boot core).
 * Needs the physical memory manager and the higher half map, so call it
 * right after AuVmmngrInitialize. Before this AA64GetPerCPU() returns NULL.
 */
void AA64PerCPUBootInitialize() {
	uint64_t exc_stack = AuPmmngrAllocPages(AA64_SMP_EXC_STACK_PAGES, 1, 0, AURORA_PAGE_KERNEL);
	if (exc_stack == PMM_INVALID_PHYS) {
		/* no fallback, nothing past this point may assume per cpu data is missing */
		AuTextOut("[aurora]: percpu: no exception stack for boot cpu, halting \r\n");
		for (;;)
			__asm__ volatile("wfi");
	}
	AA64PerCPUInitialize(0,
						 read_mpidr_el1() & AA64_MPIDR_AFF_MASK,
						 P2V(exc_stack) + AA64_SMP_EXC_STACK_PAGES * PAGE_SIZE);
	AA64GetPerCPU()->state = AA64_CPU_STATE_ONLINE;
}
