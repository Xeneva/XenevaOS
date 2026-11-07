#ifndef __ASM_IO_H__
#define __ASM_IO_H__

/*
 * DCL <asm/io.h> -- port I/O accessors.
 *
 * ARM64 has no x86 I/O port space, but 8250's probe and PIO paths carry
 * inb/outb/inl/outl calls that must still *compile* even though every port we
 * intend to bind is MMIO (QEMU's pci-serial BAR, iMX8MP's UART -- both
 * reach registers through readb/writeb, already in <linux/kernel.h>). The
 * 16-bit pair (inw/outw) is here for the same reason: bochs.c hands the
 * VBE dispi registers that way when it has no MMIO BAR, and that whole arm
 * is dead on this target because bochs_uses_mmio() folds to true (see
 * <linux/kconfig.h>: CONFIG_HAS_IOPORT is undefined), but the call sites
 * still have to name the functions.
 *
 * So these are honest failures, not silent ones: a read answers 0 and a write
 * drops, and the first of either logs once. 0 rather than 0xff on purpose --
 * an all-ones answer makes LSR look like "transmitter always empty", which
 * would let the driver believe it is talking to a live UART, while 0 says
 * "no line is ready" and drives every polling loop into its timeout and out
 * again. Failing toward the timeout is what keeps a misconfigured port from
 * becoming a boot hang.
 *
 * When a board that genuinely needs PIO shows up, the body becomes a window
 * (PCI I/O space is a mapping on ARM64, not an x86 instruction) and this
 * comment is the thing to delete.
 */

#include <linux/kernel.h>	/* u8/u32 */

unsigned char inb(unsigned long addr);
void outb(unsigned char v, unsigned long addr);
unsigned short inw(unsigned long addr);
void outw(unsigned short v, unsigned long addr);
unsigned int inl(unsigned long addr);
void outl(unsigned int v, unsigned long addr);


/*
 * ioremap()/iounmap() and the be32 accessors, reached the way a mainline
 * driver reaches them: through <asm/io.h>.
 *
 * DCL deliberately houses ioremap in <linux/io.h> rather than here (see the
 * note at the top of that header: on arm64 ioremap is a plain P2V, and it
 * belongs with the other map/unmap declarations).  The consequence was that
 * nothing included it -- every mainline source says <asm/io.h> and calls
 * ioremap, so 8250_port.c:2908 compiled as an implicit `int ioremap()` and
 * assigned the result to `void *port->membase`.  On this LLP64 target that is
 * not a warning to tolerate: the int is 32 bits, membase is 8, and the pointer
 * would have its top half zeroed before a single register was read through it.
 * Re-exporting the header here is what makes the declaration reach the
 * call sites that were always going to ask for it.
 */
#include <linux/io.h>

/* The `_p` ("slow, with a pause") variants: same honest-failure answers as
 * inb/outb above -- 0 on read, drop on write -- because the pause exists to
 * slow down an x86 ISA access that has no counterpart here. */
static inline unsigned char inb_p(unsigned long addr)
{
	return inb(addr);
}

static inline void outb_p(unsigned char v, unsigned long addr)
{
	outb(v, addr);
}
#endif /* __ASM_IO_H__ */
