/* Host-side stress test for the Stage-2 DEBUG_ALLOC TLSF detectors.
 *
 * Builds the REAL KernelAA64/Mm/tlsf.c with -D__XENEVA_DEBUG_ALLOC__ and
 * exercises it natively: clean churn must produce ZERO violations (no
 * false positives across splits/merges/realloc), every injected bug must
 * fire exactly, and the pool must stay consistent throughout.
 *
 * Build & run (from repo root; GCCINC auto-detects the compiler headers):
 *   GCCINC=$(cc -print-file-name=include)
 *   mkdir -p obj
 *   cc -std=c11 -O2 -Wall -nostdinc -I Tests/stubs -I "$GCCINC" -I /usr/include \
 *      -I BaseHdr -D__XENEVA_DEBUG_ALLOC__ -DTLSF_HOST_TEST \
 *      Tests/alloc_debug_stress.c KernelAA64/Mm/tlsf.c -o obj/alloc_stress
 *   ./obj/alloc_stress
 * Flag-off churn (same PRNG/ops, detectors compiled out):
 *   same command without -D__XENEVA_DEBUG_ALLOC__.
 * Extras: -DSTRESS_TRACE (per-op stderr log), -DCHURN_ITERS=N,
 * -DSTRESS_SEED=0x... (deterministic PRNG seed).
 *
 * NOTE on the UART shim below: tlsf.c's formats assume the kernel's
 * size_t-wide %d/%x. The host vprintf reads %d as int, so values print
 * truncated to 32 bits here -- display-only. Counts and verdicts are
 * asserted via tlsf_dbg_violation_count(), never via parsed text
 * (except the leak-dump grouping check, which only matches on the
 * "#N caller=" line shape, not addresses).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <Mm/tlsf.h>

/* ---- UART shim: tee into a capture buffer for dump assertions ---- */
static char uart_cap[65536];
static size_t uart_cap_len;

void UARTDebugOut(const char *format, ...) {
	va_list ap;
	char line[1024];
	va_start(ap, format);
	vsnprintf(line, sizeof line, format, ap);
	va_end(ap);
	fputs(line, stdout);
	size_t n = strlen(line);
	if (uart_cap_len + n < sizeof uart_cap) {
		memcpy(uart_cap + uart_cap_len, line, n);
		uart_cap_len += n;
	}
}

static void uart_cap_reset(void) {
	uart_cap_len = 0;
	uart_cap[0] = '\0';
}

static int uart_cap_has(const char *s) {
	return strstr(uart_cap, s) != NULL;
}

/* ---- deterministic PRNG (same family as Tests/tlsf_stress.c) ---- */
#ifndef STRESS_SEED
#define STRESS_SEED 0x58454e45U
#endif
static uint32_t rng_state = STRESS_SEED;

static uint32_t next_random(void) {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/* ---- tiny test framework ---- */
static int failures;
static const char *test_phase = "?";

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		printf("STRESS FAIL [%s] %s:%d: ", test_phase, __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		putchar('\n'); \
		++failures; \
	} \
} while (0)

/* ---- live-range ledger: catches allocator aliasing (overlapping live
 * blocks) and harness bugs (free/realloc of unknown pointers) ---- */
#define LEDGER_MAX 512
static struct { void *base; size_t len; } ledger[LEDGER_MAX];
static unsigned ledger_n;

static void ledger_reset(void) { ledger_n = 0; }

static int ledger_known(void *ptr) {
	for (unsigned i = 0; i < ledger_n; ++i)
		if (ledger[i].base == ptr)
			return (int)i;
	return -1;
}

static void ledger_add(void *ptr, size_t len) {
	/* overlap with any live range = allocator handed out aliased memory */
	for (unsigned i = 0; i < ledger_n; ++i) {
		char *a = (char *)ledger[i].base, *b = (char *)ptr;
		if (b < a + ledger[i].len && a < b + len) {
			CHECK(0, "ALIAS: new [%p,%zu) overlaps live #%u [%p,%zu)",
				ptr, len, i, ledger[i].base, ledger[i].len);
			return;
		}
	}
	if (ledger_n < LEDGER_MAX) {
		ledger[ledger_n].base = ptr;
		ledger[ledger_n].len = len;
		++ledger_n;
	}
}

static void ledger_del(void *ptr, const char *what) {
	int i = ledger_known(ptr);
	if (i < 0) {
		CHECK(0, "%s of unknown pointer %p (harness bug)", what, ptr);
		return;
	}
	ledger[i] = ledger[--ledger_n];
}

static void ledger_resize(void *oldptr, void *newptr, size_t newlen) {
	int i = ledger_known(oldptr);
	if (i < 0) {
		CHECK(0, "realloc of unknown pointer %p (harness bug)", oldptr);
		return;
	}
	ledger[i] = ledger[--ledger_n];
	ledger_add(newptr, newlen);
}

/* Structural pool validator (host-only test aid): block headers must tile
 * the grafted region exactly with sane sizes. Layout knowledge comes from
 * the public header comments (16-byte header, low 2 size bits are flags).
 * Returns 0 if intact (callers abort on first failure to preserve state). */
static int validate_pool(void *arena, size_t size, unsigned iter) {
	char *p = (char *)arena + 16, *end = (char *)arena + size - 16;
	int n = 0;
	while (p < end) {
		size_t raw = *(const size_t *)p;
		size_t sz = raw & ~(size_t)3;
		if (sz < 32 || sz > (size_t)(end - p) + 16) {
			printf("STRESS FAIL iter=%u: bad block size %zu at offset %ld (raw=%zx)\n",
				iter, sz, (long)(p - (char *)arena), raw);
			++failures;
			return -1;
		}
		p += sz;
		if (++n > 100000) {
			printf("STRESS FAIL iter=%u: block walk runaway\n", iter);
			++failures;
			return -1;
		}
	}
	if (p != end) {
		printf("STRESS FAIL iter=%u: tiling mismatch (end=%p want=%p)\n",
			iter, (void *)p, (void *)end);
		++failures;
		return -1;
	}
	return 0;
}

/* hint-setting allocator wrappers (mirror the kernel's kmalloc layer) */
static __attribute__((noinline)) void *t_alloc(tlsf_pool_t *p, size_t n) {
#ifdef __XENEVA_DEBUG_ALLOC__
	tlsf_dbg_hint_set(__builtin_return_address(0));
#endif
	void *r = tlsf_malloc(p, n);
	if (r)
		ledger_add(r, n);
	return r;
}

static __attribute__((noinline)) void t_free(tlsf_pool_t *p, void *ptr) {
	if (!ptr)
		return; /* mirrors tlsf_free's NULL tolerance */
#ifdef __XENEVA_DEBUG_ALLOC__
	tlsf_dbg_hint_set(__builtin_return_address(0));
#endif
	ledger_del(ptr, "free");
	tlsf_free(p, ptr);
}

static __attribute__((noinline)) void *t_realloc(tlsf_pool_t *p, void *ptr, size_t n) {
	void *r;
#ifdef __XENEVA_DEBUG_ALLOC__
	tlsf_dbg_hint_set(__builtin_return_address(0));
#endif
	r = tlsf_realloc(p, ptr, n);
	if (r)
		ledger_resize(ptr, r, n);
	return r;
}

/* noinline leak sources with distinct callers (leak-dump grouping) */
static __attribute__((noinline)) void *leak_a(tlsf_pool_t *p, size_t n) {
#ifdef __XENEVA_DEBUG_ALLOC__
	tlsf_dbg_hint_set(__builtin_return_address(0));
#endif
	return tlsf_malloc(p, n);
}

static __attribute__((noinline)) void *leak_b(tlsf_pool_t *p, size_t n) {
#ifdef __XENEVA_DEBUG_ALLOC__
	tlsf_dbg_hint_set(__builtin_return_address(0));
#endif
	return tlsf_malloc(p, n);
}

/* ---- arenas ---- */
static _Alignas(16) unsigned char arena_big[512 * 1024];
static _Alignas(16) unsigned char arena_small[64 * 1024];
#ifdef __XENEVA_DEBUG_ALLOC__
static tlsf_pool_t pool_obj;

static tlsf_pool_t *fresh_pool(void *arena, size_t size) {
	tlsf_pool_t *pool = tlsf_create_at(&pool_obj);
#else
/* Flag-off: only the global singleton exists; tests run sequentially
 * and must each drain it fully (asserted via tlsf_used). */
static tlsf_pool_t *fresh_pool(void *arena, size_t size) {
	tlsf_pool_t *pool = tlsf_create();
#endif
	if (!pool || tlsf_add_memory(pool, arena, size) != 0) {
		printf("STRESS FAIL: pool init\n");
		++failures;
		return NULL;
	}
	return pool;
}

/* ---- Test A: clean churn, zero violations, exact accounting ---- */
#define CHURN_SLOTS 256
#ifndef CHURN_ITERS
#define CHURN_ITERS 20000
#endif

static void test_clean_churn(void) {
	typedef struct { unsigned char *ptr; size_t size; unsigned char pat; } slot_t;
	static slot_t slots[CHURN_SLOTS];
#ifdef __XENEVA_DEBUG_ALLOC__
	uint64_t v0 = tlsf_dbg_violation_count();
#else
	uint64_t v0 = 0;
#endif
	ledger_reset();
	tlsf_pool_t *pool = fresh_pool(arena_big, sizeof arena_big);
	if (!pool)
		return;
	memset(slots, 0, sizeof slots);

	for (unsigned i = 0; i < CHURN_ITERS; ++i) {
		unsigned s = next_random() % CHURN_SLOTS;
		slot_t *sl = &slots[s];
		if (sl->ptr) {
			for (size_t k = 0; k < sl->size; ++k)
				if (sl->ptr[k] != sl->pat) {
					CHECK(0, "churn payload corrupted iter=%u slot=%u", i, s);
					break;
				}
		}
		unsigned op = next_random() % 4;
#ifdef STRESS_TRACE
		fprintf(stderr, "i=%u slot=%u op=%u size=%zu ptr=%p\n",
			i, s, sl->ptr ? op : 9, sl->size, (void *)sl->ptr);
#endif
		if (validate_pool(arena_big, sizeof arena_big, i) != 0)
			exit(1);
		if (!sl->ptr) {
			size_t n = 1 + next_random() % 16384;
			sl->ptr = t_alloc(pool, n);
			if (!sl->ptr)
				continue;
			sl->size = n;
			sl->pat = (unsigned char)(s + 1);
			memset(sl->ptr, sl->pat, n);
		} else if (op == 0) {
			t_free(pool, sl->ptr);
			memset(sl, 0, sizeof *sl);
		} else {
			size_t old = sl->size;
			size_t n = 1 + next_random() % 16384;
			unsigned char *r = t_realloc(pool, sl->ptr, n);
			if (!r)
				continue;
			size_t keep = old < n ? old : n;
			for (size_t k = 0; k < keep; ++k)
				if (r[k] != sl->pat) {
					CHECK(0, "realloc lost data iter=%u", i);
					break;
				}
			sl->ptr = r;
			sl->size = n;
			memset(r, sl->pat, n);
		}
#ifdef STRESS_TRACE
		fprintf(stderr, "post i=%u slot=%u ptr=%p off=%ld size=%zu\n",
			i, s, (void *)sl->ptr,
			sl->ptr ? (long)((char *)sl->ptr - (char *)arena_big) : -1,
			sl->size);
#endif
	}
	for (unsigned s = 0; s < CHURN_SLOTS; ++s)
		if (slots[s].ptr) {
			t_free(pool, slots[s].ptr);
			slots[s].ptr = NULL;
		}
	CHECK(tlsf_used(pool) == 0, "used_size not drained: %zu", tlsf_used(pool));
#ifdef __XENEVA_DEBUG_ALLOC__
	CHECK(tlsf_dbg_violation_count() == v0, "false positives during clean churn: %llu",
		(unsigned long long)(tlsf_dbg_violation_count() - v0));
	printf("STRESS clean churn: %u iters, violations=%llu\n", CHURN_ITERS,
		(unsigned long long)(tlsf_dbg_violation_count() - v0));
#else
	(void)v0;
	printf("STRESS clean churn: %u iters (flag-off, no detectors)\n", CHURN_ITERS);
#endif
}

/* ---- Test B: injection battery, each fires exactly once ---- */
#ifdef __XENEVA_DEBUG_ALLOC__
static void expect_delta(uint64_t before, uint64_t want, const char *name) {
	uint64_t got = tlsf_dbg_violation_count() - before;
	CHECK(got == want, "%s: violations=%llu want=%llu", name,
		(unsigned long long)got, (unsigned long long)want);
}

static void test_injections(void) {
	ledger_reset();
	tlsf_pool_t *pool = fresh_pool(arena_small, sizeof arena_small);
	uint64_t v;
	if (!pool)
		return;

	/* pinned guards so no merge moves the victim */
	void *g1 = t_alloc(pool, 32);
	void *victim = t_alloc(pool, 32);
	void *g2 = t_alloc(pool, 32);
	CHECK(g1 && victim && g2, "injection setup");

	v = tlsf_dbg_violation_count();
	((volatile char *)victim)[32] = 0x41;
	t_free(pool, victim);
	expect_delta(v, 1, "overflow-back");

	victim = t_alloc(pool, 32);
	v = tlsf_dbg_violation_count();
	((volatile char *)victim)[-1] = 0x42;
	t_free(pool, victim);
	expect_delta(v, 1, "underflow-front");

	victim = t_alloc(pool, 32);
	t_free(pool, victim);
	((volatile char *)victim)[0] = 0x43;
	v = tlsf_dbg_violation_count();
	void *reused = t_alloc(pool, 32);
	CHECK(reused == victim, "uaf realloc moved (want same block)");
	expect_delta(v, 1, "uaf");

	void *d = t_alloc(pool, 16);
	t_free(pool, d);
	v = tlsf_dbg_violation_count();
	/* Deliberate double free: bypass the ledger (which rightly rejects
	 * freeing unknown pointers) and call the backend directly. */
	tlsf_dbg_hint_set(__builtin_return_address(0));
	tlsf_free(pool, d);
	expect_delta(v, 1, "double-free");

	/* corruption across a coalesced merge: free two adjacent blocks so
	 * they fuse (absorbed header poisoned), corrupt the interior, then
	 * reallocate across it -- must fire, proving merge-time poisoning.
	 * NOTE offsets are user-relative: the absorbed m2 header sits at
	 * user+80 (block+128), and the re-verified range starts at user+0. */
	void *m1 = t_alloc(pool, 64);
	void *m2 = t_alloc(pool, 64);
	void *mguard = t_alloc(pool, 64);
	CHECK(m1 && m2 && mguard, "merge setup");
	t_free(pool, m2);
	t_free(pool, m1); /* m1+m2 fuse into one free run */
	((volatile unsigned char *)m1)[84] = 0x44;
	v = tlsf_dbg_violation_count();
	void *big = t_alloc(pool, 128);
	CHECK(big == m1, "merged realloc moved (want same start)");
	(void)big;
	expect_delta(v, 1, "uaf-across-merge");

	/* pool still consistent: fresh allocs work, drain to zero */
	void *z = t_alloc(pool, 1024);
	CHECK(z != NULL, "pool wedged after injections");
	t_free(pool, z);
	t_free(pool, g1);
	t_free(pool, g2);
	t_free(pool, reused);
	t_free(pool, mguard);
	t_free(pool, big);
	CHECK(tlsf_used(pool) == 0, "injections did not drain: %zu", tlsf_used(pool));
	printf("STRESS injections done\n");
}

/* ---- Test C: leak dump grouping + region-table cap ---- */
static void test_leak_dump(void) {
	ledger_reset();
	tlsf_pool_t *pool = fresh_pool(arena_small, sizeof arena_small);
	if (!pool)
		return;
	void *l1 = leak_a(pool, 64);
	void *l2 = leak_a(pool, 96);
	void *l3 = leak_b(pool, 128);
	if (l1)
		ledger_add(l1, 64);
	if (l2)
		ledger_add(l2, 96);
	if (l3)
		ledger_add(l3, 128);
	uart_cap_reset();
	tlsf_leak_dump(pool);
	CHECK(uart_cap_has("live blocks"), "dump header missing");
	CHECK(uart_cap_has("#0 caller="), "dump groups missing");
	printf("STRESS leak dump groups ok\n");
	t_free(pool, l1);
	t_free(pool, l2);
	t_free(pool, l3);

	/* graft 40 tiny regions: table holds 32, dump must not crash */
	static _Alignas(16) unsigned char many[40][1024];
	tlsf_pool_t extra;
	tlsf_create_at(&extra);
	for (int i = 0; i < 40; ++i)
		tlsf_add_memory(&extra, many[i], sizeof many[i]);
	uart_cap_reset();
	tlsf_leak_dump(&extra);
	CHECK(uart_cap_has("live blocks"), "capped dump failed");
	printf("STRESS region cap ok\n");
}
#endif /* __XENEVA_DEBUG_ALLOC__ */

int main(void) {
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	test_phase = "churn";
	test_clean_churn();
#ifdef __XENEVA_DEBUG_ALLOC__
	test_phase = "inject";
	test_injections();
	test_phase = "dump";
	test_leak_dump();
#endif
	if (failures == 0)
		printf("STRESS ALL PASS\n");
	else
		printf("STRESS %d FAILURES\n", failures);
	return failures != 0;
}
