#ifndef __LINUX_IDR_H
#define __LINUX_IDR_H

/*
 * DCL <linux/idr.h> -- struct ida and the small allocation API.
 *
 * virtio-rng.c uses exactly two things from it:
 *
 *     static DEFINE_IDA(rng_index_ida);
 *     vi->index = ida_alloc(&rng_index_ida, GFP_KERNEL);
 *     ...
 *     ida_free(&rng_index_ida, index);
 *
 * the index being used to name the device ("virtio-<n>"). mainline gets to
 * this header by accident of its include graph -- there is no idr.h among the
 * driver's own includes. The chain, confirmed against the header package:
 *
 *     linux/virtio.h -> linux/device.h -> linux/kobject.h
 *                  -> linux/sysfs.h     -> linux/kernfs.h -> linux/idr.h
 *
 * DCL has no device.h (that chain is why <linux/virtio.h> here also pulls in
 * completion.h and pm.h), so virtio.h includes this file directly instead of
 * walking five headers to reach the same place. The driver's own #include
 * list is therefore untouched, which is what has to stay true.
 *
 * ### struct ida keeps its own counter
 *
 * mainline's is `struct ida { struct xarray xa; }` -- a radix tree of ID
 * bitmaps, because a kernel hands out millions of them. DCL's
 * ida_alloc_range() (linux_kmod_shim.c) is a monotone counter, and the
 * counter lives in this struct so that every ida numbers on its own. Three
 * exist now and their numbering must not mix: virtio-rng's device index,
 * virtio-console's vtermno (min 1), and drm's connector ids -- the
 * per-config connector_ida, whose upper bound is 31, and the per-type idas
 * in drm_connector_enum_list. One static counter in the shim served them all
 * and would eventually have handed connector_ida an id past its own bound;
 * the counter moved in here when the second user showed up.
 *
 * Ids are never reused -- mainline's reuses freed ones -- which costs
 * nothing at DCL's scale: an ida here numbers devices or connectors, a
 * handful per boot.
 */

#include <linux/kernel.h>	/* gfp_t */

struct ida {
	unsigned int next_id;
};

/*
 * struct idr -- DCL's is a fixed slot table; mainline's is a radix tree.
 *
 * The drm core hangs three of these off a device (mode_config object/tile
 * ids, GEM object names) and one off every open file (GEM handles), and all
 * it asks of them is "give me an int in [base, base+N) and give it back as a
 * pointer".  A 64-slot array answers that directly; what it does not answer
 * is the two things mainline's tree buys -- ids beyond 64 at once, and
 * lock-free lookup.  Both are noted where they would bite:
 * idr_alloc() (DCL/linux_idr_shim.c) returns -ENOSPC rather than growing, and
 * rcu_read_lock() is a no-op, so the "lockless idr_find" mainline documents
 * is simply single-threaded here like every other DCL lock.
 *
 * A slot answers two questions -- is the id taken, and what does it hold --
 * and mainline keeps them apart: idr_alloc() may reserve an id and store
 * NULL in it, which drm leans on.  A connector's id is reserved as a NULL
 * entry by __drm_mode_object_add() (register_obj false) and filled in later
 * by drm_mode_object_register() through idr_replace() at connector
 * registration; a GEM handle is reserved the same way in
 * drm_gem_handle_create_tail().  A table of pointers alone cannot tell a
 * reserved NULL entry from a free slot -- it would hand the id out a second
 * time, and idr_replace() would refuse to fill the reservation -- so `used`
 * carries the state and `slots` the value.
 *
 * idr_init_base()/idr_init()/idr_is_empty() are static inline over these
 * fields, mirroring mainline (idr.h:152-181), so a `struct idr` member in
 * drm_file.h or drm_mode_config.h is complete without a shim call.
 */
#define DCL_IDR_SLOTS 64

struct idr {
	void *slots[DCL_IDR_SLOTS];	/* slot i holds id base + i */
	unsigned char used[DCL_IDR_SLOTS]; /* slot i's id has been reserved */
	unsigned int base;		/* first id this idr may hand out */
};

static inline void idr_init_base(struct idr *idr, int base)
{
	int i;

	for (i = 0; i < DCL_IDR_SLOTS; i++) {
		idr->slots[i] = NULL;
		idr->used[i] = 0;
	}
	idr->base = (base > 0) ? (unsigned int)base : 0;
}

static inline void idr_init(struct idr *idr)
{
	idr_init_base(idr, 0);
}

static inline int idr_is_empty(struct idr *idr)
{
	int i;

	for (i = 0; i < DCL_IDR_SLOTS; i++)
		if (idr->used[i])
			return 0;
	return 1;
}

/*
 * The rest of the API is extern -- mainline's are too (idr.h:113-126) --
 * because the bodies share the slot arithmetic with each other and live in
 * one file.
 */
void idr_preload(gfp_t gfp);
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp);
void *idr_remove(struct idr *idr, unsigned long id);
void *idr_find(const struct idr *idr, unsigned long id);
int idr_for_each(const struct idr *idr,
		 int (*fn)(int id, void *p, void *data), void *data);
void *idr_get_next(struct idr *idr, int *nextid);
void *idr_replace(struct idr *idr, void *ptr, unsigned long id);
void idr_destroy(struct idr *idr);

/*
 * idr_preload_end() -- closes the section idr_preload() opened. mainline's
 * is a static inline that unlocks the per-CPU radix-node cache (idr.h:189);
 * the whole bracket does nothing in DCL because idr_alloc() takes its slot
 * out of the struct itself, so there is nothing an allocation could have
 * failed to find. It stays a static inline so drm_gem.c:502's call costs
 * nothing at all. The long version of why the pair brackets nothing is at
 * the top of DCL/linux_idr_shim.c.
 */
static inline void idr_preload_end(void)
{
}

/*
 * idr_for_each_entry() -- mainline's spelling (idr.h:204-205): idr_get_next()
 * finds the first occupied id at or after *nextid and reports it there, so
 * the loop's `id += 1U` starts the following search one past what it just
 * found, and holes are skipped rather than returned as NULL entries.
 */
#define idr_for_each_entry(idr, entry, id)			\
	for (id = 0; ((entry) = idr_get_next((idr), &(id))) != NULL; id += 1U)

#define DEFINE_IDA(name)	struct ida name = { 0 }

/*
 * Matching DCL/linux_kmod_shim.c's definitions exactly -- same order, same
 * types -- because both now appear in the same translation unit (the shim
 * reaches this through <linux/virtio.h>). A `void*` here against a
 * `struct ida*` there would be a conflicting-declaration error, not a
 * harmless mismatch.
 */
extern int ida_alloc_range(struct ida* ida, unsigned int min,
			   unsigned int max, gfp_t gfp);
extern void ida_free(struct ida* ida, unsigned int id);

/**
 * ida_alloc - allocate an unused ID
 * @ida: the IDA
 * @gfp: allocation flags (unused; the shim's allocator does not allocate)
 *
 * mainline's is a static inline over ida_alloc_range() with min 0 and max at
 * the top of the ID space, so this is the same call with the bound written
 * out. 0x7FFFFFFF rather than INT_MAX to avoid dragging <limits.h> in for one
 * constant; it is INT_MAX on this target (32-bit int) regardless of LP64 or
 * LLP64.
 *
 * Returns the ID, or a negative errno -- -ENOSPC if the range is exhausted.
 * The caller in virtio-rng.c checks `index < 0` and unwinds, which is why the
 * shim's out-of-range path returning -28 has to stay a negative number.
 */
static inline int ida_alloc(struct ida* ida, gfp_t gfp)
{
	return ida_alloc_range(ida, 0, 0x7FFFFFFFu, gfp);
}

/**
 * ida_alloc_min - allocate an unused ID at or above @min
 * @ida: the IDA
 * @min: the lowest ID to hand out
 * @gfp: allocation flags (unused; see ida_alloc() above)
 *
 * mainline's is ida_alloc_range(ida, min, INT_MAX, gfp), spelled here with the
 * same 0x7FFFFFFF ceiling ida_alloc() uses for the same reason -- it is INT_MAX
 * on this target and pulls in no <limits.h>.
 *
 * One caller: virtio_console.c:1215 allocates a vtermno starting at 1 so that
 * console port 0 keeps its traditional number. Returning the same negative
 * errno ida_alloc_range() returns on exhaustion is the half of the contract
 * that matters: the caller checks `< 0` and unwinds.
 */
static inline int ida_alloc_min(struct ida* ida, unsigned int min, gfp_t gfp)
{
	return ida_alloc_range(ida, min, 0x7FFFFFFFu, gfp);
}

/*
 * ida_alloc_max() -- mainline idr.h:327's third bound: [0, max] instead of
 * [min, INT_MAX]. One caller, drm_connector.c:249, which asks the per-config
 * connector_ida for an id no larger than 31, so the upper bound is real and
 * not just mainline's usual ~0. The shim's range check answers -ENOSPC past
 * it, which is also mainline's exhaustion errno, and the caller's
 * `if (ret < 0) return ret;` takes it the same way.
 */
static inline int ida_alloc_max(struct ida* ida, unsigned int max, gfp_t gfp)
{
	return ida_alloc_range(ida, 0, max, gfp);
}

/*
 * ida_init()/ida_destroy() -- mainline's pair for the xarray the ids live in
 * (idr.h:332, and lib/idr.c's teardown). DCL's struct ida has no state: the
 * numbering is the shim's static counter (linux_kmod_shim.c), which
 * ida_free() deliberately does not wind back, so there is nothing to
 * initialise and nothing to release. The bodies are empty rather than
 * `ida->placeholder = 0` because the member this header documents as
 * "documentation, not state" must not start being written by the one pair
 * that would make it look load-bearing. drm_mode_config.c:455/:599 and
 * drm_connector.c:122/:130 are the callers.
 */
static inline void ida_init(struct ida* ida)
{
	(void)ida;
}

static inline void ida_destroy(struct ida* ida)
{
	(void)ida;
}

#endif /* __LINUX_IDR_H */
