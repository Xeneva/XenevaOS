/**
 * Port glue for QuickJS on XenevaOS.
 *
 * libc bits quickjs.c needs that XEClib does not provide, following the
 * original os.o semantics (_msize via dlmalloc, real pthread cond vars,
 * UTC local time) but completing them so the port builds from source:
 *  - _msize -> dlmalloc_usable_size (quickjs bookkeeping query)
 *  - clock_gettime for CLOCK_REALTIME/MONOTONIC via kernel wallclock/ms
 *  - a full gmtime/localtime (UTC) so struct tm is deterministic
 *    (XEClib's lacks tm_gmtoff/tm_zone -> quickjs Date would read slack)
 */

#include <time.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <_xeneva.h>
#include <sys/_ketime.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t dlmalloc_usable_size(void* ptr);

size_t _msize(void* ptr) {
	return dlmalloc_usable_size(ptr);
}

/* quickjs.c reaches malloc_usable_size() on non-Windows builds; route it
 * through the dlmalloc in-use size so js_def_* bookkeeping stays exact. */
size_t malloc_usable_size(void* ptr) {
	return dlmalloc_usable_size(ptr);
}

/* ------------------------------------------------------------------ */
/* clocks                                                              */
/* ------------------------------------------------------------------ */

int clock_gettime(int clock_id, struct timespec* ts) {
	if (!ts)
		return -1;
	switch (clock_id) {
#ifdef CLOCK_REALTIME
	case CLOCK_REALTIME: {
		int64_t sec = 0;
		int64_t nsec = 0;
		_KeGetWalltime(&sec, &nsec);
		ts->tv_sec = (time_t)sec;
		ts->tv_nsec = (long)nsec;
		return 0;
	}
#endif
	default: {
		uint64_t ms = _KeGetCurrentMS();
		ts->tv_sec = (time_t)(ms / 1000);
		ts->tv_nsec = (long)((ms % 1000) * 1000000ULL);
		return 0;
	}
	}
}

/* ------------------------------------------------------------------ */
/* calendar (UTC zone everywhere on Xeneva)                            */
/* ------------------------------------------------------------------ */

static const char kZoneName[4] = "UTC";

static int gm_is_leap(int y) {
	return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static struct tm* gmtime_fill(const time_t* t, struct tm* out) {
	if (!out)
		return NULL;
	memset(out, 0, sizeof(*out));
	if (!t)
		return NULL;
	long long days = (long long)(*t / 86400);
	long long rem = (long long)(*t % 86400);
	if (rem < 0) {
		rem += 86400;
		days--;
	}
	out->tm_hour = (int)(rem / 3600);
	out->tm_min = (int)((rem % 3600) / 60);
	out->tm_sec = (int)(rem % 60);

	/* 1970-01-01 was a Thursday (wday 4). */
	int wday = (int)((days + 4) % 7);
	if (wday < 0)
		wday += 7;
	out->tm_wday = wday;

	/* Civil date from days since 1970-01-01. */
	long long z = days + 719468;
	long long era = (z >= 0 ? z : z - 146096) / 146097;
	long long doe = z - era * 146097;
	long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long long y = yoe + era * 400;
	long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long long mp = (5 * doy + 2) / 153;
	long long d = doy - (153 * mp + 2) / 5 + 1;
	long long m = mp < 10 ? mp + 3 : mp - 9;
	if (m <= 2)
		y++;
	out->tm_year = (int)(y - 1900);
	out->tm_mon = (int)(m - 1);
	out->tm_mday = (int)d;
	{
		/* day-of-year: days since Jan 1 */
		static const int cum[11] = {31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
		int idx = out->tm_mon - 1;
		int cumd = (idx >= 0 && idx < 11) ? cum[idx] : 0;
		if (idx >= 0 && idx >= 1 && gm_is_leap((int)y))
			cumd++;
		out->tm_yday = cumd + out->tm_mday - 1;
	}
	out->tm_isdst = 0;
	out->tm_gmtoff = 0;
	out->tm_zone = kZoneName;
	return out;
}

static struct tm s_gmtimeBuf;
static struct tm s_localBuf;

struct tm* gmtime(const time_t* t) {
	return gmtime_fill(t, &s_gmtimeBuf);
}

struct tm* gmtime_r(const time_t* t, struct tm* out) {
	return gmtime_fill(t, out);
}

struct tm* localtime(const time_t* t) {
	/* Distinct static so interleaved gmtime/localtime don't alias. */
	return gmtime_fill(t, &s_localBuf);
}

struct tm* localtime_r(const time_t* t, struct tm* out) {
	return gmtime_fill(t, out);
}

time_t time(time_t* t) {
	int64_t sec = 0;
	int64_t nsec = 0;
	_KeGetWalltime(&sec, &nsec);
	if (t)
		*t = (time_t)sec;
	return (time_t)sec;
}

double difftime(time_t a, time_t b) {
	return (double)((long long)a - (long long)b);
}

#ifdef __cplusplus
}
#endif
