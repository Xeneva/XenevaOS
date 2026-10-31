/*
 * DCL/linux_idr_shim.c -- the idr operations over idr.h's slot table.
 *
 * mainline implements these in lib/radix-tree.c over a tree; DCL's struct idr
 * (BaseHdr/linux/idr.h) is 64 slots and a base, so each operation is a walk of
 * that array.  The drm core is the only consumer: GEM handle allocation
 * (drm_gem.c:499/:1051), object names (:968), mode_config tile ids
 * (drm_connector.c:3627), lease ids and auth magic (drm_auth.c).  What the
 * differences from mainline cost, spelled out:
 *
 *   - capacity: 64 ids per idr, base included.  Exhaustion returns -ENOSPC,
 *     which every caller already handles (it is mainline's exhaustion answer
 *     too); it just arrives ~2^30 ids sooner.
 *   - idr_preload()/idr_preload_end(): mainline stages a radix node on this
 *     CPU so a GFP_NOWAIT alloc inside a spinlock cannot fail.  Here the
 *     alloc inside drm_gem.c's locked region cannot fail at all -- slots are
 *     in the struct -- so the pair brackets nothing, and idr_preload_end()
 *     stays mainline's static inline (idr.h:189) while preload itself is the
 *     extern mainline declares (idr.h:113) with an empty body.
 *   - idr_find() under rcu_read_lock(): single-threaded, so the lockless
 *     lookup mainline documents is simply a lookup.
 *   - reserved ids: mainline's idr_alloc() can hand out an id whose value is
 *     NULL -- drm reserves connector ids and GEM handles that way and fills
 *     the object in later via idr_replace().  The slot table keeps "taken"
 *     and "what it holds" apart (idr.h's `used` and `slots`), so a
 *     reservation is neither handed out twice nor invisible to
 *     idr_replace().
 */
#include <linux/idr.h>

void idr_preload(gfp_t gfp)
{
	(void)gfp;
}

/*
 * idr_alloc(): first unreserved slot at or above `start`, clamped to this
 * idr's base.  end == 0 is mainline's "no upper bound", which here means the
 * last slot; a bounded end (drm_gem.c:1051 allocates exactly new_handle) is
 * honoured as the exclusive limit mainline spells it.  The slot is reserved
 * whether or not `ptr` is NULL -- mainline's contract too, and drm leans on
 * the NULL half of it (drm_gem.c:499, connector ids).
 */
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp)
{
	unsigned int id;
	unsigned int last;

	(void)gfp;
	id = (start > (int)idr->base) ? (unsigned int)start : idr->base;
	if (end > 0 && (unsigned int)end < idr->base + DCL_IDR_SLOTS)
		last = (unsigned int)end;
	else
		last = idr->base + DCL_IDR_SLOTS;

	for (; id < last; id++) {
		if (!idr->used[id - idr->base]) {
			idr->slots[id - idr->base] = ptr;
			idr->used[id - idr->base] = 1;
			return (int)id;
		}
	}
	return -28; /* -ENOSPC */
}

void *idr_remove(struct idr *idr, unsigned long id)
{
	void *old;

	if (id < idr->base || id >= idr->base + DCL_IDR_SLOTS)
		return NULL;
	if (!idr->used[id - idr->base])
		return NULL;
	old = idr->slots[id - idr->base];
	idr->slots[id - idr->base] = NULL;
	idr->used[id - idr->base] = 0;
	return old;
}

void *idr_find(const struct idr *idr, unsigned long id)
{
	if (id < idr->base || id >= idr->base + DCL_IDR_SLOTS)
		return NULL;
	return idr->slots[id - idr->base];
}

void *idr_replace(struct idr *idr, void *ptr, unsigned long id)
{
	void *old;

	if (id < idr->base || id >= idr->base + DCL_IDR_SLOTS)
		return NULL;
	if (!idr->used[id - idr->base])
		return NULL;
	old = idr->slots[id - idr->base];
	idr->slots[id - idr->base] = ptr;
	return old;
}

void *idr_get_next(struct idr *idr, int *nextid)
{
	unsigned int id = (*nextid > (int)idr->base) ? (unsigned int)*nextid
						     : idr->base;

	for (; id < idr->base + DCL_IDR_SLOTS; id++) {
		if (idr->slots[id - idr->base]) {
			*nextid = (int)id;
			return idr->slots[id - idr->base];
		}
	}
	*nextid = (int)(idr->base + DCL_IDR_SLOTS);
	return NULL;
}

int idr_for_each(const struct idr *idr,
		 int (*fn)(int id, void *p, void *data), void *data)
{
	unsigned int id;

	for (id = 0; id < DCL_IDR_SLOTS; id++) {
		int ret;

		if (!idr->slots[id])
			continue;
		ret = fn((int)(idr->base + id), idr->slots[id], data);
		if (ret)
			return ret;
	}
	return 0;
}

void idr_destroy(struct idr *idr)
{
	int i;

	/* Stored pointers are the callers' (drm frees objects itself); this
	 * only drops the table, as mainline's does. */
	for (i = 0; i < DCL_IDR_SLOTS; i++) {
		idr->slots[i] = NULL;
		idr->used[i] = 0;
	}
}
