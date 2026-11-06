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

#include <Hal/ioapic.h>
#include <MM/vmmngr.h>
#include <Hal/x86_64_idt.h>
#include <stdint.h>

void *_IOAPICBase;

/* 
 * ReadIOAPICRegister -- reads an ioapic register
 * @param offset -- offset of the register
 */
static uint32_t ReadIOAPICRegister(const uint8_t offset) {
	volatile uint32_t* ioregsel = reinterpret_cast<volatile uint32_t*>(_IOAPICBase);
	volatile uint32_t* ioregwin = raw_offset<volatile uint32_t*>(_IOAPICBase, 0x10);
	*ioregsel = offset;
	return *ioregwin;
}

/*
 * WriteIOAPICRegister -- writes a value to an ioapic register
 * @param offset -- offset of the register
 * @param val -- value to write
 */
static void WriteIOAPICRegister(const uint8_t offset, const uint32_t val) {
	volatile uint32_t* ioregsel = reinterpret_cast<volatile uint32_t*>(_IOAPICBase);
	volatile uint32_t* ioregwin = raw_offset<volatile uint32_t*>(_IOAPICBase, 0x10);
	*ioregsel = offset;
	*ioregwin = val;
}

/*
 * IOAPICRegisterIRQ -- Maps an IOAPIC interrupt to a interrupt vector
 * @param vector -- vector number
 * @param fn -- function handler
 * @param irq -- IRQ number
 * @param level -- is this a level interrupt ?
 */
void IOAPICRegisterIRQ(size_t vector, void(*fn)(size_t, void*), uint8_t irq, bool level) {
	uint32_t reg = IOAPIC_REG_RED_TBL_BASE + irq * 2;
	uint32_t high_reg = ReadIOAPICRegister(reg + 1);
	high_reg &= ~0xff000000;
	high_reg |= (0 << 24);
	WriteIOAPICRegister(reg + 1, high_reg);
	uint32_t low = ReadIOAPICRegister(reg);

	low &= ~(1 << 16);

	if (level)
		low |= (1 << 15);
	else
		low |= (0 << 15);
	
	low |= (0 << 13);
	low |= (0 << 11);
	low |= vector + 32;
	WriteIOAPICRegister(reg, low);
	setvect(vector + 32, fn);
}


void IOAPICRedirect(uint8_t irq, uint32_t gsi, uint16_t flags, uint8_t apic) {
	uint32_t ioapic_base = 0xfec00000;
	uint64_t redirection = irq + 32;

	if (flags & 2)
		redirection |= 1 << 13;
	
	if (flags & 8)
		redirection |= 1 << 15;

	redirection |= ((uint64_t)apic) << 56;

	uint32_t ioredtbl = (gsi - 0) * 2 + 16;

	WriteIOAPICRegister(ioredtbl + 0, (uint32_t)redirection);
	WriteIOAPICRegister(ioredtbl + 1, (uint32_t)(redirection >> 32));

}


/*
 * IOAPICMaskIRQ -- masks an irq
 * @param irq -- Interrupt Request number
 * @param value -- if true, then mask, if false unmask
 */
void IOAPICMaskIRQ(uint8_t irq, bool value) {
	uint32_t reg = IOAPIC_REG_RED_TBL_BASE + irq * 2;
	WriteIOAPICRegister(reg + 1, ReadIOAPICRegister(0x02) << 24);
	uint32_t low = ReadIOAPICRegister(reg);

	if (value)
		low |= (1 << 16);
	else
		low &= ~(1 << 16);

	WriteIOAPICRegister(reg, low);
}

/*
 * IOAPICInitialise -- initialise ioapic
 * @param address -- ioapic base address
 */
void IOAPICInitialise(void* address) {
	uint64_t ioapic_phys = (uint64_t)address;
	_IOAPICBase = (void*)AuMapMMIO(ioapic_phys, 2);

	uint32_t ver = ReadIOAPICRegister(IOAPIC_REG_VER);
	uint32_t intr_num = (ver >> 16) & 0xff;
	for (size_t n = 0; n <= intr_num; ++n) {
		uint32_t reg = IOAPIC_REG_RED_TBL_BASE + n * 2;
		uint32_t val = ReadIOAPICRegister(reg);
		WriteIOAPICRegister(reg, val | (1 << 16));
	}
}