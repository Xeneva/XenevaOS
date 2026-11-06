/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

/*
 * calevt.h -- calendar event store, shared by the Calendar app and
 * XELnch's LuitFlow ribbon. Header only, no allocation.
 *
 * On disk (FAT32 without long file names, so every name is 8.3):
 *
 *     /CALEVT/              one folder for all events
 *     /CALEVT/202611.EVT    one file per month: YYYYMM.EVT
 *
 *   file = CalEvtHeader + count * CalEvent, events sorted by
 *          (day, hour, minute). A month with no events has no file.
 *
 * Costs: a month is one read / one write of at most 4060 bytes.
 * Lookups are binary searches over the sorted month, O(log n); an insert
 * is O(log n) to find the slot + one memmove; the "which days have
 * events" mask is rebuilt in O(n) only when the month changes.
 */

#ifndef __CALEVT_H__
#define __CALEVT_H__

#include <stdint.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_ketime.h>

#define CALEVT_DIR		 "/CALEVT"
#define CALEVT_MAGIC	 0x31454358u /* "XCE1" */
/* 92 events keep a month file (12 + 92 * 44 = 4060 bytes) inside one
 * 4 KB page: one page per write call is what the FAT write path stages */
#define CALEVT_MAX		 92			 /* events per month              */
#define CALEVT_TITLE_MAX 40
#define CALEVT_TZ		 TZ_SEC_IST_INDIA

#define CALEVT_FLAG_ALLDAY (1 << 0)

typedef struct _cal_event_ {
	uint8_t day; /* 1..31 */
	uint8_t hour;
	uint8_t minute;
	uint8_t flags;
	char title[CALEVT_TITLE_MAX];
} CalEvent;

typedef struct _cal_evt_header_ {
	uint32_t magic;
	uint16_t count;
	uint16_t year;
	uint8_t month;
	uint8_t reserved[3];
} CalEvtHeader;

/*
 * hdr and ev[] are adjacent (CalEvent has 1-byte alignment), so the file
 * image is one contiguous block: the FAT layer reads and writes whole
 * clusters per call, so a file must be read and written in ONE call.
 */
typedef struct _cal_month_ {
	uint16_t year;
	uint8_t month;
	uint8_t dirty;
	uint16_t count;
	uint16_t reserved;
	uint32_t dayMask; /* bit d set: day d has at least one event */
	CalEvtHeader hdr; /* file image starts here ...      */
	CalEvent ev[CALEVT_MAX]; /* ... and runs on through here */
} CalMonth;

#define CALEVT_IMAGE_MAX (sizeof(CalEvtHeader) + sizeof(CalEvent) * CALEVT_MAX)
typedef char _calevt_layout_check[(sizeof(CalEvent) == 44 && sizeof(CalEvtHeader) == 12) ? 1 : -1];

/* ------------------------------------------------------------ dates */

static inline int CalEvtIsLeap(int y) {
	return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
}

static inline int CalEvtDaysIn(int y, int m) {
	static const uint8_t d[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m < 1 || m > 12)
		return 30;
	return (m == 2 && CalEvtIsLeap(y)) ? 29 : d[m];
}

/* 0 = Sunday */
static inline int CalEvtWeekday(int y, int m, int d) {
	static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	if (m < 3)
		y -= 1;
	return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

typedef struct _cal_now_ {
	int year, month, day, hour, minute, weekday;
} CalNow;

/* wall clock in the calendar's time zone (days -> civil date, Hinnant) */
static inline void CalEvtNow(CalNow* n) {
	int64_t sec = 0, nsec = 0;
	_KeGetWalltime(&sec, &nsec);
	sec += CALEVT_TZ;
	int64_t z = sec / 86400;
	int64_t sod = sec % 86400;
	if (sod < 0) {
		sod += 86400;
		z -= 1;
	}
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y = (int64_t)yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	unsigned d = doy - (153 * mp + 2) / 5 + 1;
	unsigned m = mp + (mp < 10 ? 3 : -9);
	n->year = (int)(y + (m <= 2));
	n->month = (int)m;
	n->day = (int)d;
	n->hour = (int)(sod / 3600);
	n->minute = (int)((sod / 60) % 60);
	n->weekday = CalEvtWeekday(n->year, n->month, n->day);
}

/* ------------------------------------------------------------ store */

static inline uint32_t CalEvtKey(int day, int hour, int minute) {
	return ((uint32_t)day << 16) | ((uint32_t)hour << 8) | (uint32_t)minute;
}

static inline uint32_t CalEvtKeyOf(const CalEvent* e) {
	return CalEvtKey(e->day, e->hour, e->minute);
}

/* "/CALEVT/YYYYMM.EVT", out must hold 20 bytes */
static inline void CalEvtPath(char* out, int year, int month) {
	const char* dir = CALEVT_DIR "/";
	int i = 0;
	while (dir[i]) {
		out[i] = dir[i];
		i++;
	}
	out[i++] = (char)('0' + (year / 1000) % 10);
	out[i++] = (char)('0' + (year / 100) % 10);
	out[i++] = (char)('0' + (year / 10) % 10);
	out[i++] = (char)('0' + year % 10);
	out[i++] = (char)('0' + (month / 10) % 10);
	out[i++] = (char)('0' + month % 10);
	out[i++] = '.';
	out[i++] = 'E';
	out[i++] = 'V';
	out[i++] = 'T';
	out[i] = '\0';
}

/* first index whose key >= key, O(log n) */
static inline int CalEvtLowerBound(const CalMonth* cm, uint32_t key) {
	int lo = 0, hi = cm->count;
	while (lo < hi) {
		int mid = (lo + hi) >> 1;
		if (CalEvtKeyOf(&cm->ev[mid]) < key)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* events of one day: returns how many, *first = index of the first */
static inline int CalEvtDayRange(const CalMonth* cm, int day, int* first) {
	int a = CalEvtLowerBound(cm, CalEvtKey(day, 0, 0));
	int b = CalEvtLowerBound(cm, CalEvtKey(day + 1, 0, 0));
	*first = a;
	return b - a;
}

static inline void CalEvtRebuildMask(CalMonth* cm) {
	cm->dayMask = 0;
	for (int i = 0; i < cm->count; i++)
		if (cm->ev[i].day < 32)
			cm->dayMask |= 1u << cm->ev[i].day;
}

/* insertion sort: the stored file is already sorted, so this is O(n)
 * on good files and only repairs a hand-edited one */
static inline void CalEvtSortFix(CalMonth* cm) {
	for (int i = 1; i < cm->count; i++) {
		CalEvent t = cm->ev[i];
		uint32_t k = CalEvtKeyOf(&t);
		int j = i - 1;
		while (j >= 0 && CalEvtKeyOf(&cm->ev[j]) > k) {
			cm->ev[j + 1] = cm->ev[j];
			j--;
		}
		cm->ev[j + 1] = t;
	}
}

/* load a month; a missing or foreign file gives an empty month. 0 = ok */
static inline int CalEvtLoad(CalMonth* cm, int year, int month) {
	cm->year = (uint16_t)year;
	cm->month = (uint8_t)month;
	cm->count = 0;
	cm->dirty = 0;
	cm->dayMask = 0;
	char path[20];
	CalEvtPath(path, year, month);
	int fd = _KeOpenFile(path, FILE_OPEN_READ_ONLY);
	if (fd == -1)
		return 0;
	XEFileStatus st;
	memset(&st, 0, sizeof(st));
	_KeFileStat(fd, &st);
	size_t want = st.size < CALEVT_IMAGE_MAX ? st.size : CALEVT_IMAGE_MAX;
	size_t got = want >= sizeof(CalEvtHeader) ? _KeReadFile(fd, &cm->hdr, want) : 0;
	_KeCloseFile(fd);
	if (got < sizeof(CalEvtHeader) || got > CALEVT_IMAGE_MAX || cm->hdr.magic != CALEVT_MAGIC)
		return -1;
	int n = cm->hdr.count;
	int avail = (int)((got - sizeof(CalEvtHeader)) / sizeof(CalEvent));
	if (n > avail)
		n = avail;
	if (n > CALEVT_MAX)
		n = CALEVT_MAX;
	int ok = 0;
	for (int i = 0; i < n; i++) {
		CalEvent* e = &cm->ev[i];
		if (e->day < 1 || e->day > 31 || e->hour > 23 || e->minute > 59)
			continue;
		e->title[CALEVT_TITLE_MAX - 1] = '\0';
		if (ok != i)
			cm->ev[ok] = *e;
		ok++;
	}
	cm->count = (uint16_t)ok;
	CalEvtSortFix(cm);
	CalEvtRebuildMask(cm);
	return 0;
}

/*
 * write the month back: the folder is created on first use, the old file
 * removed first (the FAT layer does not truncate on rewrite) and the new
 * one written in one call. An empty month just removes its file.
 */
static inline int CalEvtSave(CalMonth* cm) {
	char path[20];
	CalEvtPath(path, cm->year, cm->month);
	_KeCreateDir((char*)CALEVT_DIR);
	_KeRemoveFile(path);
	cm->dirty = 0;
	if (cm->count == 0)
		return 0;
	int fd = _KeOpenFile(path, FILE_OPEN_WRITE | FILE_OPEN_CREAT);
	if (fd == -1)
		return -1;
	memset(&cm->hdr, 0, sizeof(cm->hdr));
	cm->hdr.magic = CALEVT_MAGIC;
	cm->hdr.count = cm->count;
	cm->hdr.year = cm->year;
	cm->hdr.month = cm->month;
	size_t len = sizeof(CalEvtHeader) + (size_t)cm->count * sizeof(CalEvent);
	size_t put = _KeWriteFile(fd, &cm->hdr, len);
	_KeCloseFile(fd);
	return put == len ? 0 : -1;
}

/* insert keeping the order; returns the index or -1 when full */
static inline int CalEvtInsert(CalMonth* cm, const CalEvent* e) {
	if (cm->count >= CALEVT_MAX)
		return -1;
	/* after equal keys, so same-time events keep their entry order */
	int at = CalEvtLowerBound(cm, CalEvtKeyOf(e) + 1);
	memmove(&cm->ev[at + 1], &cm->ev[at], (size_t)(cm->count - at) * sizeof(CalEvent));
	cm->ev[at] = *e;
	cm->ev[at].title[CALEVT_TITLE_MAX - 1] = '\0';
	cm->count++;
	if (e->day < 32)
		cm->dayMask |= 1u << e->day;
	cm->dirty = 1;
	return at;
}

static inline void CalEvtRemove(CalMonth* cm, int idx) {
	if (idx < 0 || idx >= cm->count)
		return;
	int day = cm->ev[idx].day;
	memmove(&cm->ev[idx], &cm->ev[idx + 1], (size_t)(cm->count - idx - 1) * sizeof(CalEvent));
	cm->count--;
	int first;
	if (CalEvtDayRange(cm, day, &first) == 0)
		cm->dayMask &= ~(1u << day);
	cm->dirty = 1;
}

/* "today  18:30", "tomorrow  07:30", "sat 14  10:00": how the launcher's
 * calendar card, LuitFlow and the calendar app all name an event */
static inline void CalEvtWhen(char* out, int daysAhead, int weekday, int day, int hour, int minute) {
	static const char* wd[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
	int i = 0;
	const char* lead = daysAhead == 0 ? "today  " : (daysAhead == 1 ? "tomorrow  " : NULL);
	if (lead) {
		while (*lead)
			out[i++] = *lead++;
	} else {
		const char* w = wd[((weekday % 7) + 7) % 7];
		while (*w)
			out[i++] = *w++;
		out[i++] = ' ';
		if (day >= 10)
			out[i++] = (char)('0' + day / 10);
		out[i++] = (char)('0' + day % 10);
		out[i++] = ' ';
		out[i++] = ' ';
	}
	out[i++] = (char)('0' + hour / 10);
	out[i++] = (char)('0' + hour % 10);
	out[i++] = ':';
	out[i++] = (char)('0' + minute / 10);
	out[i++] = (char)('0' + minute % 10);
	out[i] = '\0';
}

/*
 * CalEvtFindNext -- the first event at or after now: the rest of this
 * month, else the start of the next one (two reads at most, O(log n)
 * each). scratch is clobbered. Returns 1 and fills out / daysAhead.
 */
static inline int CalEvtFindNext(CalMonth* scratch, const CalNow* now, CalEvent* out, int* daysAhead) {
	if (CalEvtLoad(scratch, now->year, now->month) == 0) {
		int i = CalEvtLowerBound(scratch, CalEvtKey(now->day, now->hour, now->minute));
		if (i < scratch->count) {
			*out = scratch->ev[i];
			*daysAhead = out->day - now->day;
			return 1;
		}
	}
	int y = now->year, m = now->month + 1;
	if (m > 12) {
		m = 1;
		y++;
	}
	if (CalEvtLoad(scratch, y, m) == 0 && scratch->count) {
		*out = scratch->ev[0];
		*daysAhead = CalEvtDaysIn(now->year, now->month) - now->day + out->day;
		return 1;
	}
	return 0;
}

/* "07:30" */
static inline void CalEvtFormatTime(char* out, int hour, int minute) {
	out[0] = (char)('0' + hour / 10);
	out[1] = (char)('0' + hour % 10);
	out[2] = ':';
	out[3] = (char)('0' + minute / 10);
	out[4] = (char)('0' + minute % 10);
	out[5] = '\0';
}

#endif
