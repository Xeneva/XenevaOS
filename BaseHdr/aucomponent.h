/**
 * Component model: runtime load/unload bookkeeping for loadable drivers.
 *
 * A driver is a component with an explicit lifecycle:
 *   load -> AuDriverMain() -> [running, refcounted] -> AuDriverUnload() -> freed
 * The kernel records what a driver acquires (IRQ handlers today) keyed by the
 * caller's return address, so unloading releases everything even if the
 * driver forgot to. Physical pages, MMIO and timers are tracked next.
 */
#ifndef __AU_COMPONENT_H__
#define __AU_COMPONENT_H__

#include <stdint.h>
#include <stddef.h>
#include <audrv.h>
#include <drvctl.h>

enum AuResKind {
	AU_RES_IRQ_SPI   = 1,  /* value = SPI number registered via GICRegisterSPIHandler */
	AU_RES_PHYS_PAGE = 2,  /* value = physical page from AuPmmngrAllocPage */
	AU_RES_PHYS_RUN  = 3,  /* value = physical base from AuPmmngrAllocPages */
	AU_RES_MMIO      = 4,  /* value = virtual base, extra = physical base, extra2 = page count */
};

typedef struct _au_resource_ {
	struct _au_resource_* next;
	uint8_t  kind;
	uint64_t value;
	uint64_t extra;
	uint64_t extra2;
} AuResource;

/* find the loaded driver whose image contains `addr` (NULL if none) */
extern AuDriver* AuDriverOwnerOf(uint64_t addr);
extern AuDriver* AuDriverFindByName(const char* name);

/* hold / release a driver so it cannot be unloaded while in use */
extern int  AuDriverRef(AuDriver* drv);
extern void AuDriverUnref(AuDriver* drv);

/* record / forget a resource owned by a driver */
extern void AuDriverTrack(AuDriver* drv, uint8_t kind, uint64_t value, uint64_t extra, uint64_t extra2);
extern void AuDriverUntrack(AuDriver* drv, uint8_t kind, uint64_t value);
/* forget a resource whichever driver holds it (memory can change hands) */
extern void AuDriverUntrackAny(uint8_t kind, uint64_t value);

/* kernel symbols a driver imports can be replaced by tracking wrappers;
 * returns NULL when `name` is not overridden (see pe.c AuGetProcAddress) */
extern void* AuDriverImportOverride(const char* name);

/* load (or reload after an unload) a driver by name and run its AuDriverMain */
extern int AuDriverLoadByName(const char* name);

/* true when a loaded driver, or a built-in one (aubuiltin.h), owns this PCI function */
extern bool AuDriverClaimsPci(int bus, int dev, int func);

/* /dev/drvctl: write "unload NAME", "load NAME" or "list" */
extern void AuDriverCtlInitialize(void);

/* unload a running driver: veto check, AuDriverUnload(), release tracked
 * resources, unmap and free the image, return its region to the free list */
extern int AuDriverUnload(AuDriver* drv);
extern int AuDriverUnloadByName(const char* name);

#endif
