#ifndef __DEVHW_H__
#define __DEVHW_H__

#include <aurora.h>
#include <process.h>

/**
 * @brief AuHwdevInitialize -- register /dev/hwdev (hardware access for
 * user-space drivers, see BaseHdr/hwdev.h)
 */
extern void AuHwdevInitialize(void);

/** @brief AuInputSrvInitialize -- register /dev/inputsrv (see BaseHdr/inputsrv.h) */
extern void AuInputSrvInitialize(void);

/**
 * @brief AuHwdevCallerAllowed -- true when proc is in the MISC_DAEMONS group.
 * Checked on every privileged operation: a write-mode open skips the VFS
 * permission check, so node permissions alone are not enough.
 */
extern bool AuHwdevCallerAllowed(AuProcess* proc);

/** @brief True when a live user-space hwdev capability owns this PCI function. */
extern bool AuHwdevClaimsPci(int bus, int dev, int func);

/**
 * @brief AuHwdevOwnsDma -- true when [phys, phys+len) lies inside a DMA buffer
 * that proc allocated through one of its own hw handles. Lets other kernel
 * code accept a user-supplied physical address without trusting it.
 */
extern bool AuHwdevOwnsDma(AuProcess* proc, uint64_t phys, uint64_t len);

/** @brief Validate that fd is the caller's PCI function with the requested ID. */
extern bool AuHwdevHandleIsDevice(AuProcess* proc, int fd, uint16_t vendor,
	uint16_t device);

/** @brief Return the opaque kernel object for one exact caller handle. */
extern void* AuHwdevHandleObject(AuProcess* proc, int fd);

/** @brief Validate a DMA range against one exact hwdev capability. */
extern bool AuHwdevHandleOwnsDma(AuProcess* proc, int fd, uint64_t phys,
	uint64_t len);

/** @brief AuHwdevCurrentProcess -- the process running the current thread */
extern AuProcess* AuHwdevCurrentProcess(void);

#endif
