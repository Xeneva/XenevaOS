/*
 * DCL kgdb.h -- in_dbg_master() is false.
 *
 * drm_util.h's drm_can_sleep() checks it before allowing a "can sleep"
 * answer; DCL has no kgdb stub, so the debugger is never "master" and the
 * remaining conditions (atomic context, irqs off) decide alone.
 */
#ifndef _LINUX_KGDB_H
#define _LINUX_KGDB_H

static inline int in_dbg_master(void)
{
	return 0;
}

#endif /* _LINUX_KGDB_H */
