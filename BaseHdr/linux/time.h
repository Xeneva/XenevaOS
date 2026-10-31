#ifndef __LINUX_TIME_H__
#define __LINUX_TIME_H__

/*
 * DCL <linux/time.h> -- the POSIX clock IDs.
 *
 * mainline keeps these in uapi/linux/time.h and reaches them through
 * linux/time.h; DCL's tree is flat with no uapi/ layer, so this header is the
 * uapi block and nothing else.  Copied verbatim (upstream path and tag below)
 * because every value is an ABI constant: CLOCK_MONOTONIC is 1 because that
 * is what userspace passes to clock_gettime(), and hrtimer_setup() records it
 * in the timer's mode word.
 *
 *   upstream  include/uapi/linux/time.h  (mainline v7.2)
 *
 * Not here yet: struct timespec/timeval/itimerspec, which uapi/linux/time.h
 * also defines.  Nothing in stages 3-7 names them, so they are left to the
 * first caller rather than transcribed on spec -- see the rule at the top of
 * linux/cleanup.h about not carrying a speculative surface.
 */

/*
 * The IDs of the various system clocks (for POSIX.1b interval timers):
 */
#define CLOCK_REALTIME			0
#define CLOCK_MONOTONIC			1
#define CLOCK_PROCESS_CPUTIME_ID	2
#define CLOCK_THREAD_CPUTIME_ID		3
#define CLOCK_MONOTONIC_RAW		4
#define CLOCK_REALTIME_COARSE		5
#define CLOCK_MONOTONIC_COARSE		6
#define CLOCK_BOOTTIME			7
#define CLOCK_REALTIME_ALARM		8
#define CLOCK_BOOTTIME_ALARM		9
/*
 * The driver implementing this got removed. The clock ID is kept as a
 * place holder. Do not reuse!
 */
#define CLOCK_SGI_CYCLE			10
#define CLOCK_TAI			11

#endif /* __LINUX_TIME_H__ */
