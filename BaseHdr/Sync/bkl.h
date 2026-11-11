/**
* @file bkl.h
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
**/

#ifndef __AU_BKL_H__
#define __AU_BKL_H__

#include <stdint.h>
#include <aurora.h>
#include <stdbool.h>

/*
 * Big kernel lock (AArch64 SMP).
 *
 * Most of the kernel was written for one core: it protects its data by
 * masking IRQs, or not at all. The BKL keeps that world intact on SMP:
 * user mode runs on every core in parallel, but only one core at a time
 * executes kernel code (syscalls, exceptions from EL0, kernel threads,
 * device interrupt work).
 *
 * - Owned by a core and recursive on it, so an interrupt that lands on
 *   the owning core nests exactly like it did on one core.
 * - IRQs are masked only while spinning for it, not while holding it:
 *   a kernel thread holding it stays preemptible by the timer.
 * - The scheduler hands it over: a thread switched out keeps the depth
 *   it held in AA64Thread::bkl_depth and gets it back when it resumes
 *   inside the kernel (Hal/sched.c).
 * - Lock order: BKL before the scheduler lock, never the other way.
 *   Never spin on another core's progress while holding it.
 */

/** take the BKL (recursive on the owning core) */
extern void AuBKLLock(void);

/** drop one level of the BKL, no-op if this core does not own it */
extern void AuBKLUnlock(void);

/** drop the BKL completely, returns the depth this core held (0 if none).
 * Kernel threads that only touch their own state (driver polling threads)
 * call it before waiting on hardware or sleeping: the scheduler hands a
 * kernel thread one level each time it resumes it */
AU_EXTERN AU_EXPORT uint32_t AuBKLReleaseAll(void);

/** take the BKL with a given depth (0 does nothing) */
extern void AuBKLAcquireDepth(uint32_t depth);

/** true if the calling core owns the BKL */
AU_EXTERN AU_EXPORT bool AuBKLHeldByMe(void);

/** take the BKL only if that needs no waiting (recursive on the owner).
 * Interrupt handlers use this: they never spin on the BKL */
extern bool AuBKLTryLock(void);

/** logical cpu that owns the BKL right now, -1 if free (a hint) */
extern int AuBKLOwnerCPU(void);

#endif
