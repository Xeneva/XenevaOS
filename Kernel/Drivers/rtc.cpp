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

#include <Drivers/rtc.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/hal.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <autimer.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

AuRTC* __rtc;

uint8_t AuRTCGetRegister(int reg_num) {
	x64_outportb(CMOS_ADDR, reg_num);
	return x64_inportb(CMOS_DATA);
}

void AuRTCSetRegister(uint16_t reg_num, uint8_t val) {
	x64_outportb(CMOS_ADDR, reg_num);
	x64_outportb(CMOS_DATA, val);
}

int AuRTCIsUpdating() {
	x64_outportb(CMOS_ADDR, 0x0A);
	uint32_t status = x64_inportb(CMOS_DATA);
	return (status & 0x80);
}

/*
* Read current date and time from rtc, store in global var current_datetime
* */
void AuRTCReadDateTime() {
	// Wait until rtc is not updating
	while (AuRTCIsUpdating());

	__rtc->second = AuRTCGetRegister(0x00);
	__rtc->minute = AuRTCGetRegister(0x02);
	__rtc->hour = AuRTCGetRegister(0x04);
	__rtc->day = AuRTCGetRegister(0x07);
	__rtc->month = AuRTCGetRegister(0x08);
	__rtc->year = AuRTCGetRegister(0x09);

	uint8_t registerB = AuRTCGetRegister(0x0B);

	// Convert BCD to binary values if necessary
	if (!(registerB & 0x04)) {
		__rtc->second = (__rtc->second & 0x0F) + ((__rtc->second / 16) * 10);
		__rtc->minute = (__rtc->minute & 0x0F) + ((__rtc->minute / 16) * 10);
		__rtc->hour = ((__rtc->hour & 0x0F) + (((__rtc->hour & 0x70) / 16) * 10)) | (__rtc->hour & 0x80);
		__rtc->day = (__rtc->day & 0x0F) + ((__rtc->day / 16) * 10);
		__rtc->month = (__rtc->month & 0x0F) + ((__rtc->month / 16) * 10);
		__rtc->year = (__rtc->year & 0x0F) + ((__rtc->year / 16) * 10);
	}
}

void AuRTCClockUpdate(size_t v, void* p) {
	AuDisableInterrupt();

	bool ready = AuRTCGetRegister(0x0C) & 0x10;
	if (ready)
		AuRTCReadDateTime();

	AuTimerFire(__rtc->second, __rtc->minute, __rtc->hour);
	
	AuEnableInterrupt();
	AuInterruptEnd(8);
}

/*
 * AuRTCInitialize -- initialize rtc clock
 */
void AuRTCInitialize() {
	__rtc = (AuRTC*)kmalloc(sizeof(AuRTC));
	memset(__rtc, 0, sizeof(AuRTC));

	uint8_t status = AuRTCGetRegister(0x0b);
	status |= 0x02;
	status |= 0x10;
	status &= ~0x20;
	status &= ~0x40;
	__rtc->bcd = !(status & 0x04);

	x64_outportb(0x70, 0x0B);
	x64_outportb(0x71, status);

	AuRTCGetRegister(0x0C);
	AuRTCReadDateTime();

	AuHalRegisterIRQ(8, AuRTCClockUpdate, 8, false);
}


/*
 * AuRTCGetYear -- returns the current year
 */
uint8_t AuRTCGetYear() {
	return __rtc->year;
}

/*
 * AuRTCGetCentury -- returns the current
 * century
 */
uint8_t AuRTCGetCentury() {
	return __rtc->century;
}

/*
 * AuRTCGetMinutes -- returns the current
 * minutes
 */
uint8_t AuRTCGetMinutes() {
	return __rtc->minute;
}

/*
 * AuRTCGetSecond -- returns the current
 * second
 */
uint8_t AuRTCGetSecond() {
	return __rtc->second;
}

/*
 * AuRTCGetDay -- returns the current
 * day
 */
uint8_t AuRTCGetDay() {
	return __rtc->day;
}

/*
 * AuRTCGetHour -- returns the current
 * hour
 */
uint8_t AuRTCGetHour() {
	return __rtc->hour;
}

/*
 * AuRTCGetMonth -- returns the current 
 * month
 */
uint8_t AuRTCGetMonth() {
	return __rtc->month;
}

