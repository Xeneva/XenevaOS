/**
* @file aa64cpu.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#include <stdint.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/vector.h>
#include <Hal/AA64/gic.h>
#include <aucon.h>
#include <Hal/basicacpi.h>
#include <Drivers/uart.h>
#include <Drivers/rtcmmio.h>
#include <kernelAA64.h>
#include <Board/board.h>

uint64_t basisTime;
uint64_t cpuFrequency;
uint64_t bootTime;

uint64_t AA64GetPhysicalTimerCount() {
	uint64_t val = get_cntpct_el0();
	return val * 100;
}

/**
 * @brief AA64FPUNeonEnable -- initialize FPU and
 * NEON feature
 */
void AA64FPUNeonEnable() {
	uint64_t cpacr_el1 = get_cpacr_el1();
	cpacr_el1 |= (3 << 20) | (3 << 16);
	set_cpacr_el1(cpacr_el1);
	isb_flush();
}

/**
 * @brief AA64ClockInitialize -- initialize the clock
 */
void AA64ClockInitialize() {
	uint64_t val = get_cntfrq_el0();
	cpuFrequency = val / 10000;

	basisTime = AA64GetPhysicalTimerCount() / cpuFrequency;
	bootTime = AuAA64BoardGetBootEpoch();
	AuTextOut("[aurora]: using %d mHZ \r\n", cpuFrequency);
}

#define CACHE_LINE_SIZE 64
void aa64_data_cache_clean_range(void* addr, size_t size) {
	size_t start = (size_t)addr;
	size_t end = start + size;

	start &= ~((uint64_t)(CACHE_LINE_SIZE - 1));
	for (size_t p = start; p < end; p += CACHE_LINE_SIZE)
		data_cache_flush((uint64_t*)p);

	dsb_sy_barrier();
	dsb_ish();
	isb_flush();
}

uint32_t get_cache_line_sz() {
	uint64_t ctr = read_ctr_el0();
	uint32_t dminline = (ctr >> 16) & 0xF;
	return (4 << dminline);
}

void aa64_dc_cvac_range(void* addr, size_t sz) {
	uint64_t start = (uint64_t)addr;
	size_t end = start + sz;

	uint32_t line_sz = get_cache_line_sz();
	start &= ~((uint64_t)(line_sz - 1));
	for (size_t p = start; p < end; p += line_sz) {
		dc_cvac(p);
	}

	dsb_sy_barrier();
	isb_flush();
	AA64SleepUS(100);
}

void aa64_dc_ivac_range(void* addr, size_t sz) {
	size_t start = (size_t)addr;
	size_t end = start + sz;

	uint32_t line_sz = get_cache_line_sz();

	start &= ~((uint64_t)(line_sz - 1));

	for (size_t p = start; p < end; p += line_sz) {
		dc_ivac(p);
	}

	dsb_sy_barrier();
	isb_flush();
	AA64SleepUS(100);
}

void AA64CPUImplementer(uint32_t midr) {
	uint8_t iid = (midr >> 24) & 0xFF;
	uint8_t arch = (midr >> 16) & 0xF;
	switch (iid) {
	case CPU_IMPLEMENTER_ARM:
		AuTextOut("CPU Implementer: ARM Limited \r\n");
		break;
	case CPU_IMPLEMENTER_BROADCOM:
		AuTextOut("CPU Implementer: Broadcom \r\n");
		break;
	case CPU_IMPLEMENTER_CAVIUM:
		AuTextOut("CPU Implementer: Cavium \r\n");
		break;
	case CPU_IMPLEMENTER_FUJITSU:
		AuTextOut("CPU Implementer: Fujitsu \r\n");
		break;
	case CPU_IMPLEMENTER_INTEL:
		AuTextOut("CPU Implementer: Intel \r\n");
		break;
	case CPU_IMPLEMENTER_APPLIED_MICRO:
		AuTextOut("CPU Implementer: Applied Micro \r\n");
		break;
	case CPU_IMPLEMENTER_QUALCOMM:
		AuTextOut("CPU Implementer: Qualcomm \r\n");
		break;
	case CPU_IMPLEMENTER_MARVELL:
		AuTextOut("CPU Implementer: Marvell \r\n");
		break;
	case CPU_IMPLEMENTER_APPLE:
		AuTextOut("CPU Implementer: Apple \r\n");
		break;
	default:
		AuTextOut("CPU Implementer: Unknown \r\n");
		break;
	}
	//AuTextOut("CPU Architecture: ARMv8-A(%x) \r\n", arch);
}

#define SUBSECONDS_PER_SECOND 1000000

void updateTicks(uint64_t ticks, uint64_t* timerTick, uint64_t* timerSubticks) {
	*timerSubticks = ticks - basisTime;
	*timerTick = *timerSubticks / SUBSECONDS_PER_SECOND;
	*timerSubticks = *timerSubticks % SUBSECONDS_PER_SECOND;
}

int aa64_gettimeofday(timeval* t) {
	uint64_t cur_cnt = AA64GetPhysicalTimerCount();
	uint64_t timer_ticks, timer_subticks;
	updateTicks(cur_cnt / cpuFrequency, &timer_ticks, &timer_subticks);
	t->tv_sec = bootTime + timer_ticks;
	t->tv_usec = timer_subticks;
	return 0;
}

uint64_t aa64_now() {
	timeval t;
	aa64_gettimeofday(&t);
	return t.tv_sec;
}

int aa64_settimeofday(timeval* t) {
	if (!t)
		return -1;
	if (t->tv_sec < 0 || t->tv_usec < 0 || t->tv_usec > 1000000)
		return -1;

	//atomic lock
	uint64_t clock = aa64_now();
	bootTime += t->tv_sec - clock;
	//atomic unlock
	return 0;
}

/*
 * aa64_calculate_ticks -- calculate the number of ticks from given milliseconds
 * @param seconds -- amount of seconds
 * @param out_milliseconds -- where to store the number of ticks
 */
void aa64_calculate_ticks(uint64_t seconds,
						  uint64_t subsec,
						  uint64_t* out_seconds,
						  uint64_t* out_subsec) {
	if (bootTime == 0) {
		*out_seconds = 0;
		*out_subsec = 0;
		return;
	}

	uint64_t curr = AA64GetPhysicalTimerCount();
	uint64_t timer_ticks, timer_subticks;
	updateTicks(curr / cpuFrequency, &timer_ticks, &timer_subticks);
	if (subsec + timer_subticks >= SUBSECONDS_PER_SECOND) {
		*out_seconds = timer_ticks + seconds + (subsec + timer_subticks) / SUBSECONDS_PER_SECOND;
		*out_subsec = (subsec + timer_subticks) % SUBSECONDS_PER_SECOND;
	} else {
		*out_seconds = timer_ticks + seconds;
		*out_subsec = timer_subticks + subsec;
	}
}
/**
 * @brief AA64SleepUS -- sleep for sometimes
 * @param us -- microseconds to sleep
 */
void AA64SleepUS(uint32_t us) {
	AuAA64BoardSleepUS(us);
}

/**
 * @brief AA64SleepMS -- sleep for sometimes
 * @param ms -- milliseconds to sleep
 */
void AA64SleepMS(uint32_t ms) {
	AuAA64BoardSleepMS(ms);
}

extern void enableAlignCheck();
/**
 * @brief AA64CpuInitialize -- initialize aa64 cpu
 */
void AA64CpuInitialize() {
	cpuFrequency = 0;
	basisTime = 0;

	/*
	 * Initialize the vector table
	 */
	AA64InitializeVectorTable();

	/* Initialize the clock */
	AA64ClockInitialize();

	/* enable FPU and NEON */
	AA64FPUNeonEnable();

	/* because little boot always enable align check 
	 * so skip this
	 */
	if (!AuLittleBootUsed()) {
		enableAlignCheck();
	}

	uint32_t id = read_midr();
	AA64CPUImplementer(id);

	enable_irqs();

	//
	//initialize Interrupt controller GIC

	//if smp, set per core datas
}

void AA64TimerSetup() {
#ifdef __TARGET_BOARD_RPI3__
	/* already enabled during bcm2836-armctl-ic initialization */
	return;
#else
	setupTimerIRQ();
	GICEnableIRQ(27);
	GICClearPendingIRQ(27);
	isb_flush();
#endif
}

/**
 * @brief AA64CPUPostInitialize -- initilaize post cpu requirements
 * @param info -- Pointer to KERNEL BOOT INFORMATIONs
 */
void AA64CPUPostInitialize(KERNEL_BOOT_INFO* info) {
	AuACPIInitialise(info->acpi_table_pointer);
	UARTInitialize();

	//enable_irqs();
	mask_irqs();
	suspendTimer();
	GICInitialize();
	AA64TimerSetup();
	
	//PS/2 Enable
	
	uint32_t id = read_midr();
	AA64CPUImplementer(id);
	AA64PCIeInitialize();
	AuTextOut("[aurora]: cpu post initialized \r\n");
	//AuPL031RTCInit();
	//mask_irqs();
}

uint64_t AA64CPUGetFreqencyHz() {
	return cpuFrequency;
}