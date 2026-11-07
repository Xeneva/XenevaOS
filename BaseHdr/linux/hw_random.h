#ifndef LINUX_HWRANDOM_H_
#define LINUX_HWRANDOM_H_

/*
 * DCL <linux/hw_random.h> -- struct hwrng and the register/unregister pair.
 *
 * Field order is copied from mainline v7.2 include/linux/hw_random.h and must
 * stay that way: DCL/linux_kmod_shim.c hands the registered rng back out as an
 * opaque pointer and reads ->name / ->read off it, so the two definitions are
 * one ABI in two files. If a field moves here, the shim reads the wrong one
 * and every hwrng read returns garbage -- quietly, because the types are all
 * still pointers.
 *
 * The prototypes match mainline exactly, in particular
 *
 *     int (*read)(struct hwrng *rng, void *data, size_t max, bool wait);
 *
 * `size_t`, not `unsigned long`: this target is aarch64 *Windows*, LLP64, so
 * unsigned long is 4 bytes while size_t is 8. A shim copy declaring `unsigned
 * long max` still lines up as a struct (every member above it is a pointer)
 * but passes a 32-bit argument where the callee reads 64, which only works
 * while the compiler happens to zero-extend. That mismatch was found while
 * bringing virtio-rng in through the vendored build; the shim copy now
 * follows this header rather than the other way round.
 *
 * ### The private tail is deliberately absent
 *
 * mainline continues with `struct list_head list; struct kref ref; struct
 * work_struct cleanup_work; struct completion cleanup_done; struct completion
 * dying;` -- all of it bookkeeping for hwrng core's unregister-delay machinery.
 * DCL's hwrng_register() (linux_kmod_shim.c) is a fixed-size table of
 * pointers: it stores the address and never touches the tail, so the fields
 * would be allocation nobody reads. Keeping them would also mean including
 * <linux/list.h> here to give `list` a complete type, which puts mainline's
 * `list_add(list_head*, list_head*)` into every translation unit that reaches
 * this header -- and Xeneva's own <list.h> spells `list_add(list_t*, void*)`,
 * so any TU that ever sees both fails to compile (that is the stage-1 gotcha,
 * recorded in the tracker). One unused member is not worth that.
 *
 * If DCL ever ports the real hwrng core, add the tail back and accept the
 * list.h inclusion -- but then, and only then.
 */

#include <linux/kernel.h>	/* size_t, bool, u32 */

struct hwrng {
	/* Unique RNG name. */
	const char* name;
	/* Initialization callback (can be NULL). */
	int (*init)(struct hwrng* rng);
	/* Cleanup callback (can be NULL). */
	void (*cleanup)(struct hwrng* rng);
	/*
	 * data_present / data_read are mainline's *OBSOLETE* API, kept because
	 * a driver may still implement either. Nothing in DCL calls them: the
	 * shim reads through ->read only.
	 */
	int (*data_present)(struct hwrng* rng, int wait);
	int (*data_read)(struct hwrng* rng, u32* data);
	/*
	 * New API: fill up to max bytes. mainline documents max as a multiple
	 * of 4 and >= 32; nothing here enforces that -- hwrng_selftest() passes
	 * 32 against a 32-byte buffer, and /dev/urandom passes whatever chunk
	 * its caller asked for. The field is only the type signature.
	 */
	int (*read)(struct hwrng* rng, void* data, size_t max, bool wait);
	/*
	 * DCL delta: mainline has `unsigned long priv;`, which is pointer-sized
	 * on LP64 (Linux/arm64) and therefore cannot be reproduced on this
	 * target -- aarch64-unknown-windows is LLP64, `unsigned long` is 4
	 * bytes, and this field holds `struct virtrng_info *`, which lives in
	 * the kernel high half at 0xFFFFC000.... A 32-bit `priv` would silently
	 * drop the top half of every pointer stored into it.
	 *
	 * uintptr_t is the same "integer that holds a pointer" the field is
	 * for, and it is pointer-sized here. This is one half of a two-part
	 * fix -- the other is the matching cast in virtio-rng.c:171, in the
	 * vendored source, which cannot be fixed from a header.
	 *
	 * Changing this does not move anything: the field is last-but-one in
	 * the public part of struct hwrng (quality follows), and DCL's hwrng
	 * core has no private tail to be positioned against -- see the note at
	 * the top of this header.
	 */
	uintptr_t priv;
	/*
	 * Bits of entropy per 1024 bits of input; 1..1024, or 0 for maximum.
	 * virtio-rng leaves this 0 (its own read path reports nothing).
	 */
	unsigned short quality;
};

/*
 * DCL/linux_kmod_shim.c defines both. Declared (not defined) here so a
 * driver can call them the way it does upstream, and so the compiler checks
 * the argument type instead of accepting an implicit int.
 */
extern int hwrng_register(struct hwrng* rng);
extern void hwrng_unregister(struct hwrng* rng);

#endif /* LINUX_HWRANDOM_H_ */
