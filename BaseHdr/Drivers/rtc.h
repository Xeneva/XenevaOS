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

#ifndef __RTC_H__
#define __RTC_H__

#include <stdint.h>
#include <aurora.h>


typedef struct __rtc__ {
	bool bcd;
	uint8_t century;
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
}AuRTC;

/*
* AuRTCInitialize -- initialize rtc clock
*/
extern void AuRTCInitialize();

/*
* AuRTCGetYear -- returns the current year
*/
AU_EXTERN AU_EXPORT uint8_t AuRTCGetYear();

/*
* AuRTCGetCentury -- returns the current
* century
*/
AU_EXTERN AU_EXPORT uint8_t AuRTCGetCentury();

/*
* AuRTCGetMinutes -- returns the current
* minutes
*/
AU_EXTERN AU_EXPORT uint8_t AuRTCGetMinutes();

/*
* AuRTCGetSecond -- returns the current
* second
*/
AU_EXTERN AU_EXPORT uint8_t AuRTCGetSecond();

/*
* AuRTCGetDay -- returns the current
* day
*/
AU_EXTERN AU_EXPORT uint8_t AuRTCGetDay();

/*
* AuRTCGetHour -- returns the current
* hour
*/
AU_EXTERN AU_EXPORT uint8_t AuRTCGetHour();

/*
* AuRTCGetMonth -- returns the current
* month
*/
AU_EXTERN AU_EXPORT uint8_t AuRTCGetMonth();

#endif