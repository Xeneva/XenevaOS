/**
* @file _ketime.h
* 
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

#ifndef __KE_TIME_H__
#define __KE_TIME_H__

#include <stdint.h>
#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

/* defined in seconds */
#define TZ_SEC_IST_INDIA  19800 //+5:30
#define TZ_SEC_CHINA_STD  28800 //+8:00
#define TZ_SEC_JAPAN_STD  32400 //+9:00
#define TZ_SEC_NEPAL	  20700 //+5:45
#define TZ_SEC_PAKISTAN	  18000 //+5:00
#define TZ_SEC_UAE_GULF	  14400 //+4:00
#define TZ_SEC_BANGLADESH 21600 //+6:00

#pragma pack(push, 1)
	typedef struct _xe_time_ {
		uint8_t century;
		uint8_t year;
		uint8_t month;
		uint8_t day;
		uint8_t hour;
		uint8_t minute;
		uint8_t second;
	} XETime;
#pragma pack(pop)

	/**
	* @brief _KeGetCurrentTime -- get current time
	* @param time -- pointer to time memory area
	*/
	XE_EXPORT int _KeGetCurrentTime(XETime * time);

	XE_LIB uint64_t _KeGetCurrentUS();

	XE_LIB uint64_t _KeGetCurrentMS();

	XE_LIB void _KeSetWalltime(int64_t sec, int64_t nsec);

	XE_LIB void _KeGetWalltime(int64_t* sec, int64_t* nsec);

#ifdef __cplusplus
}
#endif

#endif