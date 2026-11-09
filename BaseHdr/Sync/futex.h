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

#ifndef __SYNC_FUTEX_H__
#define __SYNC_FUTEX_H__

#include <stdint.h>

/* These match the Linux futex command encoding so the Linux ELF ABI path and
 * the native PE path share one implementation. */
#define AU_FUTEX_WAIT			0
#define AU_FUTEX_WAKE			1
#define AU_FUTEX_REQUEUE		3
#define AU_FUTEX_CMP_REQUEUE	4
#define AU_FUTEX_WAIT_BITSET	9
#define AU_FUTEX_WAKE_BITSET	10
#define AU_FUTEX_CMD_MASK		127
#define AU_FUTEX_PRIVATE_FLAG	128
#define AU_FUTEX_CLOCK_REALTIME	256

#define AU_FUTEX_EAGAIN		11
#define AU_FUTEX_EINVAL		22
#define AU_FUTEX_ENOSYS		38
#define AU_FUTEX_ETIMEDOUT	116

#ifdef __KERNEL__

/**
 * @brief AuFutexWait -- sleep until uaddr changes or the timeout expires
 *
 * The timeout is a relative timespec as two int64_t fields (seconds,
 * nanoseconds), matching FUTEX_WAIT.  On success the caller resumes directly
 * in user mode with 0 in x0/x6; this function only returns for immediate
 * failures (EAGAIN/EINVAL).
 */
extern int AuFutexWait(AuProcess* proc, AA64Thread* thr, uint32_t* uaddr,
					   uint32_t val, uint32_t bitset, int timed,
					   uint64_t timeout_ms);

/**
 * @brief AuFutexWake -- wake up to nwake waiters matching uaddr/bitset
 */
extern int AuFutexWake(AuProcess* proc, uint32_t* uaddr, int nwake,
					   uint32_t bitset);

/**
 * @brief AuFutexRequeue -- wake waiters, then move the rest to uaddr2
 */
extern int AuFutexRequeue(AuProcess* proc, uint32_t* uaddr, int nwake,
						  int nrequeue, uint32_t* uaddr2,
						  uint32_t cmpval, int has_cmp);

/**
 * @brief AuFutexSyscall -- native SYS_FUTEX entry point
 */
extern int64_t AuFutexSyscall(int64_t uaddr, int64_t op, int64_t val,
							  int64_t timeout, int64_t uaddr2,
							  int64_t val3);

#endif /* __KERNEL__ */

#endif
