/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2026, XenevaOS contributors
 * All rights reserved.
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
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <Sync/spinlock.h>
#include <Mm/kmalloc.h>
#include <stdint.h>
#include <_null.h>

#define AA64_EARLY_SPINLOCK_COUNT 8

static Spinlock early_spinlocks[AA64_EARLY_SPINLOCK_COUNT];
static Spinlock early_spinlock_guard;
static uint32_t early_spinlock_count;

static void aa64_spinlock_acquire_raw(Spinlock* lock) {
    uint64_t value;
    uint32_t status;
    const uint64_t locked = 1;

    __asm__ volatile(
        "sevl\n"
        "1: wfe\n"
        "2: ldaxr %0, [%2]\n"
        "cbnz %0, 1b\n"
        "stxr %w1, %3, [%2]\n"
        "cbnz %w1, 2b\n"
        : "=&r"(value), "=&r"(status)
        : "r"(&lock->value), "r"(locked)
        : "memory");
}

static void aa64_spinlock_release_raw(Spinlock* lock) {
    __asm__ volatile(
        "stlr xzr, [%0]\n"
        "sev\n"
        :
        : "r"(&lock->value)
        : "memory");
}

AU_EXTERN AU_EXPORT Spinlock* AuCreateSpinlock(bool early) {
    Spinlock* lock;

    if (early) {
        uint64_t daif;
        AuAcquireSpinlockIrqSave(&early_spinlock_guard, &daif);

        if (early_spinlock_count >= AA64_EARLY_SPINLOCK_COUNT) {
            AuReleaseSpinlockIrqRestore(&early_spinlock_guard, daif);
            return NULL;
        }

        lock = &early_spinlocks[early_spinlock_count++];
        lock->value = 0;
        AuReleaseSpinlockIrqRestore(&early_spinlock_guard, daif);
        return lock;
    }

    lock = (Spinlock*)kmalloc(sizeof(Spinlock));
    if (lock)
        lock->value = 0;

    return lock;
}

AU_EXTERN AU_EXPORT void AuDeleteSpinlock(Spinlock* lock) {
    uintptr_t address;
    uintptr_t first;
    uintptr_t end;

    if (!lock)
        return;

    address = (uintptr_t)lock;
    first = (uintptr_t)&early_spinlocks[0];
    end = (uintptr_t)&early_spinlocks[AA64_EARLY_SPINLOCK_COUNT];

    if (address >= first && address < end)
        return;

    kfree(lock);
}

AU_EXTERN AU_EXPORT void AuAcquireSpinlock(Spinlock* lock) {
    if (lock)
        aa64_spinlock_acquire_raw(lock);
}

AU_EXTERN AU_EXPORT void AuReleaseSpinlock(Spinlock* lock) {
    if (lock)
        aa64_spinlock_release_raw(lock);
}

AU_EXTERN AU_EXPORT void AuAcquireSpinlockIrqSave(Spinlock* lock, uint64_t* daif) {
    if (!lock || !daif)
        return;

    __asm__ volatile(
        "mrs %0, daif\n"
        "msr daifset, #2\n"
        "isb\n"
        : "=r"(*daif)
        :
        : "memory");

    aa64_spinlock_acquire_raw(lock);
}

AU_EXTERN AU_EXPORT void AuReleaseSpinlockIrqRestore(Spinlock* lock, uint64_t daif) {
    if (!lock)
        return;

    aa64_spinlock_release_raw(lock);

    __asm__ volatile(
        "msr daif, %0\n"
        "isb\n"
        :
        : "r"(daif)
        : "memory");
}
