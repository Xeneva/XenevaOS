/*
 * DCL rtmutex.h -- opaque.
 *
 * linux/ww_mutex.h includes this for struct rt_mutex, but only its
 * CONFIG_PREEMPT_RT branch names the type; this build is not PREEMPT_RT
 * (WW_MUTEX_BASE is plain mutex), so nothing beyond the name is needed.
 */
#ifndef _LINUX_RTMUTEX_H
#define _LINUX_RTMUTEX_H

struct rt_mutex;

#endif /* _LINUX_RTMUTEX_H */
