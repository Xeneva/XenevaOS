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

#include <Hal/x86_64_cpu.h>
#include <Hal/pcpu.h>
#include <Hal/x86_64_gdt.h>
#include <Hal/x86_64_idt.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/apic.h>
#include <Hal/x86_64_ap_init.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <aucon.h>
#include <string.h>
#include <time.h>
#include <Hal/serial.h>

TSS* _tss;
bool _fxsave = false;
uint64_t cpuMhz;
uint64_t tscBasisTiming;
uint64_t tscBasisTimingKhz;
uint64_t bootTime;

/*
 * x86_64_enable_syscall_ext -- enablse syscall 
 * extension
 */
void x86_64_enable_syscall_ext() {
	size_t efer = x64_read_msr(IA32_EFER);
	efer |= (1 << 11);
	efer |= 1;
	x64_write_msr(IA32_EFER, efer);
}

/*
 * x86_64_init_user -- initialise the user land
 * @param bit -- either 32 or 64 bit
 */
void x86_64_init_user(size_t bit) {
	uint16_t data_sel = SEGVAL(GDT_ENTRY_USER_DATA, 3);
	uint16_t code_sel = 0;
	switch (bit) {
	case 64:
		code_sel = SEGVAL(GDT_ENTRY_USER_CODE, 3);
		break;
	case 32:
		code_sel = SEGVAL(GDT_ENTRY_USER_CODE32, 3);
		break;
	default:
		return;
	}

	gdtr peek_gdt;
	x64_sgdt(&peek_gdt);
	gdt_entry& tss_entry = peek_gdt.gdtaddr[GDT_ENTRY_TSS];

	_tss = (TSS*)(tss_entry.base_low + (tss_entry.base_mid << 16) + (tss_entry.base_high << 24) +
		((uint64_t)*(uint32_t*)&peek_gdt.gdtaddr[GDT_ENTRY_TSS + 1] << 32));

}

/*
 * x86_64_init_user_ap -- initialise user land for application
 * processor's
 * @param bit -- cpu bit
 */
void x86_64_init_user_ap(size_t bit) {

	uint16_t data_sel = SEGVAL(GDT_ENTRY_USER_DATA, 3);
	uint16_t code_sel = 0;
	switch (bit) {
	case 64:
		code_sel = SEGVAL(GDT_ENTRY_USER_CODE, 3);
		break;
	case 32:
		code_sel = SEGVAL(GDT_ENTRY_USER_CODE32, 3);
		break;
	default:
		return;
	}

	gdtr peek_gdt;
	x64_sgdt(&peek_gdt);
	gdt_entry& tss_entry = peek_gdt.gdtaddr[GDT_ENTRY_TSS];

	TSS *tss_ = (TSS*)(tss_entry.base_low + (tss_entry.base_mid << 16) + (tss_entry.base_high << 24) + ((uint64_t)*(uint32_t*)&peek_gdt.gdtaddr[GDT_ENTRY_TSS + 1] << 32));

	AuPerCPUSetKernelTSS(tss_);
}

/*
 * x86_64_get_tss -- returns the tss
 */
TSS* x86_64_get_tss() {
	return _tss;
}

/*
 * x86_64_hal_cpu_feature_enable -- enable cpu features
 */
void x86_64_hal_cpu_feature_enable() {
	uint64_t cr0 = x64_read_cr0();
	cr0 &= ~(1 << 2);
	cr0 |= (1 << 1);
	x64_write_cr0(cr0);

	size_t a, b, c, d;
	x64_cpuid(1, &a, &b, &c, &d, 0);
	if ((c & (1 << 26)) != 0) {
		uint64_t cr4 = x64_read_cr4();
		cr4 |= (1 << 18);
		x64_write_cr4(cr4);
	}

	if ((d & (1 << 25)) != 0) {
		size_t cr4 = x64_read_cr4();

		if ((d & (1 << 24)) != 0) {
			cr4 |= (1 << 9);
			_fxsave = true;
		}

		// don't enable TLS 
		cr4 |= (1 << 10);
		x64_write_cr4(cr4);
	}

	else if ((d & (1 << 26)) != 0) {
		//supported SSE2
	}

	else if ((c & (1 << 0)) != 0) {
		//supported SSE3
	}
}


bool x86_64_is_cpu_fxsave_supported() {
	return _fxsave;
}

/*
 * x86_64_cpu_msi_address -- calculates the cpu msi address
 * @param data -- msi data to return
 * @param vector -- interrupt vector number
 * @param processor -- processor number
 * @param edge -- edge triggered or level triggered
 * @param deassert -- deassert bit
 */
uint64_t x86_64_cpu_msi_address(uint64_t* data, size_t vector, uint32_t processor, uint8_t edge, uint8_t deassert) {
	*data = (vector & 0xFF) | (edge == 1 ? 0 : (1 << 15)) | (deassert == 1 ? 0 : (1 << 14));
	//*data = low;
	return (0xFEE00000 | (processor << 12));
}


bool __ApStarted;

uint64_t _ICRDest(uint32_t processor) {
	if (X2APICSupported())
		return ((uint64_t)processor << 32);
	else
		return ((uint64_t)processor << 56);
}

bool _ICRBusy() {
	return (ReadAPICRegister(LAPIC_REGISTER_ICR) & (1 << 12)) != 0;
}


/*
 * x86_64_cpu_initialize -- initialise all Application processor
 * except the BSP processor
 * @param num_cpu -- cpu number to initialise except 0
 */
void x86_64_cpu_initialize(uint8_t num_cpu) {
	if (num_cpu == 0)
		return;

	//! fixed address
	uint64_t *apdata = (uint64_t*)0xA000;
	uint64_t ap_init_address = (uint64_t)x86_64_ap_init;
	uint64_t ap_aligned_address = (uint64_t)apdata;

	uint64_t *pml4 = (uint64_t*)AuGetRootPageTable();
	
	for (int i = 1; i <= num_cpu; i++) {

		/* In SMP Mode : no more than 8 cpus */
		if (i == 8)
			break;
	
		__ApStarted = false;


		void *stack_address = AuPmmngrAlloc();
		*(uint64_t*)(ap_aligned_address + 8) = (uint64_t)pml4;
		*(uint64_t*)(ap_aligned_address + 16) = (uint64_t)stack_address;
		*(uint64_t*)(ap_aligned_address + 24) = ap_init_address;
		*(uint64_t*)(ap_aligned_address + 32) = (uint64_t)P2V((size_t)AuPmmngrAlloc());
		void* cpu_struc = (void*)P2V((uint64_t)AuPmmngrAlloc());
		CPUStruc *cpu = (CPUStruc*)cpu_struc;
		cpu->cpu_id = i;
		cpu->au_current_thread = 0;
		cpu->kernel_tss = 0;
		*(uint64_t*)(ap_aligned_address + 40) = (uint64_t)cpu_struc;


		WriteAPICRegister(LAPIC_REGISTER_ICR, _ICRDest(i) | 0x4500);
		while (_ICRBusy());


		size_t startup_ipi = _ICRDest(i) | 0x4600 | ((size_t)apdata >> 12);
		WriteAPICRegister(LAPIC_REGISTER_ICR, startup_ipi);
		while (_ICRBusy());
		for (int i = 0; i < 10000000; i++)
			;
		WriteAPICRegister(LAPIC_REGISTER_ICR, startup_ipi);
		while (_ICRBusy());

		for (int i = 0; i < 10000000; i++)
			;
		do {
			x64_pause();
		} while (!__ApStarted);
	}
}

/*
 * x86_64_set_ap_start_bit -- set a bit in __ApStarted
 * @param value -- bit to set
 */
void x86_64_set_ap_start_bit(bool value) {
	__ApStarted = value;
}

extern "C" void syscall_entry();
extern "C" void x64_syscall_entry_compat();

/*
 * x86_64_initialise_syscall -- initialise the syscall service
 */
void x86_64_initialise_syscall() {
	uint64_t syscall_sel = SEGVAL(GDT_ENTRY_KERNEL_CODE, 0);
	uint64_t sysret_sel = SEGVAL(GDT_ENTRY_USER_CODE32, 3);

	x64_write_msr(IA32_STAR, (sysret_sel << 48) | (syscall_sel << 32));
	x64_write_msr(IA32_LSTAR, (size_t)&syscall_entry);
	x64_write_msr(IA32_SFMASK, IA32_EFLAGS_INTR | IA32_EFLAGS_DIRF);
	x64_write_msr(IA32_CSTAR, (size_t)&x64_syscall_entry_compat);
}

/*
 * cpu_read_tsc -- read the tsc count
 */
uint64_t cpu_read_tsc(){
	uint32_t hi = 0;
	uint32_t lo = 0;
	x64_rdtsc(&hi, &lo);
	uint64_t count = (((uint64_t)hi << 32UL) | (uint64_t)lo);
	return count;
}


/*
 * inspired from Toaru operating system project by
 * klange, http://toaruos.org 
 */

#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71
#define fromBCD(val) ((val / 16) * 10 + (val & 0xf))

enum{
	CMOS_SECOND = 0,
	CMOS_MINUTE = 2,
	CMOS_HOUR = 4,
	CMOS_DAY = 7,
	CMOS_MONTH = 8,
	CMOS_YEAR = 9
};

void CMOSDump(uint16_t *val) {
	for (uint16_t i = 0; i < 128; ++i) {
		x64_outportb(CMOS_ADDRESS, i);
		val[i] = x64_inportb(CMOS_DATA);
	}
}

int isUpdateInProgress(void) {
	x64_outportb(CMOS_ADDRESS, 0x0a);
	return x64_inportb(CMOS_DATA) & 0x80;
}

uint64_t secs_of_years(int years) {
	uint64_t days = 0;
	years += 2000;
	while (years > 1969) {
		days += 365;
		if (years % 4 == 0) {
			if (years % 100 == 0) {
				if (years % 400 == 0) {
					days++;
				}
			}
			else {
				days++;
			}
		}
		years--;
	}
	return days * 86400;
}

static uint64_t secs_of_month(int months, int year) {
	year += 2000;

	uint64_t days = 0;
	switch (months) {
	case 11:
		days += 30; /* fallthrough */
	case 10:
		days += 31; /* fallthrough */
	case 9:
		days += 30; /* fallthrough */
	case 8:
		days += 31; /* fallthrough */
	case 7:
		days += 31; /* fallthrough */
	case 6:
		days += 30; /* fallthrough */
	case 5:
		days += 31; /* fallthrough */
	case 4:
		days += 30; /* fallthrough */
	case 3:
		days += 31; /* fallthrough */
	case 2:
		days += 28;
		if ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) {
			days++;
		} /* fallthrough */
	case 1:
		days += 31; /* fallthrough */
	default:
		break;
	}
	return days * 86400;
}

uint64_t readCMOS(void) {
	uint16_t values[128];
	uint16_t old_values[128];

	while (isUpdateInProgress());
	CMOSDump(values);

	do {
		memcpy(old_values, values, 128);
		while (isUpdateInProgress());
		CMOSDump(values);
	} while ((old_values[CMOS_SECOND] != values[CMOS_SECOND]) ||
		(old_values[CMOS_MINUTE] != values[CMOS_MINUTE]) ||
		(old_values[CMOS_HOUR] != values[CMOS_HOUR]) ||
		(old_values[CMOS_DAY] != values[CMOS_DAY]) ||
		(old_values[CMOS_MONTH] != values[CMOS_MONTH]) ||
		(old_values[CMOS_YEAR] != values[CMOS_YEAR]));

	/* Math Time */
	uint64_t time =
		secs_of_years(fromBCD(values[CMOS_YEAR]) - 1) +
		secs_of_month(fromBCD(values[CMOS_MONTH]) - 1,
		fromBCD(values[CMOS_YEAR])) +
		(fromBCD(values[CMOS_DAY]) - 1) * 86400 +
		(fromBCD(values[CMOS_HOUR])) * 3600 +
		(fromBCD(values[CMOS_MINUTE])) * 60 +
		fromBCD(values[CMOS_SECOND]) + 0;

	return time;
}

#define SUBSECONDS_PER_SECOND 1000000

void updateTicks(uint64_t ticks, uint64_t *timerTick, uint64_t *timerSubticks) {
	*timerSubticks = ticks - tscBasisTiming;
	*timerTick = *timerSubticks / SUBSECONDS_PER_SECOND;
	*timerSubticks = *timerSubticks % SUBSECONDS_PER_SECOND;
}


void x86_64_measure_cpu_speed() {
	x64_cli();
	bootTime = 0;
	bootTime = readCMOS();
	uint8_t al = x64_inportb(0x61);
	al &= 0xDD;
	al |= 0x1;
	x64_outportb(0x61, al);

	x64_outportb(0x43, 0xB2);
	x64_outportb(0x42, 0x9B);
	al = x64_inportb(0x60);

	x64_outportb(0x42, 0x2E);

	al = x64_inportb(0x61);
	al &= 0xDE;
	x64_outportb(0x61, al);

	al |= 0x01;
	x64_outportb(0x61, al);
	unsigned long long stsc = cpu_read_tsc();
	
	uint8_t count_lo = x64_inportb(0x61);
	count_lo &= 0x20;
	while (count_lo) {
		count_lo = x64_inportb(0x61);
		count_lo &= 0x20;
	}

	uint8_t count_hi = x64_inportb(0x61);
	count_hi &= 0x20;
	unsigned long long etsc = cpu_read_tsc();
	uint64_t cpu_hz = (etsc - stsc) / 10000;
	if (cpu_hz == 0){
		x64_outportb(0x43, 0x04);
		while (count_hi == 0){
			count_hi = x64_inportb(0x61);
			count_hi &= 0x20;
		}
		etsc = cpu_read_tsc();
	}
	cpu_hz = (etsc - stsc) / 10000;
	cpuMhz = cpu_hz;
	tscBasisTiming = stsc / cpuMhz;
	tscBasisTimingKhz = stsc / (cpuMhz * 1000);
}

uint64_t x86_64_cpu_get_mhz() {
	return cpuMhz;
}

int x86_64_gettimeofday(timeval *t){
	uint64_t tsc = cpu_read_tsc();
	uint64_t timer_ticks, timer_subticks;
	updateTicks(tsc / cpuMhz, &timer_ticks, &timer_subticks);
	t->tv_sec = bootTime + timer_ticks;
	t->tv_usec = timer_subticks;
	return 0;
}


/* 
 *  x86_64_calculate_ticks -- calculate the number of ticks from given milliseconds
 * @param milliseconds -- amount of milliseconds
 * @param out_milliseconds -- where to store the number of ticks
 */
void x86_64_calculate_ticks(uint64_t milliseconds,uint64_t subsec, uint64_t* out_milliseconds, uint64_t* out_subsec) {
	uint64_t tsc_time = cpu_read_tsc();
	uint64_t timer_ticks = 0;
	uint64_t timer_subticks = 0;
	updateTicks(tsc_time / cpuMhz, &timer_ticks, &timer_subticks);
	//SeTextOut("timer_tick -> %d %d\r\n", timer_ticks, (timer_ticks + milliseconds));
	if (subsec + timer_subticks >= SUBSECONDS_PER_SECOND) {
		*out_milliseconds = timer_ticks + milliseconds + (subsec + timer_subticks) / SUBSECONDS_PER_SECOND;
		*out_subsec = (subsec + timer_subticks) % SUBSECONDS_PER_SECOND;
	}
	else {
		*out_milliseconds = timer_ticks + milliseconds;
		*out_subsec = timer_subticks + subsec;
	}
}

/*
 * x86_64_udelay -- udelay
 */
void x86_64_udelay(uint64_t usec) {
	uint64_t start = cpu_read_tsc();
	uint64_t end = start + (usec * cpuMhz);
	while (cpu_read_tsc() < end) {
		x64_pause();
	}
}