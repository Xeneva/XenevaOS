/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_REFCOUNT_TYPES_H
#define _LINUX_REFCOUNT_TYPES_H

/*
 * DCL <linux/refcount_types.h> -- byte-port of mainline's, except the include.
 *
 * Mainline's file includes <linux/types.h>, because that is where atomic_t
 * lives there. In this tree atomic_t is defined by <linux/atomic.h> (which
 * also carries every accessor over it), and types.h does not know the name,
 * so the include is the one deliberate difference. Everything below -- the
 * struct, the field name, the typedef -- is mainline's verbatim, and the
 * field name matters: drm_connector.c:1070 reaches inside struct kref and
 * writes refcount_dec_and_test(&conn->base.refcount.refcount), so a rename
 * would not compile anywhere that expression appears.
 */
#include <linux/atomic.h>

/**
 * typedef refcount_t - variant of atomic_t specialized for reference counts
 * @refs: atomic_t counter field
 *
 * The counter saturates at REFCOUNT_SATURATED and will not move once
 * there. This avoids wrapping the counter and causing 'spurious'
 * use-after-free bugs.
 */
typedef struct refcount_struct {
	atomic_t refs;
} refcount_t;

#endif /* _LINUX_REFCOUNT_TYPES_H */
