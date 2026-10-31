/*
 * DCL linux_lock_shim.c -- the spinlock/rwlock entry points that resolve at
 * link time, for the translation units that can only reach them as calls.
 *
 * kernel.h:218 spells spin_lock_init(), spin_lock_irqsave() and
 * spin_unlock_irqrestore() as macros, and that covers every file whose
 * include order has them defined before the call.  It does not cover every
 * file.  virtio_console.c's add_port() and two of tty_port.c's entry points
 * (tty_port_init, tty_port_default_wakeup) use the names with no macro in
 * effect, because the macros do not exist until kernel.h:218 has been
 * parsed, and those files reach kernel.h through virtio.h -> completion.h ->
 * wait.h -- headers parsed ahead of the macro block.  The call the compiler
 * then sees is an ordinary function call, and the link needs the symbol.
 *
 * The rwlock names are a different case with the same answer: DCL has no
 * macro for them anywhere, so every use of rwlock_init(), read_lock(),
 * read_unlock(), write_lock() and write_unlock() -- drm_vma_manager.c and
 * drm_gem.c, over struct drm_vma_offset_manager's vm_lock -- is a call.
 *
 * Neither case can be fixed with a macro.  A macro added to a header would
 * only be seen by files that include that header, which is exactly the set
 * that is already fine; the calling files never reach it.  Functions do not
 * have that reachability problem: the linker does not care who called.
 *
 * assert_spin_locked() is the third shape: mainline only ever defines it as
 * a macro (lockdep.h) or leaves it to the lockdep build, DCL has neither,
 * and drm_vblank*.c and drm_file.c call it.  It is a statement of a
 * guarantee the caller already holds, so it does nothing here, like every
 * other lock operation.
 *
 * This file includes no header, and that is not laziness -- including
 * <linux/spinlock.h> reintroduces the trap from the other side.  wait.h:97
 * calls spin_lock_init() while the macro does not exist yet, so a TU that
 * pulls wait.h in records an implicit `int spin_lock_init()` declaration;
 * a definition below that is a conflicting type, and this file is the one
 * TU where the definition is present.  (Trying it produced exactly
 * "error: conflicting types for 'spin_lock_init'" at the definition,
 * pointing at wait.h:97.)  The typedefs below are kernel.h:43's and
 * spinlock.h:200's, copied: spinlock_t and rwlock_t are `int`, every lock
 * operation here is a statement that no lock exists, and the callers only
 * ever pass addresses.
 */
typedef int spinlock_t;
typedef int rwlock_t;

void spin_lock_init(spinlock_t* lock)
{
	(void)lock;
}

unsigned long spin_lock_irqsave(spinlock_t* lock, unsigned long flags)
{
	(void)lock;
	return flags;
}

void spin_unlock_irqrestore(spinlock_t* lock, unsigned long flags)
{
	(void)lock;
	(void)flags;
}

/* A statement about a lock the caller already holds; holding is nothing. */
void assert_spin_locked(spinlock_t* lock)
{
	(void)lock;
}

void rwlock_init(rwlock_t* lock)
{
	(void)lock;
}

void read_lock(rwlock_t* lock)
{
	(void)lock;
}

void read_unlock(rwlock_t* lock)
{
	(void)lock;
}

void write_lock(rwlock_t* lock)
{
	(void)lock;
}

void write_unlock(rwlock_t* lock)
{
	(void)lock;
}

/*
 * in_interrupt() -- the irq-state query that belongs beside the irqsave pair
 * above, for the same reachability reason.  drm_cache.c:306 and :344
 * (drm_memcpy_from_wc) ask it inside WARN_ON() and branch on the answer to
 * pick a memcpy.  preempt.h:141 spells it `irq_count()` for every file that
 * includes that header; drm_cache.c is not one of them -- its include tree
 * carries only asm/preempt.h, which is a different file -- so the call lands
 * here as a call.
 *
 * 0, not 1: DCL runs kernel work in process context, with no interrupt
 * nesting to be in the middle of, and false is the answer that takes
 * drm_memcpy_from_wc's plain-memcpy path -- the one that is right when there
 * is no reason to believe the destination is write-combining memory.  `int`
 * rather than preempt.h's `bool` because this file includes no headers (see
 * the note at the top) and every caller only tests truth.
 */
int in_interrupt(void)
{
	return 0;
}
