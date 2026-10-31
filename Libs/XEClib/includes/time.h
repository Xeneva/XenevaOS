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

#ifndef __TIME_H__
#define __TIME_H__

#include <_xeneva.h>
#include <sys/types.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#define US_PER_MS	  1000
#define MS_PER_SEC	  1000
#define SECS_PER_MIN  60
#define MINS_PER_HR	  60
#define HRS_PER_DAY	  24
#define DAYS_PER_YEAR 365

#define US_PER_SEC	(US_PER_MS * MS_PER_SEC)
#define US_PER_MIN	(US_PER_SEC * SECS_PER_MIN)
#define US_PER_HOUR (US_PER_MIN * MINS_PER_HR)

#define MS_PER_MIN (MS_PER_SEC * SECS_PER_MIN)
#define MS_PER_HR  (MS_PER_MIN * MINS_PER_HR)
#define MS_PER_DAY (MS_PER_HOUR * HRS_PER_DAY)

#define SECS_PER_HR	 (SECS_PER_MIN * MINS_PER_HR)
#define SECS_PER_DAY (SECS_PER_HR * HRS_PER_DAY)
#define SECS_PER_YR	 (SECS_PER_DAY * DAYS_PER_YEAR)

#ifndef NULL
#define NULL 0
#endif

	struct tm {
		int tm_sec;	  // seconds (0-59)
		int tm_min;	  // minutes (0-59)
		int tm_hour;  // hours (0-23)
		int tm_mday;  // day of the month (1-31)
		int tm_mon;	  // month (0-11)
		int tm_year;  // year (since 1900)
		int tm_wday;  // day of the week (0-6, 0=Sunday)
		int tm_yday;  // day in the year (0-364)
		int tm_isdst; // daylight saving time (0-1)
	};

	typedef struct _time_ {
		long tv_sec;
		long tv_usec;
	} timeval;

	struct timespec {
		time_t tv_sec;
		long tv_nsec;
	};

	XE_LIB char* asctime(const struct tm*);
	XE_LIB clock_t clock(void);
	XE_LIB char* ctime(const time_t);
	XE_LIB double difftime(time_t, time_t);
	XE_LIB struct tm* gmtime(const time_t*);
	XE_LIB time_t mktime(struct tm*);
	XE_LIB time_t time(time_t* t);
	XE_LIB int gettimeofday(timeval * val);
	XE_LIB int nanosleep(const struct timespec* req, struct timespec* rem);
#ifdef __cplusplus
}
#endif

#endif
