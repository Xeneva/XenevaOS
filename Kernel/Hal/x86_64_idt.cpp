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

#include <Hal/x86_64_idt.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/x86_64_gdt.h>
#include <Hal/pcpu.h>
#include <Hal/x86_64_lowlevel.h>
#include <Mm/kmalloc.h>
#include <aucon.h>

//=====================================================================
// I N T E R R U P T   D E S C R I P T O R   T A B L E
//=====================================================================
static void(*interrupts_handlers[256])(size_t, void*);

extern "C" extern void* default_irq_handlers[];

static void register_irq(IDT* entry, void* function)
{
	size_t faddr = (size_t)function;
	entry->offset_1 = faddr & UINT16_MAX;
	entry->offset_2 = (faddr >> 16) & UINT16_MAX;
	entry->offset_3 = (faddr >> 32) & UINT32_MAX;
}

void setvect(size_t vector, void(*function)(size_t vector, void* param))
{
	interrupts_handlers[vector] = function;
};

extern "C" void interrupt_dispatcher(uint64_t num, interrupt_stack_frame *frame)
{
	interrupts_handlers[num](num, frame);
	return;
}

static IDT the_idt[256];

void default_irq(size_t vect, void* param){
	x64_cli();
	AuTextOut("*** [x64_idt] x64_default_handler: Unhandled Exception *** \n");
	for (;;);
}


/*
 * x86_64_idt_reg_default_handlers -- register the idt
 * with all default aurora's default handlers
 */
void x86_64_idt_reg_default_handlers() {
	for (int i = 0; i < 256; i++)
		setvect(i, default_irq);
}

/*
 * x86_64_idt_init -- initialize the interrupt descriptor
 * table
 */
void  x86_64_idt_init() {
	TSS *tss = (TSS*)kmalloc(sizeof(TSS));
	tss->iomapbase = sizeof(TSS);
	size_t tss_addr = (size_t)tss;
	gdtr curr_gdt;
	x64_sgdt(&curr_gdt);
	gdt_entry* thegdt = curr_gdt.gdtaddr;
	set_gdt_entry(thegdt[GDT_ENTRY_TSS], (tss_addr & UINT32_MAX), sizeof(TSS), GDT_ACCESS_PRESENT | 0x9, 0);
	*(uint64_t*)&thegdt[GDT_ENTRY_TSS + 1] = (tss_addr >> 32);
	x64_ltr(SEGVAL(GDT_ENTRY_TSS, 3));

	//AuPCPUSetKernelTSS(tss);
	IDTR *idtr = (IDTR*)kmalloc(sizeof(IDTR));
	idtr->idtaddr = the_idt;
	idtr->length = 256 * sizeof(IDT)-1;
	x64_lidt(idtr);

	for (int n = 0; n < 256; n++)
	{
		the_idt[n].ist = 0;
		the_idt[n].selector = SEGVAL(GDT_ENTRY_KERNEL_CODE, 0);
		the_idt[n].zero = 0;
		the_idt[n].type_attr = GDT_ACCESS_PRESENT | 0xE;
		register_irq(&the_idt[n], default_irq_handlers[n]);
	}

	x86_64_idt_reg_default_handlers();
}

/*
* x86_64_idt_init_ap -- initialize the interrupt descriptor
* table of ap's
*/
void  x86_64_idt_init_ap() {
	TSS* tss = (TSS*)kmalloc(sizeof(TSS));
	tss->iomapbase = sizeof(TSS);
	size_t tss_addr = (size_t)tss;

	gdtr curr_gdt;
	x64_sgdt(&curr_gdt);
	gdt_entry* thegdt = curr_gdt.gdtaddr; //curr_gdt.gdtaddr;
	set_gdt_entry(thegdt[GDT_ENTRY_TSS], (tss_addr & UINT32_MAX), sizeof(TSS), GDT_ACCESS_PRESENT | 0x9, 0);
	*(uint64_t*)&thegdt[GDT_ENTRY_TSS + 1] = (tss_addr >> 32);
	x64_ltr(SEGVAL(GDT_ENTRY_TSS, 3));

	AuPerCPUSetKernelTSS(tss);

	IDTR *idtr = (IDTR*)kmalloc(sizeof(IDTR));  //0xFFFFD80000000000;
	idtr->idtaddr = the_idt;
	idtr->length = 256 * sizeof(IDT)-1;
	x64_lidt(idtr);
	for (int n = 0; n < 256; n++)
	{
		the_idt[n].ist = 0;
		the_idt[n].selector = SEGVAL(GDT_ENTRY_KERNEL_CODE, 0);
		the_idt[n].zero = 0;
		the_idt[n].type_attr = GDT_ACCESS_PRESENT | 0xE;
		register_irq(&the_idt[n], default_irq_handlers[n]);
	}
}