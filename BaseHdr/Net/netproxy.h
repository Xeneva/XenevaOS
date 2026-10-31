#ifndef __NETPROXY_H__
#define __NETPROXY_H__

#include <aurora.h>
#include <process.h>

/** @brief AuNetProxyInitialize -- register /dev/netsrv */
extern void AuNetProxyInitialize(void);

/** @brief AuNetProxyOwnerGone -- detach if proc owns the proxy; called before
 *  the owner's DMA memory is freed */
extern void AuNetProxyOwnerGone(AuProcess* proc);

#endif
