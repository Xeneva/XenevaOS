#ifndef __SNDPROXY_H__
#define __SNDPROXY_H__

#include <aurora.h>
#include <process.h>

/** @brief AuSndProxyInitialize -- register /dev/sndsrv */
extern void AuSndProxyInitialize(void);

/** @brief AuSndProxyOwnerGone -- detach if proc owns the proxy; called before
 * that process's DMA memory is freed */
extern void AuSndProxyOwnerGone(AuProcess* proc);

#endif
