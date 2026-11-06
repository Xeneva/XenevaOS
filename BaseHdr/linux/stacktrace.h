/*
 * DCL stacktrace.h -- stack_trace_save() records nothing.
 *
 * drm_mm.c/drm_modeset_lock.c ask for call stacks when debug printing;
 * returning 0 entries makes their dump loops print an empty stack, which is
 * the honest answer without CONFIG_STACKTRACE.
 */
#ifndef _LINUX_STACKTRACE_H
#define _LINUX_STACKTRACE_H

#include <linux/types.h>

struct stack_trace {
	unsigned int nr_entries;
	unsigned int max_entries;
	unsigned long *entries;
	int skip;
};

static inline unsigned int stack_trace_save(unsigned long *entries,
					    unsigned int size,
					    unsigned int skip)
{
	(void)entries; (void)size; (void)skip;
	return 0;
}

static inline int stack_trace_snprint(char *buf, size_t size,
				      const unsigned long *entries,
				      unsigned int nr_entries,
				      unsigned int skip)
{
	(void)buf; (void)size; (void)entries; (void)nr_entries; (void)skip;
	return 0;
}

#endif /* _LINUX_STACKTRACE_H */
