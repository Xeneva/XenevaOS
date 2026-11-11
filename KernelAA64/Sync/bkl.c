/**
* @file bkl.c
*
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

#include <stdint.h>
#include <stdbool.h>
#include <Sync/bkl.h>
#include <Sync/ticketlock.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64percpu.h>

/* see BaseHdr/Sync/bkl.h for the rules */

extern void aa64_spinlock_acquire(uint32_t* lock);
extern void aa64_spinlock_release(uint32_t* lock);
extern int aa64_spinlock_try_acquire(uint32_t* lock);

/* FIFO: a busy kernel thread re-taking it cannot starve syscalls */
static AuTicketLock s_bkl_word __attribute__((aligned(8)));
/* owning cpu_num + 1, 0 when free. Only the owner writes its own id and
 * it clears it before releasing, so reading our own id means we own it */
static volatile uint32_t s_bkl_owner;
static volatile uint32_t s_bkl_depth;

static inline uint32_t bklMe(void) {
	return AA64GetCPUNum() + 1;
}

/*
 * Stall tracing (Raspberry Pi 3 build): every process needs the BKL for
 * its syscalls, so one long hold freezes the whole UI. Holds and waits
 * longer than BKL_TRACE_MS are logged with the holder, at most one line
 * per BKL_TRACE_GAP_MS.
 */
#ifdef __TARGET_BOARD_RPI3__
#define BKL_TRACE 1
#endif

#ifdef BKL_TRACE
#include <Hal/AA64/sched.h>
#include <Drivers/uart.h>

#define BKL_TRACE_MS	 10
#define BKL_TRACE_GAP_MS 500

static volatile uint64_t s_bkl_since;	/* counter value when taken */
static volatile uint64_t s_bkl_last_log;

static inline uint64_t bklNow(void) {
	return get_cntpct_el0();
}

static inline uint64_t bklMs(uint64_t ticks) {
	return (ticks * 1000ULL) / get_cntfrq_el0();
}

/* thread names are char[8] and need not be terminated */
static void bklThreadName(char out[9]) {
	AA64Thread* t = AuGetCurrentThread();
	int i = 0;
	if (t)
		for (; i < 8 && t->name[i]; i++)
			out[i] = t->name[i];
	if (i == 0) {
		out[0] = '-';
		i = 1;
	}
	out[i] = 0;
}

static bool bklMayLog(uint64_t now) {
	if (s_bkl_last_log && bklMs(now - s_bkl_last_log) < BKL_TRACE_GAP_MS)
		return false;
	s_bkl_last_log = now;
	return true;
}

/* the lock was just taken; waited = counter ticks spent waiting */
static void bklTraceTaken(uint64_t waited) {
	uint64_t now = bklNow();
	s_bkl_since = now;
	uint64_t ms = bklMs(waited);
	if (ms >= BKL_TRACE_MS && bklMayLog(now)) {
		char name[9];
		bklThreadName(name);
		UARTDebugOut("[stall]: cpu %d (%s) waited %d ms for the BKL \r\n",
					 (uint64_t)AA64GetCPUNum(), name, ms);
	}
}

/* about to be released by its owner */
static void bklTraceReleased(void) {
	uint64_t now = bklNow();
	uint64_t ms = bklMs(now - s_bkl_since);
	if (ms >= BKL_TRACE_MS && bklMayLog(now)) {
		char name[9];
		bklThreadName(name);
		UARTDebugOut("[stall]: cpu %d (%s) held the BKL for %d ms \r\n",
					 (uint64_t)AA64GetCPUNum(), name, ms);
	}
}
#else
static inline uint64_t bklNow(void) {
	return 0;
}
static inline void bklTraceTaken(uint64_t waited) {
	(void)waited;
}
static inline void bklTraceReleased(void) {}
#endif

void AuBKLLock(void) {
	uint64_t d = read_daif();
	mask_irqs();
	uint32_t me = bklMe();
	if (s_bkl_owner == me) {
		s_bkl_depth++;
	} else {
		uint64_t t0 = bklNow();
		AuTicketLockAcquire(&s_bkl_word);
		s_bkl_owner = me;
		s_bkl_depth = 1;
		bklTraceTaken(bklNow() - t0);
	}
	restore_daif(d);
}

void AuBKLUnlock(void) {
	uint64_t d = read_daif();
	mask_irqs();
	if (s_bkl_owner == bklMe() && s_bkl_depth) {
		if (--s_bkl_depth == 0) {
			bklTraceReleased();
			s_bkl_owner = 0;
			AuTicketLockRelease(&s_bkl_word);
		}
	}
	restore_daif(d);
}

uint32_t AuBKLReleaseAll(void) {
	uint64_t d = read_daif();
	mask_irqs();
	uint32_t depth = 0;
	if (s_bkl_owner == bklMe()) {
		depth = s_bkl_depth;
		bklTraceReleased();
		s_bkl_depth = 0;
		s_bkl_owner = 0;
		AuTicketLockRelease(&s_bkl_word);
	}
	restore_daif(d);
	return depth;
}

void AuBKLAcquireDepth(uint32_t depth) {
	if (depth == 0)
		return;
	uint64_t d = read_daif();
	mask_irqs();
	uint32_t me = bklMe();
	if (s_bkl_owner == me) {
		s_bkl_depth += depth;
	} else {
		uint64_t t0 = bklNow();
		AuTicketLockAcquire(&s_bkl_word);
		s_bkl_owner = me;
		s_bkl_depth = depth;
		bklTraceTaken(bklNow() - t0);
	}
	restore_daif(d);
}

bool AuBKLHeldByMe(void) {
	return s_bkl_owner == bklMe();
}

bool AuBKLTryLock(void) {
	uint64_t d = read_daif();
	mask_irqs();
	uint32_t me = bklMe();
	bool ok = false;
	if (s_bkl_owner == me) {
		s_bkl_depth++;
		ok = true;
	} else if (AuTicketLockTryAcquire(&s_bkl_word)) {
		s_bkl_owner = me;
		s_bkl_depth = 1;
		bklTraceTaken(0);
		ok = true;
	}
	restore_daif(d);
	return ok;
}

int AuBKLOwnerCPU(void) {
	uint32_t owner = s_bkl_owner;
	return owner ? (int)owner - 1 : -1;
}
