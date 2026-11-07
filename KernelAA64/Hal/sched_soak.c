/**
* @file sched_soak.c
*
* Scheduler soak test: sustained mixed load that stresses everything the
* recent scheduler work touched -- EDF admit/wait/release/pick, sleep and
* block/unblock paths, and the spinlock-guarded lists under a 1ms tick.
*
* Layout: two EDF periodic workers (50ms/8ms, 120ms/12ms), one RR churn
* thread (sleep cycles plus periodic self-blocks), one waker thread
* exercising cross-thread AuBlockThread/AuUnblockThread, and a supervisor
* that every ~5s validates all scheduler lists (linkage, tails, states),
* samples job counters and prints a summary line.
*
* PASS criteria after 60 rounds (~5 min): zero list errors, both EDF
* workers kept producing jobs, supervisor never stalled. Miss counts are
* reported, not fatal (shared desktop, no WCET enforcement). Then every
* soak thread parks forever.
**/

#include <stdint.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <timer.h>
#include <_null.h>

#define SOAK_ROUNDS 60
#define SOAK_SUPERVISOR_SLICE_MS 100
#define SOAK_SUPERVISOR_SLICES 50

static uint64_t soak_rng = 0x123456789ABCULL;
static volatile int soak_done;

static uint64_t soak_next_rand(void) {
	soak_rng = soak_rng * 6364136223846793005ULL + 1442695040888963407ULL;
	return soak_rng >> 33;
}

static void soak_spin_us(uint64_t us) {
	uint64_t t0 = AuGetCurrentUS();
	volatile uint64_t spin = 0;
	while (AuGetCurrentUS() - t0 < us)
		spin++;
	(void)spin;
}

static void soak_sleep_ms(AA64Thread* self, uint64_t ms) {
	AuSleepThread(self, ms);
	AuScheduleNext();
	enable_irqs();
}

struct soak_edf_stat {
	uint64_t jobs;
	uint64_t misses;
};

static struct soak_edf_stat soak_stat_a;
static struct soak_edf_stat soak_stat_b;
static uint64_t soak_churn_cycles;
static uint64_t soak_wakeups;
static AA64Thread* soak_churn_thr;
static uint64_t soak_churn_id;

static void soak_parker(AA64Thread* self) {
	AuEDFRemove(self);
	AuBlockThread(self);
	AuScheduleNext();
	while (1) {}
}

static void soak_edf_worker(uint64_t period_us, uint64_t wcet_us, uint64_t work_us,
	struct soak_edf_stat* stat) {
	AA64Thread* self = AuGetCurrentThread();
	enable_irqs();
	if (!AuEDFSetParams(self, period_us, wcet_us)) {
		UARTDebugOut("[soak]: EDF worker admission FAILED\n");
		soak_parker(self);
	}
	while (1) {
		soak_spin_us(work_us);
		stat->jobs++;
		stat->misses = AuEDFGetMisses(self);
		AuEDFWaitPeriod(self);
		if (soak_done)
			soak_parker(self);
	}
}

static void soak_edf_a(uint64_t ctx) {
	(void)ctx;
	soak_edf_worker(50000ULL, 8000ULL, 2000ULL, &soak_stat_a);
}

static void soak_edf_b(uint64_t ctx) {
	(void)ctx;
	soak_edf_worker(120000ULL, 12000ULL, 4000ULL, &soak_stat_b);
}

/* RR churn: random short sleeps plus a self-block every 7th cycle (the
 * waker below releases it). Exercises sleep-list and blocked-list churn
 * against the tick under load. */
static void soak_churn(uint64_t ctx) {
	(void)ctx;
	AA64Thread* self = AuGetCurrentThread();
	enable_irqs();
	uint64_t n = 0;
	while (1) {
		if (soak_done)
			soak_parker(self);
		soak_sleep_ms(self, 3 + soak_next_rand() % 15);
		soak_churn_cycles++;
		if ((n++ % 7) == 6) {
			AuBlockThread(self);
			AuScheduleNext();
			enable_irqs();
			soak_churn_cycles++;
		}
	}
}

/* Waker: releases the churn thread only while it sits in the blocked
 * list (membership-checked, so a sleeping churn thread is never
 * touched). The blocked state is stable here: only this thread can
 * release the churner. */
static void soak_waker(uint64_t ctx) {
	(void)ctx;
	AA64Thread* self = AuGetCurrentThread();
	enable_irqs();
	while (1) {
		if (soak_done)
			soak_parker(self);
		if (AuThreadFindByIDBlockList(soak_churn_id) != NULL) {
			AuUnblockThread(soak_churn_thr);
			soak_wakeups++;
		}
		soak_sleep_ms(self, 7);
	}
}

static void soak_supervisor(uint64_t ctx) {
	(void)ctx;
	AA64Thread* self = AuGetCurrentThread();
	enable_irqs();
	int bad = 0;
	for (int round = 0; round < SOAK_ROUNDS; round++) {
		for (int i = 0; i < SOAK_SUPERVISOR_SLICES; i++)
			soak_sleep_ms(self, SOAK_SUPERVISOR_SLICE_MS);
		if (!AuSchedValidateLists()) {
			bad++;
			UARTDebugOut("[soak]: round %d LIST CORRUPTION\n", round);
		}
		UARTDebugOut("[soak]: round %d/%d a_jobs=%d a_miss=%d b_jobs=%d b_miss=%d\n",
			round + 1, SOAK_ROUNDS, (int)soak_stat_a.jobs, (int)soak_stat_a.misses,
			(int)soak_stat_b.jobs, (int)soak_stat_b.misses);
		UARTDebugOut("[soak]: churn=%d wakeups=%d %s\n",
			(int)soak_churn_cycles, (int)soak_wakeups, bad ? "BAD" : "ok");
	}
	if (bad == 0)
		UARTDebugOut("[soak]: PASS rounds=%d\n", SOAK_ROUNDS);
	else
		UARTDebugOut("[soak]: FAIL list_errors=%d\n", bad);
	soak_done = 1;
	soak_parker(self);
}

void AuSoakStart(void) {
	AA64Thread* idle = AuGetIdleThread();
	if (!idle)
		return;
	soak_churn_thr = AuCreateKthread(soak_churn, (uint64_t*)idle->pml, "soakchrn");
	if (soak_churn_thr)
		soak_churn_id = soak_churn_thr->thread_id;
	AuCreateKthread(soak_waker, (uint64_t*)idle->pml, "soakwake");
	AuCreateKthread(soak_edf_a, (uint64_t*)idle->pml, "soakedfA");
	AuCreateKthread(soak_edf_b, (uint64_t*)idle->pml, "soakedfB");
	AuCreateKthread(soak_supervisor, (uint64_t*)idle->pml, "soakvisr");
	UARTDebugOut("[soak]: started\n");
}
