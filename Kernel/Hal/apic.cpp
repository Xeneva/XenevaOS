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

#include <Hal/apic.h>
#include <Hal/ioapic.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/x86_64_idt.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/serial.h>
#include <Mm/vmmngr.h>
#include <aucon.h>
#include <Hal/x86_64_pic.h>


static bool __x2apic = false;
static void* _apic = nullptr;
static uint64_t interrupt_period;

/*
* ReadAPICRegister -- reads a register of apic
* @param reg -- register to read
*/
uint64_t ReadAPICRegister(uint16_t reg) {
	if (__x2apic) {
		size_t msr = IA32_X2APIC_REGISTER_BASE_MSR + reg;
		return x64_read_msr(msr);
	}
	else {
		if (reg == LAPIC_REGISTER_ICR) {
			volatile uint32_t* reg_addr = raw_offset<volatile uint32_t*>(_apic, reg << 4);
			volatile uint32_t* reg_next_addr = raw_offset<volatile uint32_t*>(_apic, (reg + 1) << 4);
			return *reg_addr | ((uint64_t)*reg_next_addr << 32);
		}
		volatile uint32_t* reg_addr = raw_offset<volatile uint32_t*>(_apic, reg << 4);
		return *reg_addr;
	}
}

/*
* WriteAPICRegister -- writes a value to a register
* of APIC
* @param reg -- register to select
* @param value -- value to write
*/
void WriteAPICRegister(uint16_t reg, uint64_t value) {
	if (__x2apic) {
		size_t msr = IA32_X2APIC_REGISTER_BASE_MSR + reg;
		x64_write_msr(msr, value);
	}
	else {
		if (reg == LAPIC_REGISTER_ICR) {
			volatile uint32_t* reg_addr = raw_offset<volatile uint32_t*>(_apic, reg << 4);
			volatile uint32_t* reg_next_addr = raw_offset<volatile uint32_t*>(_apic, (reg + 1) << 4);
			uint32_t low_part = value & UINT32_MAX;
			uint32_t high_part = (value >> 32);
			*reg_next_addr = high_part;
			x64_mfence();
			*reg_addr = low_part;
		}
		volatile uint32_t* reg_addr = raw_offset<volatile uint32_t*>(_apic, reg << 4);
		*reg_addr = value;
	}
}

/*
 * APICLocalEOI -- sends end_of_interrupt message
 * to APIC
 */
void APICLocalEOI() {
	WriteAPICRegister(LAPIC_REGISTER_EOI, 1);
}

bool X2APICSupported() {
	size_t a, b, c, d;
	x64_cpuid(0x1, &a, &b, &c, &d);
	return false;
}

void IOWait() {
	volatile size_t counter = 0;
	for (; counter < 1000; ++counter);
}

void APICSpuriousInterrupt(size_t p, void* param){

}

#define PIC1  0x20
#define PIC2  0xA0
#define PIC1_COMMAND   PIC1
#define PIC1_DATA      (PIC1+1)
#define PIC2_COMMAND   PIC2
#define PIC2_DATA      (PIC2+1)

#define ICW1_ICW4   0x01
#define ICW1_SINGLE  0x02
#define ICW1_INTERVAL4 0x04
#define ICW1_LEVEL     0x08
#define ICW1_INIT      0x10

#define ICW4_8086  0x01
#define ICW4_AUTO  0x02
#define ICW4_BUF_SLAVE  0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM  0x10

#define APIC_DISABLE 0x10000

static int apic_timer_count = 0;

void ApicTimerInterrupt(size_t p, void* param) {
	x64_cli();
	apic_timer_count++;
	APICLocalEOI();
	x64_sti();
}

/*
 * AuAPICInitialise -- initialise the APIC 
 * @param bsp -- is this is initialised from bsp 
 * processor ?
 */
void AuAPICInitialise(bool bsp) {
	size_t apic_base;
	if (bsp) {
		apic_base = (size_t)0xFEE00000;
		apic_timer_count = 0;

		if (X2APICSupported()) {
			__x2apic = true;
			apic_base |= IA32_APIC_BASE_MSR_X2APIC;
		}
		else {
			_apic = (void*)AuMapMMIO(apic_base, 4);
		}

		apic_base |= IA32_APIC_BASE_MSR_ENABLE;
		x64_write_msr(IA32_APIC_BASE_MSR, apic_base);
	}
 else
	x64_write_msr(IA32_APIC_BASE_MSR, x64_read_msr(IA32_APIC_BASE_MSR) | IA32_APIC_BASE_MSR_ENABLE |
		(__x2apic ? IA32_APIC_BASE_MSR_X2APIC : 0));

	setvect(0xFF, APICSpuriousInterrupt);

	WriteAPICRegister(LAPIC_REGISTER_SVR, ReadAPICRegister(LAPIC_REGISTER_SVR) |
		IA32_APIC_SVR_ENABLE | 0xFF);


	/* here i thought to use pit for timer tuning, but
	 * apic stops working here when interrupt is enabled for
	 * pic
	 */

	WriteAPICRegister(LAPIC_TIMER_DIV, 0b1010);
	uint64_t before = cpu_read_tsc();
	WriteAPICRegister(LAPIC_REGISTER_TMRINITCNT, 1000000); //10000000000UL 
	while (ReadAPICRegister(LAPIC_REGISTER_TMRCURRCNT));
	uint64_t after = cpu_read_tsc();

	uint64_t ms = (after - before) / x86_64_cpu_get_mhz();
	uint64_t target = 10000000000UL / ms;

	/* Xeneva uses 128 as divider for base cpu frequency */
	interrupt_period = (x86_64_cpu_get_mhz() * 1000000) / 128;

	size_t timer_vect = 0x40;
	setvect(timer_vect, ApicTimerInterrupt);
	WriteAPICRegister(LAPIC_REGISTER_TMRDIV, 0b1010);   //bit 0,1 and 3
	size_t timer_reg = (1 << 17) | timer_vect;
	WriteAPICRegister(LAPIC_REGISTER_LVT_TIMER, timer_reg);
	IOWait();

	WriteAPICRegister(LAPIC_REGISTER_TMRINITCNT, target);  //123456  //(target / 100) //target/100
	
	x64_outportb(PIC1_DATA, 0xFF);
	IOWait();
	x64_outportb(PIC2_DATA, 0xFF);
	//for (;;);
	/* initialise IOAPIC here*/
	if (bsp)
		IOAPICInitialise((void*)0xFEC00000);
}


void APICTimerSleep(uint32_t ms) {
	uint32_t tick = ms + apic_timer_count;
	while (tick > apic_timer_count) 
		x64_pause();
}

/*
 *AuAPICGetInterruptPeriod -- returns the current interrupt
 * period timing in microsecond (us)
 */
uint64_t AuAPICGetInterruptPeriod() {
	return interrupt_period;
}