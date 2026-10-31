/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#include <Hal/x86_64_hal.h>
#include <Hal/x86_64_exception.h>
#include <Hal/apic.h>
#include <Mm/pmmngr.h>
#include <Hal/pcpu.h>
#include <_null.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <Hal/basicacpi.h>
#include <Hal/x86_64_pic.h>

/*
 * x86_64_hal_initialise -- initialise the x86_64 hardware
 * abstraction layer
 */
void x86_64_hal_initialise(KERNEL_BOOT_INFO *info) {
	x64_cli();

	x86_64_hal_init_gdt();
	x86_64_idt_init();
	x86_64_exception_init();
	x86_64_measure_cpu_speed();
	
	AuAPICInitialise(true);

	x86_64_enable_syscall_ext();
	x86_64_init_user(64);
	x86_64_hal_cpu_feature_enable();
	
   
	AuACPIInitialise(info->acpi_table_pointer);

	x86_64_initialise_syscall();

	CPUStruc *cpu = (CPUStruc*)kmalloc(sizeof(CPUStruc));
	cpu->cpu_id = 0;
	cpu->au_current_thread = 0;
	cpu->kernel_tss = NULL;
	AuCreatePerCPU(cpu);
	AuPerCPUSetCpuID(0);
	AuPerCPUSetKernelTSS(x86_64_get_tss());
	/* acpica needs problem fixing */
	//AuInitialiseACPISubsys(info);

	x64_sti();
}