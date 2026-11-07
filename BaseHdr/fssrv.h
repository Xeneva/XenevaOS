/**
 * fssrv.h -- wire protocol between the kernel's /srv file-system proxy
 * (KernelAA64/Fs/srvfs.c) and a user-space file server (Process/fssrv).
 *
 * The protocol is stateless and path-based, like NFS: there are no server-side
 * handles, so nothing leaks when a client closes a file or dies. Every request
 * travels over the generic kernel IPC to the endpoint FSSRV_ENDPOINT; the kernel
 * stamps each one with the calling process's uid, which is what the server
 * enforces permissions against. Data follows the fixed header in the same message.
 */
#ifndef __FSSRV_H__
#define __FSSRV_H__

#include <stdint.h>

#define FSSRV_ENDPOINT   "fs"
#define FSSRV_NAME_MAX   31
#define FSSRV_MAX_DATA   4096

#define FSSRV_OP_OPEN    1        /* flags: FSSRV_F_*, answers size/uid        */
#define FSSRV_OP_READ    2        /* offset, len -> data                       */
#define FSSRV_OP_WRITE   3        /* offset, data (len bytes follow)           */
#define FSSRV_OP_READDIR 4        /* offset = entry index -> one FsDirEnt or FSSRV_E_NOENT at the end */
#define FSSRV_OP_REMOVE  5        /* owner or uid 0 only                       */

#define FSSRV_F_CREATE   1
#define FSSRV_F_WRITE    2

#define FSSRV_OK          0
#define FSSRV_E_NOENT    -1
#define FSSRV_E_PERM     -2
#define FSSRV_E_INVAL    -3
#define FSSRV_E_NOSPC    -4

typedef struct {
	uint32_t op;
	uint32_t flags;
	uint32_t offset;
	uint32_t len;                      /* READ: bytes wanted; WRITE: bytes following */
	char name[FSSRV_NAME_MAX + 1];     /* file name inside the file system, no '/' */
} FsReq;                               /* WRITE data follows this header */

/* one directory entry, returned as the data of a READDIR answer */
typedef struct {
	char name[FSSRV_NAME_MAX + 1];
	uint32_t size;
	uint32_t uid;
} FsDirEnt;

typedef struct {
	int32_t status;
	uint32_t size;                     /* file size after the operation */
	uint32_t uid;                      /* owner */
	uint32_t len;                      /* READ: data bytes following */
} FsResp;                              /* READ data follows this header */

#endif
