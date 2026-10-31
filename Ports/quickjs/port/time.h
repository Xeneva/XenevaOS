/**
 * time.h shim for the QuickJS port on XenevaOS.
 *
 * XEClib's real time.h skips struct tags (typedef-only), lacks tm_gmtoff /
 * tm_zone, and declares gettimeofday as (timeval*). quickjs.c needs
 * classic <time.h> interfaces, so the QuickJS port build resolves <time.h>
 * to this header. localtime/gmtime are implemented in port/port.c; the
 * rest stay XEClib's.
 */

#ifndef NS_QJS_TIME_H
#define NS_QJS_TIME_H

#ifndef NS_QJS_STDDEF_READY
#include <stddef.h>
#endif
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOCKS_PER_SEC 1000L

typedef long clock_t;
typedef long time_t;

struct tm {
	int tm_sec;	  /* seconds [0-59] */
	int tm_min;	  /* minutes [0-59] */
	int tm_hour;  /* hours [0-23] */
	int tm_mday;  /* day of month [1-31] */
	int tm_mon;	  /* month [0-11] */
	int tm_year;  /* years since 1900 */
	int tm_wday;  /* weekday [0=Sunday] */
	int tm_yday;  /* day of year [0-365] */
	int tm_isdst; /* DST flag */
	long tm_gmtoff;
	const char* tm_zone;
};

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

struct timeval {
	long tv_sec;
	long tv_usec;
};

/* XEClib's sys/time.h refers to the bare typedef name. */
typedef struct timeval timeval;

/* Clock ids (also mirrored in port/pthread.h). */
#define CLOCK_REALTIME	 0
#define CLOCK_MONOTONIC	 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID	 3

struct tm* gmtime(const time_t* t);
struct tm* localtime(const time_t* t);
struct tm* gmtime_r(const time_t* t, struct tm* out);
struct tm* localtime_r(const time_t* t, struct tm* out);
time_t mktime(struct tm* tm);
time_t time(time_t* t);
char* ctime(const time_t* t);
clock_t clock(void);
double difftime(time_t a, time_t b);
int gettimeofday(struct timeval* tv, void* tz);
int nanosleep(const struct timespec* req, struct timespec* rem);

/* fmt: since XCC's own time.h passes `timeval*` overloads to the same
 * implementation; Xeneva builds use the portable signature above. */

#ifdef __cplusplus
}
#endif

#endif
