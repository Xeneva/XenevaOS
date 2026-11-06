/**
 * Registry of drivers that are linked into the kernel but can still be handed
 * over: a built-in driver registers the PCI function it bound, plus how to let
 * go of it (release) and how to take it back (reinit). That makes them behave
 * like loadable drivers for /dev/drvctl ("unload NAME" / "load NAME") and lets
 * /dev/hwdev refuse a PCI function a built-in driver still owns, which is what
 * allows a user-space server to take such a device over safely and the
 * supervisor to give it back.
 */
#ifndef __AU_BUILTIN_H__
#define __AU_BUILTIN_H__

#include <stdint.h>
#include <stdbool.h>

#define AU_BUILTIN_MAX 8

typedef void (*au_builtin_fn)(void);

/* called by a built-in driver once it has bound a device */
extern void AuBuiltinRegister(const char* name, int bus, int dev, int func,
							  au_builtin_fn release, au_builtin_fn reinit);

/* mark/unmark the entry as owning its device (used by release/reinit paths) */
extern int  AuBuiltinUnload(const char* name);   /* 0 ok, -1 unknown, -2 not bound */
extern int  AuBuiltinLoad(const char* name);     /* 0 ok, -1 unknown, -2 already bound */
extern bool AuBuiltinClaimsPci(int bus, int dev, int func);
extern int  AuBuiltinList(void (*cb)(const char* name, bool bound, int bus, int dev, int func));

#endif
