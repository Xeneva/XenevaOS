/*
 * DCL srcu.h -- sleeping-RCU API as no-ops.
 *
 * drm_drv.c/drm_bridge.c run their unplug checks under DEFINE_STATIC_SRCU +
 * srcu_read_lock(); single-CPU Xeneva has no reader/writer separation to
 * wait for, so a read section is nothing and synchronize_srcu returns at
 * once.  notifier.h's heavy srcu_notifier machinery is stubbed on its own.
 */
#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

struct srcu_struct {
	int _dcl_dummy;
};

#define DEFINE_SRCU(name)	struct srcu_struct name
#define DEFINE_STATIC_SRCU(name) static struct srcu_struct name

static inline int srcu_read_lock(struct srcu_struct *s) { (void)s; return 0; }
static inline void srcu_read_unlock(struct srcu_struct *s, int id) { (void)s; (void)id; }
static inline void synchronize_srcu(struct srcu_struct *s) { (void)s; }
static inline void cleanup_srcu_struct(struct srcu_struct *s) { (void)s; }

#define srcu_dereference(p, c)		(p)
#define srcu_dereference_check(p, c)	(p)

#endif /* _LINUX_SRCU_H */
