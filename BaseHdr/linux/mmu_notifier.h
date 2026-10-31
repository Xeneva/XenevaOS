/*
 * DCL mmu_notifier.h -- invalidation lockdep maps only.
 *
 * dma-resv.c wraps its mmu-interval hooks in lock_map_acquire/release on
 * mainline; DCL's lockdep.h has no lock_map_* macros, so the stub header
 * that supplies them is this one (the sole includer).  The arg is dropped,
 * which also makes mainline's __mmu_notifier_*_map variables unreferenced.
 */
#ifndef _LINUX_MMU_NOTIFIER_H
#define _LINUX_MMU_NOTIFIER_H

#define lock_map_acquire(l)		do { (void)(l); } while (0)
#define lock_map_acquire_try(l)		do { (void)(l); } while (0)
#define lock_map_release(l)		do { (void)(l); } while (0)

struct mmu_interval_notifier;

#endif /* _LINUX_MMU_NOTIFIER_H */
