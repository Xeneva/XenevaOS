/*
 * DCL/current_ident.c -- the native half of dcl_current().
 *
 * This file exists because the two list APIs cannot share a translation
 * unit: <Hal/AA64/sched.h> includes Xeneva's <list.h>, whose
 * list_add(list_t*, void*) collides with mainline's static inline
 * list_add(struct list_head*, struct list_head*) -- see the landmine note
 * in <linux/list.h>.  The consumer (dcl_current(), in linux_irq_shim.c)
 * needs mainline's list for its wait queues *and* this header's thread
 * layout for a truthful comm, so the layout stays on this side and crosses
 * the boundary as three scalars.
 *
 * The name comes from the thread (AA64Thread.name, 8 bytes) rather than
 * its process (AuProcess.name, 16) because <process.h> would drag the same
 * <list.h> along, and the thread answers for the context that actually
 * asked.  Eight characters of a real name beats a fabricated one, and the
 * truncation is visible in the warning text rather than hidden.
 *
 * The scan stops at the field's own bound: name[] is not guaranteed to be
 * NUL-terminated, so reading a longer span would walk into the next member.
 *
 * Returns the number of name bytes copied (0 when the thread is unnamed,
 * which leaves the caller's buffer alone), or -1 when there is no current
 * thread at all; *pid is written only for the latter two being absent.
 */
#include <string.h>
#include <Hal/AA64/sched.h>

int dcl_current_ident(char* buf, int cap, int* pid)
{
	AA64Thread* thread = AuGetCurrentThread();
	int n = 0;

	if (!thread)
		return -1;

	while (n < (int)sizeof(thread->name) && n < cap - 1 && thread->name[n])
		n++;
	if (n > 0) {
		memcpy(buf, thread->name, n);
		buf[n] = 0;
	}
	*pid = (int)thread->thread_id;
	return n;
}
