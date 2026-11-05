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

#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include <stdint.h>
#include <aurora.h>

#pragma pack(push,1)
typedef struct _spinlock_ {
	size_t value;
}Spinlock;
#pragma pack(pop)

/*
* AuCreateSpinlock -- creates a new spinlock and return
* @param early -- specifies if it's early lock
*/
AU_EXTERN AU_EXPORT Spinlock* AuCreateSpinlock(bool early);

/*
* AuDeleteSpinlock -- deletes only non-early spinlocks
*/
AU_EXTERN AU_EXPORT void AuDeleteSpinlock(Spinlock* lock);


/*
* AuAcquireSpinlock -- acuires a lock
* @param lock -- pointer to spinlock
*/
AU_EXTERN AU_EXPORT void AuAcquireSpinlock(Spinlock* lock);

/*
* AuReleaseSpinlock -- releases a spinlock
* @param lock -- pointer to spinlock
*/
AU_EXTERN AU_EXPORT void AuReleaseSpinlock(Spinlock* lock);

#ifdef ARCH_ARM64
/*
 * IRQ-safe spinlock helpers. The saved DAIF value must be restored by the
 * matching release call on the same CPU.
 */
AU_EXTERN AU_EXPORT void AuAcquireSpinlockIrqSave(Spinlock* lock, uint64_t* daif);
AU_EXTERN AU_EXPORT void AuReleaseSpinlockIrqRestore(Spinlock* lock, uint64_t daif);
#endif

#endif