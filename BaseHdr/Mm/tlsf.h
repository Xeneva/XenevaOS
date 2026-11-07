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

#ifndef _TLSF_H_
#define _TLSF_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- TLSF configuration ---- */
#define TLSF_ALIGN_SIZE        16
#define TLSF_ALIGN_MASK        (TLSF_ALIGN_SIZE - 1)
#define TLSF_ALIGN_UP(x)       (((x) + TLSF_ALIGN_MASK) & ~TLSF_ALIGN_MASK)

/* Two-level bitmap parameters */
#define SL_INDEX_COUNT_LOG2    5
#define SL_INDEX_COUNT         (1U << SL_INDEX_COUNT_LOG2)  /* 32  */
#define SL_INDEX_MASK          (SL_INDEX_COUNT - 1)

/* First-level bitmap: one bit per power-of-two range */
#define FL_INDEX_COUNT         (8 * sizeof(size_t))

/* ---- Block header layout ---- */
/* Block header: 2 × size_t = 16 bytes on 64-bit.
 * This gives 16-byte alignment for the user payload
 * (payload starts at block_start + 16, which is 16-aligned
 * when block_start itself is 16-aligned).
 */
#define BLOCK_FLAG_FREE       0x1U
#define BLOCK_FLAG_PREV_FREE  0x2U
#define BLOCK_SIZE_MASK       (~(size_t)(BLOCK_FLAG_FREE | BLOCK_FLAG_PREV_FREE))

typedef struct block_header {
    size_t size;       /* bit 0 = free, bit 1 = prev-free; rest = block size (incl. header) */
    size_t prev_size;  /* size of the previous physical block (for O(1) backward coalescing) */
} block_header_t;

/* Free block: header (16) + doubly-linked-list pointers (16) = 32 bytes minimum.
 * The payload of a free block starts at offset 16 (= next_free pointer).
 */
typedef struct free_block {
    block_header_t hdr;
    struct free_block *next_free;
    struct free_block *prev_free;
} free_block_t;

/* Sizes */
#define TLSF_HEADER_SIZE       ((size_t)sizeof(block_header_t))  /* 16 */
#define TLSF_FREE_BLOCK_SIZE   ((size_t)sizeof(free_block_t))     /* 32 */
#define TLSF_MIN_BLOCK_SIZE    TLSF_FREE_BLOCK_SIZE              /* 32 — minimum free block */
#define TLSF_SENTINEL_SIZE     TLSF_HEADER_SIZE                 /* 16 — sentinel block */

/* ---- TLSF pool / control structure ---- */
typedef struct tlsf_pool {
    uint64_t  fl_bitmap;
    uint32_t  sl_bitmap[FL_INDEX_COUNT];
    free_block_t *blocks[FL_INDEX_COUNT][SL_INDEX_COUNT];
    size_t    pool_size;
    size_t    used_size;
} tlsf_pool_t;

/* ---- Public API ---- */

/* Initialize a TLSF pool.  Returns a pointer to a static pool, or NULL on failure. */
tlsf_pool_t *tlsf_create(void);

/* Return the current global pool (for internal use). */
tlsf_pool_t *tlsf_get_pool(void);

/* Add a contiguous memory region to the pool.
 * Returns 0 on success, -1 on failure.
 */
int tlsf_add_memory(tlsf_pool_t *pool, void *mem, size_t size);

/* Allocate `size` bytes of 16-byte-aligned memory.
 * Returns NULL when the pool is exhausted (caller should grow the pool).
 */
void *tlsf_malloc(tlsf_pool_t *pool, size_t size);

/* Free a previously-allocated block. */
void tlsf_free(tlsf_pool_t *pool, void *ptr);

/* Resize an allocation (in-place shrink / grow-or-move). */
void *tlsf_realloc(tlsf_pool_t *pool, void *ptr, size_t size);

/* Statistics */
static inline size_t tlsf_used(tlsf_pool_t *pool) { return pool ? pool->used_size : 0; }
static inline size_t tlsf_total(tlsf_pool_t *pool) { return pool ? pool->pool_size : 0; }

#ifdef __XENEVA_DEBUG_ALLOC__
/* ---- Stage 2: debug-only block layout ----
 * Live block: [tlsf hdr 16][dbg hdr 16][front RZ 16][user N][back RZ 16].
 * The user pointer stays 16-aligned (48 = 3 x 16). Free blocks keep the
 * stock layout; all translation happens at the API boundary, so every
 * internal helper (coalescing, buckets, walking) is untouched.
 * Everything in this block vanishes when the flag is off. */
#define TLSF_RZ_SIZE           16
#define TLSF_RZ_PATTERN        0xFCu
#define TLSF_POISON_FREE       0x6Bu
#define TLSF_POISON_FRESH      0xAAu
#define TLSF_DBG_HDR_SIZE      ((size_t)sizeof(tlsf_dbg_hdr_t))
#define TLSF_DBG_OVERHEAD      (TLSF_DBG_HDR_SIZE + 2 * TLSF_RZ_SIZE)

typedef struct tlsf_dbg_hdr {
	void    *caller;    /* __builtin_return_address(1): kmalloc's caller */
	uint32_t seq;       /* allocation sequence number */
	uint32_t user_size; /* requested payload bytes (RZ/poison bounds) */
} tlsf_dbg_hdr_t;

/* Walk every region grafted into the pool, group live blocks by caller,
 * print the top offenders. Read-only; caller must hold the heap lock. */
void tlsf_leak_dump(tlsf_pool_t *pool);

/* Initialise a caller-provided pool object, independent of the global
 * pool. Used by the debug self-tests so deliberate corruption never
 * touches the live heap. */
tlsf_pool_t *tlsf_create_at(tlsf_pool_t *pool);

/* Cumulative detector reports (redzone / poison / double-free). Tests
 * assert this moves; operators watch it via the debugger. */
uint64_t tlsf_dbg_violation_count(void);

/* Caller hint side-channel (see tlsf.c): the public wrappers stash
 * their own return address (frame 0: always safe) before entering the
 * backend, which stamps it into the block. No signature changes. */
void tlsf_dbg_hint_set(void *caller);
#endif /* __XENEVA_DEBUG_ALLOC__ */

#ifdef __cplusplus
}
#endif

#endif /* _TLSF_H_ */
