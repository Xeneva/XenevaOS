/**
 * Static components: drivers linked into the kernel image instead of loaded
 * from a PE file at run time (the unikernel build profile, STATIC_COMPONENTS=1).
 *
 * A static component keeps the component lifecycle -- the driver manager still
 * "loads" it by name and calls its AuDriverMain -- but there is no file, no PE
 * loader, no relocation and no import resolution. The component stays resident:
 * it is never unloadable (flags == 0), so /dev/drvctl reports it as such.
 */
#ifndef __AU_STATIC_H__
#define __AU_STATIC_H__

#include <audrv.h>

typedef struct {
	const char* name;            /* basename without extension, e.g. "virtgpu" */
	au_drv_entry entry;          /* the component's AuDriverMain */
	void* console_present;       /* optional AuConsolePresentFn (virtgpu), else NULL */
} AuStaticComponent;

/* look up a component by driver path ("/virtgpu.dll") or bare name; NULL if the
 * image does not contain it (always NULL in a normal build) */
extern const AuStaticComponent* AuStaticComponentFind(const char* path);

/* number of components linked into this image */
extern int AuStaticComponentCount(void);

#endif
