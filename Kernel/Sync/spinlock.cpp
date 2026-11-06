/**
* BSD 2-Clause License
*
* Copyright (c) 2022 - 2023, Manas Kamal Choudhury
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

#include <Sync/spinlock.h>
#include <Mm/kmalloc.h>
#include <stdint.h>
#include <Hal/serial.h>
#include <_null.h>
#include <Hal/x86_64_lowlevel.h>

Spinlock  early_spin[8];
static uint8_t early_spinlock_cnt = 0;

/*
 * AuCreateSpinlock -- creates a new spinlock and return
 * @param early -- specifies if it's early lock
 */
AU_EXTERN AU_EXPORT Spinlock* AuCreateSpinlock(bool early) {
	Spinlock* spinlock = NULL;
	if (early) {
		spinlock = &early_spin[early_spinlock_cnt];
		spinlock->value = 0;
		early_spinlock_cnt++;
	}
	else {
		spinlock = (Spinlock*)kmalloc(sizeof(Spinlock));
		spinlock->value = 0;
	}

	return spinlock;
}

/*
 * AuDeleteSpinlock -- deletes only non-early spinlocks
 */
AU_EXTERN AU_EXPORT void AuDeleteSpinlock(Spinlock* lock) {
	kfree(lock);
}

/*
 * AuAcquireSpinlock -- acuires a lock
 * @param lock -- pointer to spinlock
 */
AU_EXTERN AU_EXPORT void AuAcquireSpinlock(Spinlock* lock) {
	if (lock->value > 1)  {
		SeTextOut("[aurora kernel]: lock -> %x is corrupted \r\n", lock);
		lock->value = 0;
	}
	do {	
		if (x64_lock_test(&lock->value, 0, 1)) {
			break;
		}
		x64_pause();
		volatile size_t* lockd = &lock->value;
		while (*lockd == 1);
	} while (1);
}

/*
 * AuReleaseSpinlock -- releases a spinlock
 * @param lock -- pointer to spinlock
 */
AU_EXTERN AU_EXPORT void AuReleaseSpinlock(Spinlock* lock) {
	if (!x64_lock_test(&lock->value, 1, 0))
		lock->value = 0;
}