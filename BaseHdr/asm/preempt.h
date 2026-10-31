/*
 * DCL <asm/preempt.h> -- the preemption counter, on a kernel that does not
 * preempt.
 *
 * mainline's arch/arm64/include/asm/preempt.h keeps the count in
 * current_thread_info()->preempt, which is why its five functions all read
 * and WRITE_ONCE that field.  DCL has no thread_info, no CONFIG_PREEMPT_COUNT
 * and no scheduler path that would ever increment or decrement the count:
 * <linux/preempt.h>:276 puts preempt_disable()/preempt_enable() in the
 * !CONFIG_PREEMPT_COUNT arm, where both are `barrier()`.  So nothing in this
 * image can move the counter, and inventing storage for a value that no code
 * can change would be a number that only looks like evidence.
 *
 * Returning 0 is therefore the truthful reading rather than a placeholder:
 * "not in an interrupt, not in an atomic section, safe to sleep".  Its one
 * consequence worth naming is drm_util.h:64, where drm_can_sleep() is
 * `in_atomic() || in_dbg_master() || irqs_disabled()` -- the first two terms
 * are now always false and the last is a real DAIF read (<linux/irqflags.h>),
 * which is the half of that check DCL can actually answer.  A lock taken from
 * a hardware interrupt will still be caught, because the GIC dispatcher runs
 * with interrupts masked.
 *
 * The mutators are kept because <linux/preempt.h> spells preempt_count_add(),
 * preempt_count_sub() and preempt_count_dec_and_test() as calls to them
 * (:197-:201) and every driver in this tree may use those macros; a missing
 * one would fail in the driver rather than here.  They are no-ops for the
 * same reason preempt_count() is a constant.
 *
 * No current_thread_info(), so no <linux/thread_info.h>: that header needs
 * a per-task struct DCL does not model.
 */
#ifndef __ASM_PREEMPT_H
#define __ASM_PREEMPT_H

/*
 * arm64's need_resched lives above the 8/8/4/4 field layout that
 * <linux/preempt.h>:33-48 carves out of the low 24 bits.  1ULL rather than
 * mainline's BIT(32): this target is aarch64-unknown-windows, which is LLP64,
 * so `1UL` is 32 bits wide and `1UL << 32` would be undefined behaviour
 * rather than a flag.
 */
#define PREEMPT_NEED_RESCHED	(1ULL << 32)
#define PREEMPT_ENABLED		(PREEMPT_NEED_RESCHED)

static inline int preempt_count(void)
{
	return 0;
}

static inline void preempt_count_set(unsigned long long pc)
{
	(void)pc;
}

static inline void set_preempt_need_resched(void)
{
}

static inline void clear_preempt_need_resched(void)
{
}

static inline bool test_preempt_need_resched(void)
{
	return false;
}

static inline void __preempt_count_add(int val)
{
	(void)val;
}

static inline void __preempt_count_sub(int val)
{
	(void)val;
}

static inline bool __preempt_count_dec_and_test(void)
{
	return false;
}

static inline bool should_resched(int preempt_offset)
{
	(void)preempt_offset;
	return false;
}

#endif /* __ASM_PREEMPT_H */
