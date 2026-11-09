/**
* @file ticketlock.h
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
**/

#ifndef __AU_TICKETLOCK_H__
#define __AU_TICKETLOCK_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * FIFO ticket spinlock (AArch64). Waiters are served strictly in arrival
 * order, so a core that releases and immediately re-takes the lock (a
 * busy kernel thread re-picked on its own core) cannot starve the others,
 * which a plain test-and-set lock allows.
 *
 * Callers must keep IRQs masked while waiting for and holding it: a
 * ticket holder that gets preempted stalls every core queued behind it.
 */
typedef union {
	volatile uint32_t word;
	struct {
		volatile uint16_t serving; /* ticket currently allowed in */
		volatile uint16_t next;	   /* next ticket to hand out */
	} t;
} AuTicketLock;

static inline void AuTicketLockAcquire(AuTicketLock* l) {
	uint16_t my = __atomic_fetch_add(&l->t.next, (uint16_t)1, __ATOMIC_RELAXED);
	while (__atomic_load_n(&l->t.serving, __ATOMIC_ACQUIRE) != my)
		__asm__ volatile("wfe" ::: "memory");
}

static inline bool AuTicketLockTryAcquire(AuTicketLock* l) {
	uint32_t v = __atomic_load_n(&l->word, __ATOMIC_RELAXED);
	uint16_t serving = (uint16_t)(v & 0xFFFF);
	uint16_t next = (uint16_t)(v >> 16);
	if (serving != next)
		return false;
	uint32_t nv = ((uint32_t)(uint16_t)(next + 1) << 16) | serving;
	return __atomic_compare_exchange_n(&l->word, &v, nv, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static inline void AuTicketLockRelease(AuTicketLock* l) {
	uint16_t s = l->t.serving; /* only the holder writes serving */
	__atomic_store_n(&l->t.serving, (uint16_t)(s + 1), __ATOMIC_RELEASE);
	/* wake waiters parked in wfe */
	__asm__ volatile("dsb ishst\n sev" ::: "memory");
}

#endif
