/*
 * DCL <linux/vgaarb.h> -- the VGA-arbiter surface virtio-gpu's init reads.
 *
 * mainline's header is the arbiter's client API behind CONFIG_VGA_ARB. There
 * is no arbiter here, and nothing on this platform's display path wants one:
 * the device the port runs against (virtio-gpu-pci) reports class 0x0380, so
 * pci_is_vga() is false and both call sites -- vga_get_interruptible() and
 * vga_put(), virtgpu_drv.c:190-204 -- sit in a branch that is never taken.
 *
 * The two functions are therefore mainline's own !CONFIG_VGA_ARB answers:
 * the resource bits are unconditional in upstream's header, and with the
 * arbiter compiled out the get/put calls become calls that succeed and do
 * nothing. That is not a placeholder for a future arbiter; it is the honest
 * description of a bus with one display function whose legacy decode
 * nothing else shares or routes.
 */
#ifndef _LINUX_VGAARB_H
#define _LINUX_VGAARB_H

struct pci_dev;

/* Legacy VGA regions; mainline's names and values. */
#define VGA_RSRC_NONE		0x00
#define VGA_RSRC_LEGACY_IO	0x01
#define VGA_RSRC_LEGACY_MEM	0x02
#define VGA_RSRC_LEGACY_MASK	(VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM)
/* Non-legacy access */
#define VGA_RSRC_NORMAL_IO	0x04
#define VGA_RSRC_NORMAL_MEM	0x08

static inline int vga_get_interruptible(struct pci_dev* pdev,
					unsigned int rsrc)
{
	(void)pdev;
	(void)rsrc;
	return 0;	/* mainline's no-arbiter answer: acquired */
}

static inline void vga_put(struct pci_dev* pdev, unsigned int rsrc)
{
	(void)pdev;
	(void)rsrc;
}

#endif /* _LINUX_VGAARB_H */
