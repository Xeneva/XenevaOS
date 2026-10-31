#ifndef __ASM_BARRIER_H__
#define __ASM_BARRIER_H__

/*
 * DCL <asm/barrier.h> -- memory-barrier primitives.
 *
 * DCL keeps the ordering primitives in <linux/kernel.h> (READ_ONCE,
 * WRITE_ONCE, smp_load_acquire, smp_store_release, smp_{mb,rmb,wmb}) because
 * that is where the tty/serial ports pick them up, and every one of them is
 * guarded. This header exists so that a mainline source can include it the
 * way mainline does -- virtio-rng.c:7 is `#include <asm/barrier.h>` and it is
 * the first include in the file -- without needing to know which of the two
 * places a given primitive happens to live in.
 *
 * What kernel.h does NOT have is the compiler-only barrier and the plain
 * {mb,rmb,wmb} family, so those are defined here.
 *
 * `barrier()` is deliberately a *compiler* fence and not a hardware one.
 * mainline's asm-generic version is an empty asm with a "memory" clobber: its
 * job is to stop the compiler from moving a load or store across the point in
 * the source, and it must not cost an instruction. __atomic_signal_fence()
 * states exactly that -- signal-scope, i.e. compiler-only -- and works on a
 * target where an empty `__asm__` template would be a question mark. A
 * hardware ordering guarantee is smp_mb()'s job, in kernel.h.
 */

#include <linux/kernel.h>

#ifndef barrier
#define barrier()			__atomic_signal_fence(__ATOMIC_SEQ_CST)
#endif

/*
 * mb/rmb/wmb are the plain, unconditional hardware barriers. DCL is a
 * UP-ish kernel with a strong ordering habit: everything that needs one of
 * these is on the device-facing side (the virtio used-ring read below is the
 * current user), and getting a relaxed fence wrong there costs a torn
 * descriptor, so all three map to the sequential-consistency fence rather
 * than to a cheaper acquire/release pair. If a port ever needs the weaker
 * form, it should say so at the call site.
 */
#ifndef mb
#define mb()				__atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif
#ifndef rmb
#define rmb()				__atomic_thread_fence(__ATOMIC_ACQUIRE)
#endif
#ifndef wmb
#define wmb()				__atomic_thread_fence(__ATOMIC_RELEASE)
#endif

/*
 * dma_rmb()/dma_wmb() order DMA-visible memory. Xeneva's virtio rings are
 * ordinary memory in the linear map (AuVirtioPCISetupQueue hands back
 * pointers, not bus addresses needing an explicit sync), so these are the
 * same fences -- the name is kept so a ported driver reads the way it does
 * upstream.
 */
#ifndef dma_rmb
#define dma_rmb()			__atomic_thread_fence(__ATOMIC_ACQUIRE)
#endif
#ifndef dma_wmb
#define dma_wmb()			__atomic_thread_fence(__ATOMIC_RELEASE)
#endif

#endif /* __ASM_BARRIER_H__ */
