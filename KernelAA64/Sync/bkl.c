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

void AuBKLLock(void) {
	uint64_t d = read_daif();
	mask_irqs();
	uint32_t me = bklMe();
	if (s_bkl_owner == me) {
		s_bkl_depth++;
	} else {
		AuTicketLockAcquire(&s_bkl_word);
		s_bkl_owner = me;
		s_bkl_depth = 1;
	}
	restore_daif(d);
}

void AuBKLUnlock(void) {
	uint64_t d = read_daif();
	mask_irqs();
	if (s_bkl_owner == bklMe() && s_bkl_depth) {
		if (--s_bkl_depth == 0) {
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
		AuTicketLockAcquire(&s_bkl_word);
		s_bkl_owner = me;
		s_bkl_depth = depth;
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
		ok = true;
	}
	restore_daif(d);
	return ok;
}

int AuBKLOwnerCPU(void) {
	uint32_t owner = s_bkl_owner;
	return owner ? (int)owner - 1 : -1;
}
