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

#include <Hal/x86_64_ap_init.h>
#include <Hal/x86_64_gdt.h>
#include <Hal/apic.h>
#include <Hal/x86_64_idt.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/pcpu.h>
#include <Mm/kmalloc.h>
#include <Hal/serial.h>
#include <aucon.h>

void SMPISR(size_t v, void* p) {
	SeTextOut("SMP %d \r\n", AuPerCPUGetCpuID());
}
void SMPStart() {
	SeTextOut("SMP %d \r\n", AuPerCPUGetCpuID());
	setvect(0x40, SMPISR);  //0x40
	//AuThread* current_thread = AuPerCPUGetCurrentThread();
	//TSS* _ks = AuPerCPUGetKernelTSS(); //x86_64_get_tss();
	//SeTextOut("CurrentThread ->%x %x \r\n", current_thread, _ks);
	//execute_idle(current_thread, x86_64_get_tss());
	x64_sti();
}
/*
 * x86_64_ap_init -- application processor initialisation
 * sequence
 * @param cpu -- pointer to cpu data structure
 */
void x86_64_ap_init(void* cpu) {
	x64_cli();
	SeTextOut("ap init \r\n");

	/* tell the bsp that initialization
	 * process is finished */
	AuCreatePerCPU(cpu);
	x86_64_hal_init_gdt_ap();
	x86_64_idt_init_ap();
	x86_64_enable_syscall_ext();

	AuAPICInitialise(false);


	x86_64_init_user_ap(64);
	x86_64_initialise_syscall();
	x86_64_hal_cpu_feature_enable();

	/* till here, almost cpu initialisation done,
	 * now just start the scheduler
	 */
	//AuSchedulerInitAp();
	AuTextOut("CPU ID -> %d, TSS -> %x \r\n", AuPerCPUGetCpuID(), AuPerCPUGetKernelTSS());
	x86_64_set_ap_start_bit(true);
	//SMPStart();
	for (;;){
		x64_pause();
	}
}