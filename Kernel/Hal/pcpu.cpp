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

#include <Hal/pcpu.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/x86_64_lowlevel.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <Hal/serial.h>

CPUStruc* cpus[256];

/*
 * AuCreatePerCPU -- creates a per-cpu data structure
 * @param alloc -- allocation address only for AP's
 */
void AuCreatePerCPU(void* alloc) {
	CPUStruc *cpu;
	if (alloc != 0) 
		cpu = (CPUStruc*)alloc;
	else {
		cpu = (CPUStruc*)kmalloc(sizeof(CPUStruc));
		memset(cpu, 0, sizeof(CPUStruc));
	}

	cpus[cpu->cpu_id] = cpu;
	x64_write_msr(MSR_IA32_GS_BASE, (size_t)cpu);
}

/*
 * AuGetPerCPU -- returns a per cpu data
 * structure
 * @param id -- cpu id
 */
CPUStruc* AuGetPerCPU(uint8_t id) {
	return cpus[id];
}

/*
 * AuPerCPUSetCpuID -- sets cpu id for current processor
 * @param id -- cpu id
 */
void AuPerCPUSetCpuID(uint8_t id) {
	x64_gs_writeb(0, id);
}

/*
 * AuPerCPUGetCpuID -- returns the current cpu id
 * of current processor
 */
uint8_t AuPerCPUGetCpuID() {
	return x64_gs_readb(0);
}

/*
 * AuPerCPUSetCurrentThread -- set thread of current 
 * processor
 * @param thread -- pointer to thread
 */
void AuPerCPUSetCurrentThread(void* thread) {
	x64_gs_writeq(1, (size_t)thread);
}

/*
 * AuPerCPUGetCurrentThread -- get the current thread of
 * present processor
 */
AuThread* AuPerCPUGetCurrentThread() {
	uint64_t val = x64_gs_readq(1);
	return (AuThread*)x64_gs_readq(1);
}

/**
* AuPerCPUSetKernelTSS -- Sets Kernel TSS structure
* @param tss -- pointer to tss structure
*/
void AuPerCPUSetKernelTSS(TSS *tss){
	x64_gs_writeq(9, (uint64_t)tss);
}

/**
* AuPerCPUGetKernelTSS -- Gets the kernel TSS structure
*/
TSS* AuPerCPUGetKernelTSS() {
	uint64_t val = x64_gs_readq(9);
	return (TSS*)val;
}



