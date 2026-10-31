/**
 * BSD 2-Clause License
 *
 * Copyright (c) 2022 - 2023, Manas Kamal Choudhury
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

#include <Mm/kmalloc.h>
#include <Mm/tlsf.h>
#include <stdint.h>
#include <string.h>
#include <_null.h>
#if defined(__GNUC__) || defined(__clang__)
#include <stdbool.h>
#endif
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Sync/spinlock.h>
#include <aucon.h>
#include <Hal/AA64/aa64lowlevel.h>

/* ---- Brk pointer for the TLSF pool ---- */
static uint64_t _brk_current = KERNEL_BASE_ADDRESS;

/* ---- TLSF global pool and lock ---- */
static tlsf_pool_t* g_kheap = NULL;
static Spinlock* g_heap_lock = NULL;

/* ---- Page-level allocation (brk-style) ---- */
void* au_request_page(int pages) {
	if (pages <= 0)
		return NULL;

	uint64_t page_addr = _brk_current;

	for (int i = 0; i < pages; i++) {
		void* p = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		AuMapPage((uint64_t)(size_t)p, page_addr + (size_t)i * PAGE_SIZE, PTE_NORMAL_MEM);
	}

	_brk_current += (uint64_t)pages * PAGE_SIZE;

	return (void*)page_addr;
}
/*
 * au_free_page frees up contiguous pages
 * @ptr starting virtual address
 * @pages number of pages
 */
int au_free_page(void* ptr, int pages) {
	if (!ptr || pages <= 0)
		return -1;

	AuFreePages((uint64_t)(size_t)ptr, true, (size_t)pages * PAGE_SIZE);
	return 0;
}

/* ---- Kernel heap (TLSF) ---- */

void AuHeapInitialize() {
	tlsf_pool_t* pool = tlsf_create();
	g_kheap = pool;

	/* Allocate initial heap region: 128 pages = 512 KiB */
	size_t initial_pages = 128;
	void* heap_mem = au_request_page(initial_pages);
	if (!heap_mem) {
		AuTextOut("[kmalloc]: failed to allocate initial heap memory\r\n");
		return;
	}

	/* Register the region with TLSF */
	if (tlsf_add_memory(pool, heap_mem, initial_pages * PAGE_SIZE) != 0) {
		AuTextOut("[kmalloc]: tlsf_add_memory failed\r\n");
		return;
	}

	/* Create spinlock for SMP safety (early = no kmalloc dependency) */
	g_heap_lock = AuCreateSpinlock(true);
	if (!g_heap_lock) {
		AuTextOut("[kmalloc]: failed to create spinlock, using no lock\r\n");
	}
	AuTextOut("[kmalloc]: TLSF heap initialized, %d pages\r\n", initial_pages);

	/* Stage 1 self-test: the global heap-bytes counter (pool->used_size,
	 * exposed via tlsf_used) must rise on alloc and return to baseline
	 * on free. Always-on accounting, verified every boot. */
	{
		size_t heap_before = tlsf_used(g_kheap);
		void* heap_probe = kmalloc(64);
		size_t heap_during = tlsf_used(g_kheap);
		kfree(heap_probe);
		size_t heap_after = tlsf_used(g_kheap);
		/* NOTE: AuTextOut implements %d (size_t-wide) but not %llu/%zu;
		 * unknown specifiers don't consume args and misalign the rest. */
		AuTextOut("[kmalloc]: counter self-test before=%d during=%d after=%d %s\r\n",
			heap_before,
			heap_during,
			heap_after,
			(heap_probe && heap_during > heap_before && heap_after == heap_before) ?
				"PASS" : "FAIL");
	}
#ifdef __XENEVA_DEBUG_ALLOC__
	/* Stage 2 deliberate-bug tests: overflow, underflow, use-after-free,
	 * double-free, leak grouping. Runs on a scratch pool, so the live
	 * heap is untouched no matter what the tests corrupt. */
	AuAllocDebugTest();
#endif
}

/* ---- Public kernel allocator API ---- */

void* kmalloc(unsigned int size) {
	if (!g_kheap || !size)
		return NULL;

#ifdef __XENEVA_DEBUG_ALLOC__
	/* Stash our return address for the backend's caller tracking.
	 * kcalloc delegates here, so its blocks group under kcalloc. */
	tlsf_dbg_hint_set(__builtin_return_address(0));
#endif
	/* The heap spinlock does not mask IRQs. A timer tick in the middle of
	 * tlsf_malloc/free can schedule another thread that also kmallocs, or
	 * an IRQ path can re-enter TLSF, and the free-list walks off into
	 * payload bytes (the "ctrl.exe" next_free smash). Save DAIF and keep
	 * the allocator atomic on this CPU. --axiss */
	uint64_t daif = read_daif();
	mask_irqs();
	AuAcquireSpinlock(g_heap_lock);
	void* ptr = tlsf_malloc(g_kheap, size);
	AuReleaseSpinlock(g_heap_lock);
	restore_daif(daif);

	if (!ptr) {
		/* Pool exhausted, grow by 32 pages (128 KiB) and retry. This has to
		 * happen outside the lock/mask above: au_request_page walks the
		 * physical allocator and page tables once per page, which is slow
		 * enough (under QEMU/TCG especially) that doing it with IRQs
		 * masked was stalling the *entire system's* timer tick for several
		 * ms every time the heap grew -- visible as system-wide stutter,
		 * not just a slow allocation. Only the actual free-list surgery
		 * (tlsf_add_memory/tlsf_malloc) needs the lock+mask --axiss */
		size_t need_pages = ((size_t)size + PAGE_SIZE - 1) / PAGE_SIZE;
		size_t more_pages = 32;
		/* The added region must by itself be able to satisfy the request:
		 * a 256 KiB receive buffer can never come out of a 128 KiB growth
		 * chunk, so the retry below would fail forever. Leave a few pages
		 * for TLSF's own block header and alignment. */
		if (need_pages + 8 > more_pages)
			more_pages = need_pages + 8;
		void* more_mem = au_request_page(more_pages);
		if (more_mem) {
			uint64_t daif2 = read_daif();
			mask_irqs();
			AuAcquireSpinlock(g_heap_lock);
			tlsf_add_memory(g_kheap, more_mem, more_pages * PAGE_SIZE);
			ptr = tlsf_malloc(g_kheap, size);
			AuReleaseSpinlock(g_heap_lock);
			restore_daif(daif2);
		}
	}

	return ptr;
}

void kfree(void* ptr) {
	if (!ptr || !g_kheap)
		return;

#ifdef __XENEVA_DEBUG_ALLOC__
	tlsf_dbg_hint_set(__builtin_return_address(0));
#endif
	uint64_t daif = read_daif();
	mask_irqs();
	AuAcquireSpinlock(g_heap_lock);
	tlsf_free(g_kheap, ptr);
	AuReleaseSpinlock(g_heap_lock);
	restore_daif(daif);
}

void* krealloc(void* ptr, unsigned int new_size) {
	if (!g_kheap)
		return NULL;

#ifdef __XENEVA_DEBUG_ALLOC__
	tlsf_dbg_hint_set(__builtin_return_address(0));
#endif
	uint64_t daif = read_daif();
	mask_irqs();
	AuAcquireSpinlock(g_heap_lock);
	void* result = tlsf_realloc(g_kheap, ptr, new_size);
	AuReleaseSpinlock(g_heap_lock);
	restore_daif(daif);

	if (!result && new_size > 0) {
		/* see kmalloc: grow outside the lock/mask, only the free-list
		 * surgery needs it --axiss */
		size_t need_pages = ((size_t)new_size + PAGE_SIZE - 1) / PAGE_SIZE;
		size_t more_pages = 32;
		if (need_pages + 8 > more_pages)
			more_pages = need_pages + 8;
		void* more_mem = au_request_page(more_pages);
		if (more_mem) {
			uint64_t daif2 = read_daif();
			mask_irqs();
			AuAcquireSpinlock(g_heap_lock);
			tlsf_add_memory(g_kheap, more_mem, more_pages * PAGE_SIZE);
			result = tlsf_realloc(g_kheap, ptr, new_size);
			AuReleaseSpinlock(g_heap_lock);
			restore_daif(daif2);
		}
	}

	return result;
}

void* kcalloc(size_t n_item, size_t size) {
	if (!g_kheap || !n_item || !size)
		return NULL;

	size_t total = n_item * size;
	void* ptr = kmalloc(total);
	if (ptr)
		memset(ptr, 0, total);
	return ptr;
}

void kheap_debug() {
	if (!g_kheap)
		return;
	AuTextOut("[kmalloc]: pool=%d, used=%d\r\n", tlsf_total(g_kheap), tlsf_used(g_kheap));
}

#ifdef __XENEVA_DEBUG_ALLOC__
void kheap_leak_dump() {
	if (!g_kheap)
		return;
	uint64_t daif = read_daif();
	mask_irqs();
	AuAcquireSpinlock(g_heap_lock);
	tlsf_leak_dump(g_kheap);
	AuReleaseSpinlock(g_heap_lock);
	restore_daif(daif);
}
#endif

void kmalloc_debug_on(bool bit) {
	(void)bit;
}
