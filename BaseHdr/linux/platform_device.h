#ifndef __DCL_STUB_BaseHdr_linux_platform_device_h__
#define __DCL_STUB_BaseHdr_linux_platform_device_h__
/*
 * linux/platform_device.h -- created by mkh.sh while porting 8250_port.c.
 *
 * Still not a platform bus: nothing in this tree registers a platform
 * driver. The two declarations below are here because drm_module.h --
 * which bochs.c reaches through <drm/drm_module.h> -- defines static
 * inline registration helpers that name both functions. The PCI arm of
 * that header is the one this target uses; the platform arm is parsed
 * and never referenced, so declarations are all it needs. Without them
 * the parse of every bochs.c compile would trip -Wimplicit-function-
 * declaration for a function nobody calls.
 */
struct platform_driver;

int platform_driver_register(struct platform_driver *drv);
void platform_driver_unregister(struct platform_driver *drv);

#endif /* __DCL_STUB_BaseHdr_linux_platform_device_h__ */
