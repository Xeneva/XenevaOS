/**
* @file rtcmmio.c
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

#include <Drivers/rtcmmio.h>
#include <Hal/AA64/gic.h>
#include <aucon.h>
#include <Drivers/rtc.h>
#include <string.h>
#include <Hal/AA64/aa64lowlevel.h>

#define QEMU_VIRT_RTCIRQ 2
AuRTC __rtc;

#define PL031_RTC_MR 0x04
#define PL031_RTC_LR 0x08
#define PL031_RTC_CR 0x0C

#define PL031_RTC_CR_EN_BIT (1 << 0)
#define PL031_RTC_CR_IE_BIT (1 << 1)

#define rtc_write32(addr, value) (*(volatile uint32_t*)(addr) = (value))
#define rtc_read32(addr)		 (*(volatile uint32_t*)(addr))

/**
 * @brief AuRTCEnableInterrupt -- enable pl031 rtc
 */
void AuRTCEnableInterrupt() {
	rtc_write32(RTCIMSC, 1);
}

/**
 * @brief AuRTCGetCurrentTime -- returns thr current time
 * value
 * @return return current time value
 */
uint32_t AuRTCGetCurrentTime() {
	return rtc_read32(RTCDR);
}

/**
 * @brief AuRTCClearInterrupt -- clear pending rtc interrupt
 */
void AuRTCClearInterrupt() {
	rtc_write32(RTCICR, 1);
}

int is_leap(int year) {
	return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

/**
 * @brief epoch_to_tm -- convert unix epoch value to time
 * @param epoch -- unix epoch value
 */
void epoch_to_tm(uint32_t epoch) {
	static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint32_t days = epoch / 86400;
	uint32_t rem = epoch % 86400;

	__rtc.hour = rem / 3600;
	rem %= 3600;
	__rtc.minute = rem / 60;
	__rtc.second = rem % 60;

	int year = 1970;
	while (1) {
		int dim = is_leap(year) ? 366 : 365;
		if (days >= dim) {
			days -= dim;
			year++;
		} else
			break;
	}
	__rtc.year = year;
	int month = 0;
	while (1) {
		int dim = days_in_month[month];
		if (month == 1 && is_leap(year))
			dim++;
		if (days >= dim) {
			days -= dim;
			month++;
		} else
			break;
	}
	__rtc.month = month + 1;
	__rtc.day = days + 1;
	__rtc.century = (__rtc.year - 1) / 100 + 1;
}

/** 
 * @brief AuPL031RTCIRQHandler -- irq handler of PL031 RTC
 */
void AuPL031RTCIRQHandle() {
	uint32_t epoch = AuRTCGetCurrentTime();
	epoch_to_tm(epoch);
	AuRTCClearInterrupt();
}

/**
 * @brief AuPL031RTCInit -- initialize pl031 rtc 
 * [NOT USED]
 */
void AuPL031RTCInit() {
#ifdef __TARGET_BOARD_QEMU_VIRT__
	memset(&__rtc, 0, sizeof(AuRTC));
	GICEnableIRQ(QEMU_VIRT_RTCIRQ);

	rtc_write32(RTC_BASE + PL031_RTC_LR, 1);
	rtc_write32(RTC_BASE + PL031_RTC_MR, 0);
	rtc_write32(RTC_BASE + PL031_RTC_CR, 1);

	//AuRTCClearInterrupt();
	AuRTCEnableInterrupt();

	AuTextOut("PL031 RTC Initialized \n");
#endif
}

/**
* @brief AuRTCGetYear -- returns the current year
* @return return year value
*/
uint8_t AuRTCGetYear() {
	return __rtc.year;
}

/**
* @brief AuRTCGetCentury -- returns the current
* century
* @return return century value
*/
uint8_t AuRTCGetCentury() {
	return __rtc.century;
}

/**
* @brief AuRTCGetMinutes -- returns the current
* minutes
* @return return minute value
*/
uint8_t AuRTCGetMinutes() {
	return __rtc.minute;
}

/**
* @brief AuRTCGetSecond -- returns the current
* second
* @return return current second value
*/
uint8_t AuRTCGetSecond() {
	return __rtc.second;
}

/**
* @brief AuRTCGetDay -- returns the current
* day
* @return return current day value
*/
uint8_t AuRTCGetDay() {
	return __rtc.day;
}

/*
* AuRTCGetHour -- returns the current
* hour
*/
uint8_t AuRTCGetHour() {
	return __rtc.hour;
}

/**
* @brief AuRTCGetMonth -- returns the current
* month
*/
uint8_t AuRTCGetMonth() {
	return __rtc.month;
}
