/**
 * BSD 2-Clause License
 *
 * Copyright (c) 2022-2023, Manas Kamal Choudhury
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <Sync/spinlock.h>
#include <_null.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/kmalloc.h>
#if defined(__GNUC__) || defined(__clang__)
#include <stdbool.h>
#endif
//it is three am, please just work, ill expand this to be reentrant and SMP safe later --axiss
//needs more stress testing, but it booted, 3:42am
/* Low-level AArch64 assembly primitives */
extern void aa64_spinlock_acquire(uint32_t* lock);
extern void aa64_spinlock_release(uint32_t* lock);

static Spinlock early_spin[8];
static uint8_t early_spinlock_cnt = 0;

/*
 * AuCreateSpinlock -- creates a new spinlock
 * @param early -- if true, use a static early spinlock (no kmalloc dependency)
 */
AU_EXTERN AU_EXPORT Spinlock* AuCreateSpinlock(bool early) {
	Spinlock* spinlock = NULL;
	if (early && early_spinlock_cnt < 8) {
		early_spin[early_spinlock_cnt].value = 0;
		return &early_spin[early_spinlock_cnt++];
	}else {
		spinlock = (Spinlock*)kmalloc(sizeof(Spinlock));
		if (!spinlock)
			return NULL;
		spinlock->value = 0;
	}
	/* this used to return NULL, which turned every heap allocated lock
	 * into a no-op (Acquire/Release ignore NULL) */
	return spinlock;
}

/*
 * AuDeleteSpinlock -- frees a heap allocated spinlock, no-op for early
 * spinlocks (they live in early_spin[])
 */
AU_EXTERN AU_EXPORT void AuDeleteSpinlock(Spinlock* lock) {
	if (!lock)
		return;
	if (lock >= &early_spin[0] && lock < &early_spin[8])
		return;
	kfree(lock);
}

/*
 * AuAcquireSpinlock -- acquire spinlock via AArch64 WFE-based primitive
 */
AU_EXTERN AU_EXPORT void AuAcquireSpinlock(Spinlock* lock) {
	if (!lock)
	   return;
	aa64_spinlock_acquire((uint32_t*)&lock->value);
}

/*
 * AuReleaseSpinlock -- release spinlock via AArch64 STLR
 */
AU_EXTERN AU_EXPORT void AuReleaseSpinlock(Spinlock* lock) {
	if (!lock)
	   return;
	aa64_spinlock_release((uint32_t*)&lock->value);
}

/*
 * AuAcquireSpinlockIrqSave -- mask IRQs on this core, then acquire
 */
AU_EXTERN AU_EXPORT uint64_t AuAcquireSpinlockIrqSave(Spinlock* lock) {
	uint64_t daif = read_daif();
	mask_irqs();
	if (lock)
		aa64_spinlock_acquire((uint32_t*)&lock->value);
	return daif;
}

/*
 * AuReleaseSpinlockIrqRestore -- release, then restore the saved DAIF
 */
AU_EXTERN AU_EXPORT void AuReleaseSpinlockIrqRestore(Spinlock* lock, uint64_t daif) {
	if (lock)
		aa64_spinlock_release((uint32_t*)&lock->value);
	restore_daif(daif);
}
