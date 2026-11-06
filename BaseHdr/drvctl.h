/**
 * Signed status ABI for /dev/drvctl.
 */
#ifndef __AU_DRVCTL_H__
#define __AU_DRVCTL_H__

#include <stdint.h>

#define AU_UNLOAD_OK         0
#define AU_UNLOAD_NOT_FOUND -1
#define AU_UNLOAD_NOT_OPTED -2
#define AU_UNLOAD_BUSY      -3
#define AU_UNLOAD_VETOED    -4
#define AU_DRVCTL_E_UNKNOWN -5
#define AU_LOAD_ALREADY_PRESENT -2

#define AU_DRVCTL_IOC_EXEC  0x5701
#define AU_DRVCTL_CMD_MAX   96

typedef struct _AuDriverCtlRequest {
	char command[AU_DRVCTL_CMD_MAX];
} AuDriverCtlRequest;

#endif
