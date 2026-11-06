/*
 * DCL vga_switcheroo.h -- no discrete GPU to switch.
 *
 * drm_edid.c's switcheroo wrappers and drm_file.c's delayed-switch drain
 * call these; every one returns the "nothing happened" answer, so the DDC
 * lock/unlock pair around an adapter that does not exist is balanced.
 */
#ifndef _LINUX_VGA_SWITCHEROO_H
#define _LINUX_VGA_SWITCHEROO_H

struct pci_dev;

static inline int vga_switcheroo_lock_ddc(struct pci_dev *pdev)
{
	(void)pdev;
	return 0;
}

static inline int vga_switcheroo_unlock_ddc(struct pci_dev *pdev)
{
	(void)pdev;
	return 0;
}

static inline void vga_switcheroo_process_delayed_switch(void) { }

#endif /* _LINUX_VGA_SWITCHEROO_H */
