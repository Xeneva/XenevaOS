/**
 * BSD 2-Clause License
 *
 * Copyright (c) 2022-2023, Manas Kamal Choudhury
 * 
 * Author:
 *      Karthik Ramanathan Lakshmanan,
 *      karthik20066002@gmail.com
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
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
//can we all agree to use clang-format, and something akin to clang-tidy to maintain code quality and style? --axiss
//also, comments are autogen by the buildproc. im adding on wherever needed.
#include <Mm/tlsf.h>
#include <stdint.h>
#include <string.h>
#include <_null.h>
#include <Log/klog.h>
#if defined(__GNUC__) || defined(__clang__)
#include <stdbool.h>
#endif
#include <Drivers/uart.h>

/* ---- Global pool (statically allocated; no circular dependency) ---- */
static tlsf_pool_t g_tlsf_pool_obj;
static tlsf_pool_t* g_pool = NULL;

tlsf_pool_t* tlsf_get_pool(void) {
	return g_pool;
}

/* ---- Bit-scan helpers ---- */

static inline int tlsf_fls(size_t x) {
	return (int)(sizeof(size_t) * 8) - (int)__builtin_clzll(x) - 1;
}

static inline int tlsf_ffs32(uint32_t word) {
	if (word == 0)
		return -1;
	return (int)__builtin_ffs((int)word) - 1;
}

static inline int tlsf_ffs64(uint64_t word) {
	if (word == 0)
		return -1;
	return (int)__builtin_ffsll((long long)word) - 1;
}

/* ---- Block helpers ---- */

static inline size_t blk_size(const block_header_t* hdr) {
	return hdr->size & BLOCK_SIZE_MASK;
}

static inline bool blk_is_free(const block_header_t* hdr) {
	return (hdr->size & BLOCK_FLAG_FREE) != 0;
}

static inline bool blk_prev_free(const block_header_t* hdr) {
	return (hdr->size & BLOCK_FLAG_PREV_FREE) != 0;
}

static inline void* blk_payload(block_header_t* hdr) {
	return (void*)((char*)hdr + TLSF_HEADER_SIZE);
}

static inline block_header_t* blk_from_payload(void* ptr) {
	return (block_header_t*)((char*)ptr - TLSF_HEADER_SIZE);
}

static inline block_header_t* blk_next(const block_header_t* hdr) {
	return (block_header_t*)((char*)hdr + blk_size(hdr));
}

static inline block_header_t* blk_prev(const block_header_t* hdr) {
	return (block_header_t*)((char*)hdr - hdr->prev_size);
}

static inline void blk_set_size(block_header_t* hdr, size_t sz, bool free, bool prev_free) {
	hdr->size = sz | (free ? BLOCK_FLAG_FREE : 0U) | (prev_free ? BLOCK_FLAG_PREV_FREE : 0U);
}

#ifdef __XENEVA_DEBUG_ALLOC__
/* ---- Stage 2: redzones / poisoning / caller tracking ----
 * Every helper here is flag-gated; the flag-off translation unit does
 * not declare any of it (no dead structs, no extra branches). */
static uint64_t tlsf_dbg_seq = 0;
static uint64_t tlsf_dbg_violations = 0;
/* Caller hints are per-pool-region: the leak dump only walks regions
 * grafted into the pool being dumped (scratch test pools stay out of
 * the live heap's dump and vice versa). */
#define TLSF_DBG_MAX_REGIONS 32
static struct { void* base; size_t size; tlsf_pool_t* pool; } tlsf_dbg_regions[TLSF_DBG_MAX_REGIONS];
static unsigned tlsf_dbg_region_count = 0;
/* Latest stashed wrapper return address (see below). Single-CPU +
 * IRQs-masked heap sections make this race-free here; an SMP future
 * must make it per-CPU before relying on it. */
static void* tlsf_dbg_hint = NULL;

void tlsf_dbg_hint_set(void* caller) { tlsf_dbg_hint = caller; }

#define TLSF_DBG_USER_OFF (TLSF_HEADER_SIZE + TLSF_DBG_HDR_SIZE + TLSF_RZ_SIZE) /* 48 */

uint64_t tlsf_dbg_violation_count(void) { return tlsf_dbg_violations; }

tlsf_pool_t* tlsf_create_at(tlsf_pool_t* pool) {
	if (!pool)
		return NULL;
	memset(pool, 0, sizeof(tlsf_pool_t));
	return pool;
}

static inline tlsf_dbg_hdr_t* tlsf_dbg(block_header_t* hdr) {
	return (tlsf_dbg_hdr_t*)((char*)hdr + TLSF_HEADER_SIZE);
}

static inline void* tlsf_dbg_user(block_header_t* hdr) {
	/* blk_payload stops at the tlsf header; the user area sits past
	 * the debug header and front redzone. (Spelled via blk_payload so
	 * the helper stays live in both configs.) */
	return (void*)((char*)blk_payload(hdr) + TLSF_DBG_HDR_SIZE + TLSF_RZ_SIZE);
}

static inline block_header_t* tlsf_dbg_block(void* user) {
	return blk_from_payload((char*)user - (TLSF_DBG_HDR_SIZE + TLSF_RZ_SIZE));
}

static inline void* tlsf_dbg_front_rz(block_header_t* hdr) {
	return (void*)((char*)hdr + TLSF_HEADER_SIZE + TLSF_DBG_HDR_SIZE);
}

static inline void* tlsf_dbg_back_rz(block_header_t* hdr, uint32_t user_size) {
	return (void*)((char*)hdr + TLSF_DBG_USER_OFF + user_size);
}

static bool tlsf_rz_ok(const void* rz) {
	const unsigned char* b = (const unsigned char*)rz;
	for (size_t i = 0; i < TLSF_RZ_SIZE; ++i)
		if (b[i] != TLSF_RZ_PATTERN)
			return false;
	return true;
}

static bool tlsf_poison_ok(const void* p, size_t n) {
	const unsigned char* b = (const unsigned char*)p;
	for (size_t i = 0; i < n; ++i)
		if (b[i] != TLSF_POISON_FREE)
			return false;
	return true;
}

/* Clamp a possibly-corrupt stored user_size into its block. Never trust
 * header contents for bounds until they are validated. */
static uint32_t tlsf_dbg_sane_user(block_header_t* hdr, uint32_t user_size) {
	uint64_t back_off = (uint64_t)TLSF_DBG_USER_OFF + user_size;
	if (back_off + TLSF_RZ_SIZE > blk_size(hdr))
		return 0;
	return user_size;
}
#endif /* __XENEVA_DEBUG_ALLOC__ */

/* Free-list smash from a kfree'd-then-overwritten filename shows up as
 * next_free = ASCII ("ctrl.exe" was 0x6578652E6C727463). Kernel heap
 * pointers are TTBR1 VAs. Refuse to follow anything else. */
static bool tlsf_ptr_sane(const void* p) {
	uintptr_t a = (uintptr_t)p;
	if (a == 0)
		return true;
	if (a & (uintptr_t)TLSF_ALIGN_MASK)
		return false;
#ifndef TLSF_HOST_TEST
	/* Kernel-only: heap pointers are TTBR1 VAs. The host unit test
	 * (Tests/alloc_debug_stress.c) defines TLSF_HOST_TEST, where user
	 * addresses live below this range and the check would reject every
	 * block. Kernel builds never define it: flag-off codegen identical. */
	if (a < 0xFFFF000000000000ULL)
		return false;
#endif
	return true;
}

static bool tlsf_free_block_ok(const free_block_t* blk) {
	if (!blk || !tlsf_ptr_sane(blk))
		return false;
	if (!blk_is_free(&blk->hdr))
		return false;
	if (!tlsf_ptr_sane(blk->next_free) || !tlsf_ptr_sane(blk->prev_free))
		return false;
	return true;
}

/* ---- TLSF size mapping ---- */

static void tlsf_mapping(size_t size, int* fl, int* sl) {
	if (size < SL_INDEX_COUNT) {
		*fl = 0;
		*sl = (int)size;
	} else {
		int f = tlsf_fls(size);
		*fl = f;
		*sl = (int)((size ^ ((size_t)1 << f)) >> (f - SL_INDEX_COUNT_LOG2));
	}
}

/* Search mapping (reference TLSF behavior): round the request UP to
 * second-level granularity before bucketing, so the starting bucket can
 * only hold blocks >= size. Without this, a same-first-level bucket may
 * yield a SMALLER block and the whole-block fallback hands it out short
 * -- a silent heap overflow in the caller (caught by the Stage-2 stress
 * battery as a 6-byte neighbor-header smash). Insert/remove keep using
 * tlsf_mapping with the block's exact size; using the search mapping
 * there would mis-file blocks and reintroduce aliasing. */
static void tlsf_mapping_search(size_t size, int* fl, int* sl) {
	if (size >= SL_INDEX_COUNT) {
		int f = tlsf_fls(size);
		size += ((size_t)1 << (f - SL_INDEX_COUNT_LOG2)) - 1;
	}
	tlsf_mapping(size, fl, sl);
}

/* ---- Free-list management ---- */

static void tlsf_drop_bucket(tlsf_pool_t* pool, int fl, int sl) {
	if (fl < 0 || sl < 0 || fl >= (int)FL_INDEX_COUNT || sl >= (int)SL_INDEX_COUNT)
		return;
	pool->blocks[fl][sl] = NULL;
	pool->sl_bitmap[fl] &= ~(1U << sl);
	if (pool->sl_bitmap[fl] == 0)
		pool->fl_bitmap &= ~(UINT64_C(1) << fl);
}

static void tlsf_remove_free_block(tlsf_pool_t* pool, free_block_t* blk, int fl, int sl) {
	if (!tlsf_free_block_ok(blk)) {
		UARTDebugOut("[tlsf]: corrupt free block %x (next=%x prev=%x), dropping bucket %d/%d\r\n",
					 blk,
					 blk && tlsf_ptr_sane(blk) ? (void*)blk->next_free : (void*)0,
					 blk && tlsf_ptr_sane(blk) ? (void*)blk->prev_free : (void*)0,
					 fl,
					 sl);
		tlsf_drop_bucket(pool, fl, sl);
		return;
	}

	free_block_t* prev = blk->prev_free;
	free_block_t* next = blk->next_free;

	if (prev && !tlsf_free_block_ok(prev))
		prev = NULL;
	if (next && !tlsf_free_block_ok(next))
		next = NULL;

	if (prev)
		prev->next_free = next;
	else
		pool->blocks[fl][sl] = next;

	if (next)
		next->prev_free = prev;

	if (pool->blocks[fl][sl] == NULL) {
		pool->sl_bitmap[fl] &= ~(1U << sl);
		if (pool->sl_bitmap[fl] == 0)
			pool->fl_bitmap &= ~(UINT64_C(1) << fl);
	}
}

static void tlsf_insert_free_block(tlsf_pool_t* pool, free_block_t* blk, int fl, int sl) {
	free_block_t* head = pool->blocks[fl][sl];
	if (head && !tlsf_free_block_ok(head)) {
		UARTDebugOut("[tlsf]: corrupt list head %x in bucket %d/%d, replacing\r\n", head, fl, sl);
		head = NULL;
		tlsf_drop_bucket(pool, fl, sl);
	}

	blk->next_free = head;
	blk->prev_free = NULL;

	if (head)
		head->prev_free = blk;

	pool->blocks[fl][sl] = blk;

	pool->sl_bitmap[fl] |= (1U << sl);
	pool->fl_bitmap |= (UINT64_C(1) << fl);
}

/* ---- Find best-fit free block ---- */

static free_block_t* tlsf_find_free_block(tlsf_pool_t* pool, size_t size) {
	int fl, sl;
	tlsf_mapping_search(size, &fl, &sl);

	uint32_t sl_masked = pool->sl_bitmap[fl] & ~((1U << sl) - 1);
	if (sl_masked) {
		int found_sl = tlsf_ffs32(sl_masked);
		free_block_t* blk = pool->blocks[fl][found_sl];
		if (blk && !tlsf_free_block_ok(blk)) {
			UARTDebugOut("[tlsf]: corrupt bucket %d/%d head %x\r\n", fl, found_sl, blk);
			tlsf_drop_bucket(pool, fl, found_sl);
			blk = NULL;
		}
		if (blk)
			return blk;
	}

	/* no suitable second-level bucket left in this first-level class, so i
	 * search strictly larger fl classes. including `fl` here wouldve let a
	 * lower SL bucket win and return a block smaller than `size`, thats an
	 * instant heap overflow in the caller --axiss */
	uint64_t fl_masked = 0;
	if ((size_t)(fl + 1) < FL_INDEX_COUNT)
		fl_masked = pool->fl_bitmap & (~UINT64_C(0) << (fl + 1));
	if (fl_masked) {
		int found_fl = tlsf_ffs64(fl_masked);
		int found_sl = tlsf_ffs32(pool->sl_bitmap[found_fl]);
		free_block_t* blk = pool->blocks[found_fl][found_sl];
		if (blk && !tlsf_free_block_ok(blk)) {
			UARTDebugOut("[tlsf]: corrupt bucket %d/%d head %x\r\n", found_fl, found_sl, blk);
			tlsf_drop_bucket(pool, found_fl, found_sl);
			return NULL;
		}
		return blk;
	}

	return NULL;
}

/* ---- Public API ---- */
// For now, this shit works alright, but SMP is a pipe dream --axiss
tlsf_pool_t* tlsf_create(void) {
	tlsf_pool_t* pool = &g_tlsf_pool_obj;
	memset(pool, 0, sizeof(tlsf_pool_t));
	g_pool = pool;
	return pool;
}

int tlsf_add_memory(tlsf_pool_t* pool, void* mem, size_t size) {
	if (!pool || !mem)
		return -1;

	size_t addr = (size_t)mem;
	addr = TLSF_ALIGN_UP(addr);
	size_t avail = (size_t)mem + size - addr;
	avail = avail & ~(size_t)TLSF_ALIGN_MASK;

	if (avail < TLSF_SENTINEL_SIZE * 2 + TLSF_MIN_BLOCK_SIZE)
		return -1;

	char* region = (char*)addr;
	size_t free_size = avail - TLSF_SENTINEL_SIZE * 2;

	/* Start sentinel*/
	block_header_t* sentinel_start = (block_header_t*)region;
	blk_set_size(sentinel_start, TLSF_SENTINEL_SIZE, false, false);
	sentinel_start->prev_size = 0;

	/* One large free block between sentinels */
	free_block_t* free_blk = (free_block_t*)(region + TLSF_SENTINEL_SIZE);
	blk_set_size(&free_blk->hdr, free_size, true, false);
	free_blk->hdr.prev_size = TLSF_SENTINEL_SIZE;

	/* End sentinel*/
	block_header_t* sentinel_end = (block_header_t*)(region + avail - TLSF_SENTINEL_SIZE);
	blk_set_size(sentinel_end, TLSF_SENTINEL_SIZE, false, false);
	sentinel_end->prev_size = free_size;
	sentinel_end->size |= BLOCK_FLAG_PREV_FREE;

	int fl, sl;
	tlsf_mapping(free_size, &fl, &sl);
	tlsf_insert_free_block(pool, free_blk, fl, sl);
#ifdef __XENEVA_DEBUG_ALLOC__
	/* Record the region so the leak dump can walk it, and poison the
	 * virgin free body: everything past the live header+linkage is
	 * 0x6B, so a later malloc can verify the full handed-out range. */
	if (tlsf_dbg_region_count < TLSF_DBG_MAX_REGIONS) {
		tlsf_dbg_regions[tlsf_dbg_region_count].base = (void*)addr;
		tlsf_dbg_regions[tlsf_dbg_region_count].size = avail;
		tlsf_dbg_regions[tlsf_dbg_region_count].pool = pool;
		++tlsf_dbg_region_count;
	}
	memset((char*)free_blk + TLSF_HEADER_SIZE + 2 * sizeof(void*),
		TLSF_POISON_FREE,
		free_size - TLSF_HEADER_SIZE - 2 * sizeof(void*));
#endif

	pool->pool_size += avail;
	return 0;
}

void* tlsf_malloc(tlsf_pool_t* pool, size_t size) {
	if (!pool || !size)
		return NULL;
	if (size > (size_t)-1 - TLSF_HEADER_SIZE - TLSF_ALIGN_MASK
#ifdef __XENEVA_DEBUG_ALLOC__
		- TLSF_DBG_OVERHEAD
#endif
		)
		return NULL;

#ifdef __XENEVA_DEBUG_ALLOC__
	size_t user_req = size;
	/* Caller attribution WITHOUT frame walking: the public wrappers
	 * (kmalloc/krealloc/kfree, test helpers) stash return_address(0) --
	 * their own frame, always safe -- in tlsf_dbg_hint before entering.
	 * return_address(1) here would walk the x29 chain, which this build
	 * does not maintain (omitted frame pointers): it faults on a NULL FP
	 * or, worse, silently stamps stack garbage. Fall back to our own
	 * frame (the wrapper) when no hint was stashed (direct backend users
	 * like the host stress harness). */
	void* alloc_caller = tlsf_dbg_hint ? tlsf_dbg_hint : __builtin_return_address(0);
#endif
	/* `size` is the caller's requested *payload* size. The block we carve
	 * out must also hold its own header, or the payload the caller actually
	 * writes into overruns into the next physical block's header. Round up
	 * to a block size that includes TLSF_HEADER_SIZE before searching/
	 * splitting — every size below used the raw payload size as if it were
	 * the whole block, which is TLSF_HEADER_SIZE (16) bytes short. */
	size = TLSF_ALIGN_UP(size + TLSF_HEADER_SIZE
#ifdef __XENEVA_DEBUG_ALLOC__
		+ TLSF_DBG_OVERHEAD
#endif
		);
	if (size < TLSF_MIN_BLOCK_SIZE)
		size = TLSF_MIN_BLOCK_SIZE;

	free_block_t* blk = tlsf_find_free_block(pool, size);
	if (!blk || !tlsf_free_block_ok(blk))
		return NULL;

	size_t block_size = blk_size(&blk->hdr);
	// We split the block if the leftover size is enough to hold a new free block (including its header)
	/* blk's actual bucket is determined by its own size (mapping-insert),
	 * which is not necessarily the same bucket the search for `size` landed
	 * on so tlsf_find_free_block falls back to a larger bucket whenever the
	 * exact-size bucket is empty. Removing with the wrong (fl, sl) unlinks
	 * nothing: the block stays registered as free and gets handed out again
	 * on a later allocation while still in use, aliasing two live callers
	 * onto the same memory. */
	int fl, sl;
	tlsf_mapping(block_size, &fl, &sl);
	tlsf_remove_free_block(pool, blk, fl, sl);

	bool prev_free = blk_prev_free(&blk->hdr);

	if (block_size >= size + TLSF_MIN_BLOCK_SIZE) {
		/* Split */
		size_t new_size = block_size - size;
		free_block_t* new_blk = (free_block_t*)((char*)blk + size);
		block_header_t* new_hdr = &new_blk->hdr;

		blk_set_size(new_hdr, new_size, true, false);
		new_hdr->prev_size = size;

		blk_set_size(&blk->hdr, size, false, prev_free);

		/* the allocated block is followed by new_hdr, whose predecessor is
		 * allocated. i make the block after the remainder point back by
		 * the remainder's size and mark its predecessor as free --axiss */
		new_hdr->prev_size = size;
		new_hdr->size &= ~BLOCK_FLAG_PREV_FREE;
		block_header_t* successor = blk_next(new_hdr);
		successor->prev_size = new_size;
		successor->size |= BLOCK_FLAG_PREV_FREE;

		int nfl, nsl;
		tlsf_mapping(new_size, &nfl, &nsl);
		tlsf_insert_free_block(pool, new_blk, nfl, nsl);
	} else {
		/* Use the entire block */
		blk_set_size(&blk->hdr, block_size, false, prev_free);

		block_header_t* next = blk_next(&blk->hdr);
		next->prev_size = block_size;
		next->size &= ~BLOCK_FLAG_PREV_FREE;
	}

	pool->used_size += blk_size(&blk->hdr) - TLSF_HEADER_SIZE;
#ifdef __XENEVA_DEBUG_ALLOC__
	/* Write-after-free check: the handed-out range starts past the stale
	 * linkage zone, so per the free-body invariant it must still be all
	 * 0x6B. Anything else is a write into a freed block. */
	void* user = tlsf_dbg_user(&blk->hdr);
	if (!tlsf_poison_ok(user, user_req)) {
		++tlsf_dbg_violations;
		UARTDebugOut("[tlsf]: UAF block=%x size=%d (0x6B poison disturbed)\r\n",
			user, user_req);
	}
	tlsf_dbg_hdr_t* birth = tlsf_dbg(&blk->hdr);
	birth->caller = alloc_caller;
	birth->seq = (uint32_t)++tlsf_dbg_seq;
	birth->user_size = (uint32_t)user_req;
	memset(tlsf_dbg_front_rz(&blk->hdr), TLSF_RZ_PATTERN, TLSF_RZ_SIZE);
	memset(user, TLSF_POISON_FRESH, user_req);
	memset((char*)user + user_req, TLSF_RZ_PATTERN, TLSF_RZ_SIZE);
	return user;
#else
	return blk_payload(&blk->hdr);
#endif
}

void tlsf_free(tlsf_pool_t* pool, void* ptr) {
	if (!pool || !ptr)
		return;

#ifdef __XENEVA_DEBUG_ALLOC__
	block_header_t* hdr = tlsf_dbg_block(ptr);
	/* Faulting freeing caller, same hint channel (stashed by kfree). */
	void* free_caller = tlsf_dbg_hint ? tlsf_dbg_hint : __builtin_return_address(0);
#else
	block_header_t* hdr = blk_from_payload(ptr);
#endif
	size_t cur_size = blk_size(hdr);
	size_t allocated_size = cur_size;
	/* not letting a repeated free unlink an allocated payload like its
	 * first words were free-list pointers --axiss */
	if (blk_is_free(hdr)) {
#ifdef __XENEVA_DEBUG_ALLOC__
		++tlsf_dbg_violations;
		UARTDebugOut("[tlsf]: DOUBLE FREE block=%x by=%x\r\n", ptr, free_caller);
#endif
		return;
	}
#ifdef __XENEVA_DEBUG_ALLOC__
	/* Read the debug header BEFORE anything overwrites it (poisoning
	 * below and free-list linkage both destroy it). */
	tlsf_dbg_hdr_t* dying = tlsf_dbg(hdr);
	void* alloc_caller = dying->caller;
	uint32_t alloc_seq = dying->seq;
	uint32_t user_size = tlsf_dbg_sane_user(hdr, dying->user_size);
	if (user_size != dying->user_size) {
		++tlsf_dbg_violations;
		UARTDebugOut("[tlsf]: CORRUPT HEADER block=%x size=%d caller=%x seq=%d\r\n",
			ptr, cur_size, alloc_caller, alloc_seq);
	}
	if (!tlsf_rz_ok(tlsf_dbg_front_rz(hdr))) {
		++tlsf_dbg_violations;
		UARTDebugOut("[tlsf]: REDZONE front block=%x size=%d caller=%x seq=%d\r\n",
			ptr, user_size, alloc_caller, alloc_seq);
	}
	if (!tlsf_rz_ok(tlsf_dbg_back_rz(hdr, user_size))) {
		++tlsf_dbg_violations;
		UARTDebugOut("[tlsf]: REDZONE back block=%x size=%d caller=%x seq=%d\r\n",
			ptr, user_size, alloc_caller, alloc_seq);
	}
	/* Poison the whole body past the live header. Linkage is rewritten
	 * by tlsf_insert_free_block below; absorbed stale headers are
	 * poisoned at each merge site. Report-and-continue: the block is
	 * still freed normally, so the pool stays consistent. */
	memset((char*)hdr + TLSF_HEADER_SIZE, TLSF_POISON_FREE, cur_size - TLSF_HEADER_SIZE);
#endif
	bool was_prev_free = blk_prev_free(hdr);
	size_t saved_prev_size = hdr->prev_size;

	/* 1. Mark current block as free */
	hdr->size = cur_size | BLOCK_FLAG_FREE;
	if (was_prev_free)
		hdr->size |= BLOCK_FLAG_PREV_FREE;

	/* 2. Coalesce(lol) backward */
	if (was_prev_free && saved_prev_size > 0) {
		block_header_t* prev = (block_header_t*)((char*)hdr - saved_prev_size);
		size_t prev_sz = blk_size(prev);
		/* merged block inherits the predecessor's PREV_FREE bit here, not
		 * hdr's. hdr's bit only says `prev` is free, which we already know
		 * in this branch. propagating it made the merged block claim the
		 * block before `prev` was free even when it was actually
		 * allocated, then on ITS next free TLSF unlinked that allocated
		 * block like it was a free-list node. the resulting
		 * next_free/prev_free garbage is exactly the 0x2000 -> 0x2010
		 * translation fault i saw in Namdapha --axiss */
		bool prev_prev_free = blk_prev_free(prev);

		int fl, sl;
		tlsf_mapping(prev_sz, &fl, &sl);
		tlsf_remove_free_block(pool, (free_block_t*)prev, fl, sl);

		/* Merge into prev: hdr becomes prev */
#ifdef __XENEVA_DEBUG_ALLOC__
		/* hdr's own header becomes interior bytes of the merged block:
		 * poison it (its body was already poisoned on entry). */
		memset(hdr, TLSF_POISON_FREE, TLSF_HEADER_SIZE);
#endif
		cur_size += prev_sz;
		hdr = prev;
		/* hdr->prev_size stays the same (it was prev's prev_size) */
		blk_set_size(hdr, cur_size, true, prev_prev_free);
	}

	/* 3. Coalesce(lol) forward */
	block_header_t* next = blk_next(hdr);
	if (blk_is_free(next)) {
		size_t next_sz = blk_size(next);

		int fl, sl;
		tlsf_mapping(next_sz, &fl, &sl);
		tlsf_remove_free_block(pool, (free_block_t*)next, fl, sl);

#ifdef __XENEVA_DEBUG_ALLOC__
		/* next's header+linkage become interior bytes of the merged
		 * block: poison them to preserve the free-body invariant. */
		memset(next, TLSF_POISON_FREE, TLSF_HEADER_SIZE + 2 * sizeof(void*));
#endif
		cur_size = blk_size(hdr) + next_sz;
		bool pf = blk_prev_free(hdr);
		blk_set_size(hdr, cur_size, true, pf);
	}

	/* 4. Update next physical block */
	next = blk_next(hdr);
	next->prev_size = blk_size(hdr);
	next->size |= BLOCK_FLAG_PREV_FREE;

	/* 5. Insert merged block */
	int fl, sl;
	tlsf_mapping(blk_size(hdr), &fl, &sl);
	tlsf_insert_free_block(pool, (free_block_t*)hdr, fl, sl);

	size_t payload_size = allocated_size - TLSF_HEADER_SIZE;
	pool->used_size = pool->used_size >= payload_size ?
		pool->used_size - payload_size : 0;
}

void* tlsf_realloc(tlsf_pool_t* pool, void* ptr, size_t size) {
	if (!ptr)
		return tlsf_malloc(pool, size);

	if (!size) {
		tlsf_free(pool, ptr);
		return NULL;
	}

	/* `size` is the caller's requested payload size; `need` is the total
	 * block size (header included) that must actually be carved out — see
	 * the same fix in tlsf_malloc(). Keep `size` untouched so the fallback
	 * path below can still pass the original payload size to tlsf_malloc(). */
	if (size > (size_t)-1 - TLSF_HEADER_SIZE - TLSF_ALIGN_MASK
#ifdef __XENEVA_DEBUG_ALLOC__
		- TLSF_DBG_OVERHEAD
#endif
		)
		return NULL;
#ifdef __XENEVA_DEBUG_ALLOC__
	size_t new_req = size;
	size_t need = TLSF_ALIGN_UP(size + TLSF_HEADER_SIZE + TLSF_DBG_OVERHEAD);
#else
	size_t need = TLSF_ALIGN_UP(size + TLSF_HEADER_SIZE);
#endif
	if (need < TLSF_MIN_BLOCK_SIZE)
		need = TLSF_MIN_BLOCK_SIZE;

#ifdef __XENEVA_DEBUG_ALLOC__
	block_header_t* hdr = tlsf_dbg_block(ptr);
	uint32_t old_user = tlsf_dbg_sane_user(hdr, tlsf_dbg(hdr)->user_size);
#else
	block_header_t* hdr = blk_from_payload(ptr);
#endif
	size_t old_size = blk_size(hdr);

	if (old_size >= need) {
		/* Shrink or stay same size */
		size_t remaining = old_size - need;
		if (remaining >= TLSF_MIN_BLOCK_SIZE) {
			/* Split */
			size_t rem_size = remaining;
			block_header_t* new_block = (block_header_t*)((char*)hdr + need);
			blk_set_size(new_block, rem_size, true, false);
			new_block->prev_size = need;

			bool pf = blk_prev_free(hdr);
			blk_set_size(hdr, need, false, pf);

			/* hdr + need is the newly-created free block itself, its prev_size
			 * already got set above, so updating the block after it instead --axiss */
			block_header_t* next = blk_next(new_block);
			next->prev_size = rem_size;
			next->size |= BLOCK_FLAG_PREV_FREE;

			int fl, sl;
			tlsf_mapping(rem_size, &fl, &sl);
#ifdef __XENEVA_DEBUG_ALLOC__
			/* The remainder is carved from a live block: its body is
			 * stale user data, so poison it (linkage follows via
			 * insert). Move the back redzone to the shrunken end. */
			tlsf_dbg(hdr)->user_size = (uint32_t)new_req;
			memset(tlsf_dbg_back_rz(hdr, (uint32_t)new_req), TLSF_RZ_PATTERN, TLSF_RZ_SIZE);
			memset((char*)new_block + TLSF_HEADER_SIZE, TLSF_POISON_FREE,
				rem_size - TLSF_HEADER_SIZE);
#endif
			tlsf_insert_free_block(pool, (free_block_t*)new_block, fl, sl);
			pool->used_size -= remaining;
		}
#ifdef __XENEVA_DEBUG_ALLOC__
		else {
			/* No split: still move the back redzone and refresh the
			 * extended user bytes (grow-within-block) to 0xAA. */
			if (new_req != old_user) {
				if (new_req > old_user)
					memset((char*)hdr + TLSF_DBG_USER_OFF + old_user,
						TLSF_POISON_FRESH, new_req - old_user);
				tlsf_dbg(hdr)->user_size = (uint32_t)new_req;
				memset(tlsf_dbg_back_rz(hdr, (uint32_t)new_req),
					TLSF_RZ_PATTERN, TLSF_RZ_SIZE);
			}
		}
#endif
		return ptr;
	}

	/* Grow: try in-place expansion with next block */
	block_header_t* next = blk_next(hdr);
	if (blk_is_free(next)) {
		size_t next_sz = blk_size(next);
		if (old_size + next_sz >= need) {
			int fl, sl;
			tlsf_mapping(next_sz, &fl, &sl);
			tlsf_remove_free_block(pool, (free_block_t*)next, fl, sl);

			size_t remaining = old_size + next_sz - need;
			if (remaining >= TLSF_MIN_BLOCK_SIZE) {
				/* Split after expansion */
				size_t rem_size = remaining;
				block_header_t* new_block = (block_header_t*)((char*)hdr + need);
				blk_set_size(new_block, rem_size, true, false);
				new_block->prev_size = need;

				bool pf = blk_prev_free(hdr);
				blk_set_size(hdr, need, false, pf);

				/* same as the shrink path, keeping new_block's metadata as is and
				 * just updating the physical successor of the new free remainder --axiss */
				block_header_t* next2 = blk_next(new_block);
				next2->prev_size = rem_size;
				next2->size |= BLOCK_FLAG_PREV_FREE;

				int nfl, nsl;
				tlsf_mapping(rem_size, &nfl, &nsl);
#ifdef __XENEVA_DEBUG_ALLOC__
				/* Absorbed next's header is now inside the grown user
				 * area: 0xAA-fill the whole extension, move the back
				 * redzone, poison the split remainder. */
				tlsf_dbg(hdr)->user_size = (uint32_t)new_req;
				memset((char*)hdr + TLSF_DBG_USER_OFF + old_user,
					TLSF_POISON_FRESH, new_req - old_user);
				memset(tlsf_dbg_back_rz(hdr, (uint32_t)new_req),
					TLSF_RZ_PATTERN, TLSF_RZ_SIZE);
				memset((char*)new_block + TLSF_HEADER_SIZE, TLSF_POISON_FREE,
					rem_size - TLSF_HEADER_SIZE);
#endif
				tlsf_insert_free_block(pool, (free_block_t*)new_block, nfl, nsl);

				pool->used_size += (need - old_size);
				return ptr;
			} else {
				/* Absorb entire next block, no split */
				size_t combined = old_size + next_sz;
				bool pf = blk_prev_free(hdr);
				blk_set_size(hdr, combined, false, pf);

				block_header_t* next2 = blk_next(hdr);
				next2->prev_size = combined;
				next2->size &= ~BLOCK_FLAG_PREV_FREE;

#ifdef __XENEVA_DEBUG_ALLOC__
				tlsf_dbg(hdr)->user_size = (uint32_t)new_req;
				memset((char*)hdr + TLSF_DBG_USER_OFF + old_user,
					TLSF_POISON_FRESH, new_req - old_user);
				memset(tlsf_dbg_back_rz(hdr, (uint32_t)new_req),
					TLSF_RZ_PATTERN, TLSF_RZ_SIZE);
#endif
				pool->used_size += (combined - old_size);
				return ptr;
			}
		}
	}

	/* Fallback: allocate new, copy, free old. tlsf_malloc() does its own
	 * header-size accounting, so pass the original payload size, not `need`.
	 Learn the hard way, ig. */
	void* new_ptr = tlsf_malloc(pool, size);
	if (new_ptr) {
#ifdef __XENEVA_DEBUG_ALLOC__
		/* Copy user bytes only: the flag-off formula (block minus
		 * header) would over-copy into the new redzone. */
		size_t copy_size = old_user < new_req ? old_user : new_req;
#else
		size_t old_usr = old_size - TLSF_HEADER_SIZE;
		size_t new_usr = need - TLSF_HEADER_SIZE;
		size_t copy_size = (old_usr < new_usr) ? old_usr : new_usr;
#endif
		if (copy_size > 0)
			memcpy(new_ptr, ptr, copy_size);
	}
	if (new_ptr)
		tlsf_free(pool, ptr);
	return new_ptr;
}

#ifdef __XENEVA_DEBUG_ALLOC__
/* ---- Leak dump: walk every grafted region block-by-block, group live
 * blocks by caller. No background scanner, no tracking table: grouping
 * uses a small fixed table refilled on every call. Read-only; the heap
 * lock must be held by the caller. */
#define TLSF_DBG_MAX_CALLERS 64
#define TLSF_DBG_TOP_N 8

void tlsf_leak_dump(tlsf_pool_t* pool) {
	static struct { void* caller; uint64_t count, bytes; } groups[TLSF_DBG_MAX_CALLERS];
	unsigned ngroups = 0;
	uint64_t live_blocks = 0, live_bytes = 0;

	if (!pool)
		return;
	for (unsigned r = 0; r < tlsf_dbg_region_count; ++r) {
		if (tlsf_dbg_regions[r].pool != pool)
			continue;
		char* base = (char*)tlsf_dbg_regions[r].base;
		char* end = base + tlsf_dbg_regions[r].size;
		/* Skip the start sentinel; stop before the end sentinel. A
		 * corrupt size aborts the walk instead of running off. */
		block_header_t* hdr = (block_header_t*)(base + TLSF_SENTINEL_SIZE);
		for (unsigned iter = 0; iter < (1u << 20); ++iter) {
			if ((char*)hdr + TLSF_HEADER_SIZE + TLSF_SENTINEL_SIZE > end)
				break;
			size_t sz = blk_size(hdr);
			if (sz < TLSF_MIN_BLOCK_SIZE || (char*)hdr + sz > end)
				break;
			if (!blk_is_free(hdr)) {
				tlsf_dbg_hdr_t* dbg = tlsf_dbg(hdr);
				uint32_t usz = tlsf_dbg_sane_user(hdr, dbg->user_size);
				unsigned i;
				for (i = 0; i < ngroups; ++i)
					if (groups[i].caller == dbg->caller)
						break;
				if (i == ngroups && ngroups < TLSF_DBG_MAX_CALLERS) {
					groups[ngroups].caller = dbg->caller;
					groups[ngroups].count = 0;
					groups[ngroups].bytes = 0;
					++ngroups;
				}
				if (i < ngroups) {
					++groups[i].count;
					groups[i].bytes += usz;
				}
				++live_blocks;
				live_bytes += usz;
			}
			hdr = (block_header_t*)((char*)hdr + sz);
		}
	}

	UARTDebugOut("[tlsf]: leak dump: %d live blocks, %d user bytes\r\n",
		live_blocks, live_bytes);
	/* Top-N by bytes, tiny selection: caller groups are few. */
	for (unsigned top = 0; top < TLSF_DBG_TOP_N && top < ngroups; ++top) {
		unsigned best = top;
		for (unsigned i = top + 1; i < ngroups; ++i)
			if (groups[i].bytes > groups[best].bytes)
				best = i;
		if (best != top) {
			void* c = groups[top].caller;
			uint64_t n = groups[top].count, b = groups[top].bytes;
			groups[top] = groups[best];
			groups[best].caller = c;
			groups[best].count = n;
			groups[best].bytes = b;
		}
		if (!groups[top].caller && !groups[top].count)
			break;
		UARTDebugOut("[tlsf]: #%d caller=%x blocks=%d bytes=%d\r\n",
			top, groups[top].caller, groups[top].count, groups[top].bytes);
	}
}
#endif /* __XENEVA_DEBUG_ALLOC__ */
