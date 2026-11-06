#ifndef __DEV_GPU_PROXY_H__
#define __DEV_GPU_PROXY_H__
#include <process.h>
extern void AuGpuProxyInitialize(void);
/* Called after bus mastering is stopped, before server DMA is released. */
extern void AuGpuProxyOwnerGone(AuProcess* proc, void* hw_device);
/* Non-blocking kernel-console damage notification for TERM/no-compositor mode. */
extern void AuGpuProxyDamage(int x, int y, int width, int height);
#endif
