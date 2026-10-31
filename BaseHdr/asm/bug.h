#ifndef __ASM_BUG_H__
#define __ASM_BUG_H__

/*
 * DCL <asm/bug.h> -- where BUG()/WARN() actually live.
 *
 * This header exists so a mainline source can include it the way mainline
 * does.  Nothing in DCL reaches it directly: the chain that matters is
 *   virtio_console.c -> <linux/freezer.h> -> <linux/jump_label.h>
 *                   -> <linux/bug.h> -> <asm/bug.h>
 * and <linux/bug.h> needs three things from it -- BUG() and BUG_ON() at :18
 * and :103, and the variadic WARN(cond, fmt, ...) at :105.
 *
 * All three are already in <linux/kernel.h> (BUG, BUG_ON, WARN_ON, WARN), and
 * that is deliberate on both sides: kernel.h is where the tty/serial ports
 * pick them up, so there is exactly one definition of each rather than a
 * second one here that could drift from it.  Routing rather than redefining
 * also keeps an identical-body redefinition off the table -- the macro text
 * is never written twice.
 *
 * The cycle is safe: kernel.h includes compiler.h, minmax.h, string.h and
 * sprintf.h, none of which reaches bug.h.
 */
#include <linux/kernel.h>

#endif
