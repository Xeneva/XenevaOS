/* SPDX-License-Identifier: MIT */

#ifndef _LINUX_APERTURE_H_
#define _LINUX_APERTURE_H_

/*
 * DCL <linux/aperture.h> -- upstream's header, including the shapes that
 * matter here: CONFIG_APERTURE_HELPERS is not defined in this tree, so the
 * #else arm below is the live code and every entry point answers 0.
 *
 * That is not a shortcut around mainline's logic, it is the same answer a
 * mainline build without the option gives: aperture helpers are how one
 * driver evicts another's firmware framebuffer from the PCI BARs they share
 * (the simpledrm/efifb/vesafb conflicts). This target's boot console is
 * Xeneva's own GOP framebuffer on its own device, not a PCI driver, and the
 * DRM device a vendored driver claims (bochs) never overlaps it -- there is
 * nothing to evict and no registry to evict it through. The return value is
 * what every call site tests before proceeding, so 0 keeps the driver on its
 * happy path, exactly as it would be on mainline.
 *
 * Kept as inline functions rather than deleted declarations so that a
 * driver's `aperture_remove_conflicting_pci_devices(pdev, name)` call site
 * compiles without a DCL shim symbol behind it.
 */

#include <linux/types.h>

struct pci_dev;
struct platform_device;

#if defined(CONFIG_APERTURE_HELPERS)
int devm_aperture_acquire_for_platform_device(struct platform_device *pdev,
					      resource_size_t base,
					      resource_size_t size);

int aperture_remove_conflicting_devices(resource_size_t base, resource_size_t size,
					const char *name);

int __aperture_remove_legacy_vga_devices(struct pci_dev *pdev);

int aperture_remove_conflicting_pci_devices(struct pci_dev *pdev, const char *name);
#else
static inline int devm_aperture_acquire_for_platform_device(struct platform_device *pdev,
							    resource_size_t base,
							    resource_size_t size)
{
	(void)pdev;
	(void)base;
	(void)size;
	return 0;
}

static inline int aperture_remove_conflicting_devices(resource_size_t base, resource_size_t size,
						      const char *name)
{
	(void)base;
	(void)size;
	(void)name;
	return 0;
}

static inline int __aperture_remove_legacy_vga_devices(struct pci_dev *pdev)
{
	(void)pdev;
	return 0;
}

static inline int aperture_remove_conflicting_pci_devices(struct pci_dev *pdev, const char *name)
{
	(void)pdev;
	(void)name;
	return 0;
}
#endif

static inline int aperture_remove_all_conflicting_devices(const char *name)
{
	return aperture_remove_conflicting_devices(0, (resource_size_t)-1, name);
}

#endif /* _LINUX_APERTURE_H_ */
