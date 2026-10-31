/*
 * DCL stackdepot.h -- lockdep/debug backtraces that are never kept.
 *
 * drm_mm.c (CONFIG_DRM_DEBUG_MM) and drm_modeset_lock.c save stacks into
 * depot handles when debugging allocations; the store is a fixed handle of
 * 0 and the fetch reports an empty stack, so dump paths print "no stack"
 * and nothing is allocated.
 */
#ifndef _LINUX_STACKDEPOT_H
#define _LINUX_STACKDEPOT_H

#include <linux/types.h>
#include <linux/gfp.h>

typedef u32 depot_stack_handle_t;

static inline depot_stack_handle_t stack_depot_save(unsigned long *entries,
						    unsigned int nr_entries,
						    gfp_t gfp_flags)
{
	(void)entries; (void)nr_entries; (void)gfp_flags;
	return 0;
}

static inline void stack_depot_init(void) { }

static inline int stack_depot_fetch(depot_stack_handle_t handle,
				    unsigned long ***entries)
{
	(void)handle;
	*entries = 0;
	return 0;
}

static inline int stack_depot_snprint(depot_stack_handle_t handle, char *buf,
				      size_t size, unsigned int skip)
{
	(void)handle; (void)buf; (void)size; (void)skip;
	return 0;
}

static inline void stack_depot_print(depot_stack_handle_t handle)
{
	(void)handle;
}

#endif /* _LINUX_STACKDEPOT_H */
