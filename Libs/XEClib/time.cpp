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

#include <time.h>
#include <string.h>
#include <sys/_keproc.h>
#include <sys/_ketime.h>
#include <unistd.h>

int dayOfWeek(unsigned day, unsigned month, unsigned year) {
	if (month < 3) {
		month += 12;
		year -= 1;
	}

	return (((((13 * month) + 3) / 5) + day + year + (year / 4) - (year / 100) + (year / 400)) % 7);
}
char* asctime(const struct tm* timeptr) {
	char timeStr[26];
	memset(&timeStr, 0, 26);
	return (timeStr);
}
struct tm* gmtime(const time_t *timer) {
	int year = 0;
	int count;
	time_t timeSimple;
	static struct tm timeStruc;

	if (!timer)
		return NULL;
	timeSimple = *timer;
	static int monthDays[12] = {31, 00, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	memset(&timeStruc, 0, sizeof(tm));

	timeStruc.tm_sec = (timeSimple % SECS_PER_MIN);
	timeSimple -= timeStruc.tm_sec;

	timeStruc.tm_min = ((timeSimple % SECS_PER_HR) / SECS_PER_MIN);
	timeSimple -= (timeStruc.tm_min * SECS_PER_MIN);

	timeStruc.tm_hour = ((timeSimple % SECS_PER_DAY) / SECS_PER_HR);
	timeSimple -= (timeStruc.tm_hour * SECS_PER_HR);

	year = 1970;
	while (timeSimple >= SECS_PER_YR) {
		if (!(year % 4) && ((year % 100) || !(year % 400))) {
			if (timeSimple >= (SECS_PER_YR + SECS_PER_DAY))
				timeSimple -= (SECS_PER_YR + SECS_PER_DAY);
			else
				break;
		} else {
			timeSimple -= SECS_PER_YR;
		}
		year += 1;
	}

	timeStruc.tm_year = (year - 1900);

	timeStruc.tm_yday = (timeSimple / SECS_PER_DAY);

	if (!(year % 4) && ((year % 100) || !(year % 400)))
		monthDays[1] = 29;
	else
		monthDays[1] = 28;

	for (count = 0; count < 12; count++) {
		if (timeSimple < (unsigned)(monthDays[count] * SECS_PER_DAY))
			break;
		timeSimple -= (monthDays[count] * SECS_PER_DAY);
		timeStruc.tm_mon += 1;
	}

	timeStruc.tm_mday = ((timeSimple / SECS_PER_DAY) + 1);

	timeStruc.tm_wday =
		((dayOfWeek((timeStruc.tm_mday + 1), (timeStruc.tm_mon + 1), year) + 1) % 7);

	return (&timeStruc);
}

double difftime(time_t time1, time_t time0) {
	return ((double)(time1 - time0));
}

clock_t clock(void) {
	clock_t clk = 0;
	clk = _KeGetSystemTimerTick();
	return clk;
}

char* ctime(const time_t timeSimple) {
	struct tm* timeStruc = NULL;
	char* timeString = NULL;
	timeStruc = gmtime(&timeSimple);
	if (timeStruc)
		timeString = asctime(timeStruc);
	return (timeString);
}
time_t mktime(tm* timestruc) {
	int year = 0;
	time_t timeSimple = 0;
	int count;

	static const int monthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (!timestruc)
		return -1;

	year = (1900 + timestruc->tm_year);
	if (year < 1970) {
		return -1;
	}

	timeSimple = ((year - 1970) * SECS_PER_YR);

	for (int c = year; c >= 1972; c--) {
		if (!(c % 4) && ((c % 100) || !(c % 400)))
			timeSimple += SECS_PER_DAY;
	}

	for (int c = (timestruc->tm_mon - 1); c >= 0; c--)
		timeSimple += (monthDays[c] * SECS_PER_DAY);

	timeSimple += ((timestruc->tm_mday - 1) * SECS_PER_DAY);

	if (!(year % 4) && ((year % 100) || !(year % 400))) {
		if ((timestruc->tm_mon > 1) || ((timestruc->tm_mon == 1) && (timestruc->tm_mday > 28))) {
			timeSimple += SECS_PER_DAY;
		}
	}

	timeSimple += (timestruc->tm_hour * SECS_PER_HR);
	timeSimple += (timestruc->tm_min * SECS_PER_MIN);

	timeSimple += timestruc->tm_sec;

	return timeSimple;
}
time_t time(time_t* t) {
	int64_t sec = 0;
	int64_t nsec = 0;
	_KeGetWalltime(&sec, &nsec);
	if (t)
		*t = (time_t)sec;
	return (time_t)sec;
}

/**
 * unistd.h sleep implementation 
 */
unsigned int sleep(unsigned int seconds) {
	uint64_t ticks = (uint64_t)seconds * 1000ULL;
	_KeProcessSleep(ticks);
	return 0;
}

/**
 * implementation of time.h nanosleep
 */
static inline uint64_t ns_to_ticks_ceil(uint64_t total_ns) {
	return (total_ns + 999999ULL) / 1000000ULL;
}

static inline void ticks_to_timespec(uint64_t ticks, struct timespec* ts) {
	uint64_t total_ns = ticks * 1000000ULL;
	ts->tv_sec = total_ns / 1000000000ULL;
	ts->tv_nsec = total_ns % 1000000000ULL;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
	if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0)
		return -1;

	uint64_t total_ns = (uint64_t)req->tv_sec * 1000000000ULL + (uint64_t)req->tv_nsec;
	uint64_t ticks = ns_to_ticks_ceil(total_ns);

	_KeProcessSleep(ticks);
	return 0;
}