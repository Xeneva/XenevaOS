/**
 * DCL/linux_hvc_shim.c -- the five hvc entry points virtio_console.c calls.
 *
 * The hvc core (drivers/tty/hvc/hvc_console.c) is the tty half of the console
 * port: it owns the console registration, the tty_struct and the flip-buffer
 * plumbing that would carry bytes out to the console. None of that is vendored,
 * and DCL does not have a tty to hand them to yet -- so what a console port
 * needs is these five, which are the module's own references to that core.
 *
 * The header they are declared in is vendored beside the driver that includes
 * it, at Vendored/drivers/tty/hvc/hvc_console.h (mainline's own
 * `#include "../tty/hvc/hvc_console.h"` is relative to the driver, so it has
 * to sit there rather than in BaseHdr/linux/). This file includes it too, on
 * purpose: `struct hvc_struct` is dereferenced by the *driver*, not only by
 * the core -- notifier_add_vio() writes hp->irq_requested (virtio_console.c:
 * :1591), find_port_by_vtermno() reads hp->vtermno, and hvc_resize()'s inline
 * in that header takes &hp->lock -- so a hand-typed prototype here would be a
 * promise the compiler could not check, and C would link it either way.
 *
 * Without a `-device virtconsole` on the QEMU command line no console port is
 * ever created and none of these run; ports work through the cdev path in
 * DCL/linux_cdev_shim.c regardless of whether the console side is wired.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Mm/kmalloc.h>	/* kmalloc/kfree */
#include <linux/err.h>		/* ERR_PTR -- the failure arm hvc_alloc's caller
				 * tests with IS_ERR(), and NULL would fall
				 * through it into a NULL deref at :1591 */
#include <Drivers/uart.h>

#include "../Vendored/drivers/tty/hvc/hvc_console.h"

/*
 * hvc_alloc() -- stand up the hvc_struct the driver keeps as port->cons.hvc.
 *
 * The whole struct, not a stand-in with the first few fields in common: the
 * driver and the header's own inline reach into it (see the file comment), so
 * every field has to be where mainline puts it, which is what including
 * hvc_console.h above guarantees. outbuf is a flexible array in mainline's
 * layout, so the allocation carries the requested size after the struct --
 * `sizeof + outbuf_size`, mainline's struct_size() with the rounding it does
 * for an array aligned to sizeof(long).
 *
 * Zeroed first. DCL's kmalloc hands back whatever the TLSF free list last
 * held, and this block's `port` (tty_port), `lock` and `tty_resize`
 * (work_struct) are all read before anything writes them: hvc_resize() takes
 * &hp->lock on the very next use, and a work_struct that is not zeroed is
 * not an unqueued one.
 */
struct hvc_struct* hvc_alloc(uint32_t vtermno, int data,
							 const struct hv_ops* ops, int outbuf_size) {
	struct hvc_struct* hp;

	if (outbuf_size < 0)
		outbuf_size = 0;

	hp = (struct hvc_struct*)kmalloc(sizeof(struct hvc_struct) +
									 (unsigned int)outbuf_size);
	if (!hp)
		return ERR_PTR(-ENOMEM);

	memset(hp, 0, sizeof(struct hvc_struct));
	memset(hp->outbuf, 0, (size_t)outbuf_size);

	hp->vtermno = vtermno;
	hp->data = data;
	hp->ops = ops;
	hp->outbuf_size = outbuf_size;

	/*
	 * mainline picks `index` from an IDA; DCL has no ida to pick from and
	 * nothing reads it -- the tty side that would is exactly what is not
	 * vendored. vtermno is the field that matters: find_port_by_vtermno()
	 * keys on it, and a 0 there would look up port 0 for every console.
	 */
	/* %d and not %u: UARTDebugOut has no unsigned conversion. */
	UARTDebugOut("[dcl]: hvc_alloc vterm %d\r\n", (int)vtermno);
	return hp;
}

/*
 * hvc_poll() -- is there console input? 0 says no, which is the whole of the
 * contract at virtio_console.c:1752: `if (is_console_port(port) &&
 * hvc_poll(port->cons.hvc)) hvc_kick();` skips the kick, and there is
 * nothing to kick -- no tty, no console output path. Returning 1 instead
 * would spin that branch on every notification, calling a hvc_kick() that
 * has nothing to drain.
 */
int hvc_poll(struct hvc_struct* hp) {
	(void)hp;
	return 0;
}

/*
 * hvc_kick() -- drain pending console output through hv_ops->put_chars.
 * The no-argument form is mainline's: the core keeps the hvc to kick, not
 * the caller, so there is nothing to pass and nothing to find it from. Once
 * a tty side exists this is where put_chars() gets driven.
 */
void hvc_kick(void) {
	/* console TX would drain via hv_ops->put_chars; no tty side yet */
}

/*
 * hvc_remove() -- the matching free. mainline also unregisters the console
 * and drops the tty; DCL allocated exactly one block here and never handed
 * it to anything else, so one free is the whole of it.
 */
void hvc_remove(struct hvc_struct* hp) {
	if (!hp)
		return;
	UARTDebugOut("[dcl]: hvc_remove\r\n");
	kfree(hp);
}

/*
 * __hvc_resize() -- record the new window size. Called through hvc_resize()'s
 * inline, which takes hp->lock around it first (that is why the lock had to
 * be a real, zeroed field rather than padding).
 *
 * mainline's also reconfigures the tty and raises a signal; the tty is what
 * DCL does not have, so the state is kept where the next reader will find it
 * -- hp->ws, the field the caller read to get here in the first place
 * (virtio_console.c:1162 passes port->cons.ws) -- and nothing pretends to
 * have notified a process that cannot exist yet.
 */
void __hvc_resize(struct hvc_struct* hp, struct winsize ws) {
	if (!hp)
		return;
	hp->ws = ws;
}
