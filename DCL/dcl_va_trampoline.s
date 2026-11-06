/*
 * AAPCS64 variadic entry trampolines for module-facing shim functions.
 *
 * BaseHdr/stdarg.h has no __GNUC__ under aarch64-unknown-windows, so its
 * va_start is the hand-rolled "&last + 8" form that reads the callee's own
 * frame -- but AAPCS64 callers (kernel code and .ko modules alike) deliver
 * varargs in x1..x7.  Each stub below saves x0..x7, then tails into its
 * _Call twin with a pointer to the save area; the C side rebuilds va_list
 * from that pointer (varargs start right after each function's fixed args).
 * Same pattern as the UARTDebugOut trampoline in Hal/aa64_print.s.
 */

/* save area layout: x0@0 x1@8 x2@16 x3@24 x4@32 x5@40 x6@48 x7@56 */

.extern device_create_Call
.global device_create
device_create:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #64
	stp x0, x1, [sp, #0]
	stp x2, x3, [sp, #16]
	stp x4, x5, [sp, #32]
	stp x6, x7, [sp, #48]
	mov x5, sp			/* fixed args x0..x4 untouched; x5 = reg_save */
	bl device_create_Call
	add sp, sp, #64
	ldp x29, x30, [sp], #16
	ret

/*
 * The _printk / _dev_err / _dev_warn trampolines live on the
 * dcl-virtio-console branch with their _Call twins (linux_kmod_shim.c);
 * master only carries the device_create leg of the char-device bridge.
 */
