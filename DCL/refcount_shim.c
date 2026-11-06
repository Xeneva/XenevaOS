/*
 * DCL/refcount_shim.c -- refcount_warn_saturate()'s one definition.
 *
 * Mainline's lives in lib/refcount.c and does two things: reports the fault,
 * then sets the counter to REFCOUNT_SATURATED. Both halves are kept here,
 * for reasons that are not symmetric.
 *
 * The set is non-negotiable. It is what stops the counter moving again, and
 * therefore what stops a wrapped count from being mistaken for a live object
 * -- the entire reason refcount_t exists rather than reusing atomic_t. Drop
 * it and the report describes a bug that is still actively corrupting state.
 *
 * The report had somewhere to go after all. DCL's <linux/printk.h> maps
 * printk() onto UARTDebugOut(), so a saturation prints on the console the
 * kernel already owns; this file uses it rather than staying silent, which
 * was the assumption when refcount.h was first written and is now wrong.
 *
 * The five saturation types are named in the message. They are five
 * different bugs -- taking a reference to a freed object, overflowing a live
 * one, underflowing, leaking by decrementing past zero -- and a report that
 * said only "refcount saturated" would leave the reader to work out which,
 * from a counter that by construction no longer records how it got there.
 * Once saturated the counter is stuck, so this prints once per site and not
 * once per subsequent access.
 */

#include <linux/printk.h>	/* printk */
#include <linux/refcount.h>	/* refcount_t, REFCOUNT_SATURATED */

void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t)
{
	const char *what;

	switch (t) {
	case REFCOUNT_ADD_NOT_ZERO_OVF:
		what = "addition on 0; use-after-free";
		break;
	case REFCOUNT_ADD_OVF:
		what = "saturated; leaking memory";
		break;
	case REFCOUNT_ADD_UAF:
		what = "addition on 0; use-after-free";
		break;
	case REFCOUNT_SUB_UAF:
		what = "underflow; use-after-free";
		break;
	case REFCOUNT_DEC_LEAK:
		what = "decrement hit 0; leaking memory";
		break;
	default:
		what = "unknown saturation type";
		break;
	}

	/*
	 * The assignment is the load-bearing half and is deliberately before
	 * the print: a print that faults or that never returns still leaves
	 * the counter stuck rather than still climbing.
	 */
	refcount_set(r, REFCOUNT_SATURATED);

	printk("refcount_t: saturated; %s\n", what);
}
