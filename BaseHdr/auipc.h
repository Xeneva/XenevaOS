/**
 * auipc.h -- generic kernel IPC: named endpoints with synchronous call/reply.
 *
 * A server (MISC_DAEMONS group) creates a named endpoint on /dev/ipc. Any
 * process the endpoint's ACL admits can connect by name and CALL it: the kernel
 * copies the request into a queue, wakes the server, and the caller sleeps
 * until the server REPLYs, the timeout expires, or the server dies. The server
 * learns the caller's pid/uid/gid from the kernel, never from the message.
 *
 * Blocking calls cannot return a value after they sleep (the syscall frame is
 * resumed, not the C stack), so the sleeping operations are paired with a
 * non-blocking one that delivers the data:
 *
 *   client: IPC_IOC_CALL (sleeps)  then  IPC_IOC_FETCH (reply or error)
 *   server: IPC_IOC_WAIT (sleeps)  then  IPC_IOC_RECV  (request or empty)
 *           IPC_IOC_REPLY
 *
 * Fields named `status` are the result of the sleeping call; read them after it.
 */
#ifndef __AUIPC_H__
#define __AUIPC_H__

#include <stdint.h>

#define IPC_MAX_MSG        8192
#define IPC_NAME_MAX       24

#define IPC_IOC_CREATE     0x5601   /* IpcCreate  -> handle                       */
#define IPC_IOC_DESTROY    0x5602   /* IpcHandle                                  */
#define IPC_IOC_CONNECT    0x5603   /* IpcConnect -> handle                       */
#define IPC_IOC_CALL       0x5604   /* IpcCall    (sleeps)                        */
#define IPC_IOC_FETCH      0x5605   /* IpcFetch                                   */
#define IPC_IOC_WAIT       0x5606   /* IpcWait    (sleeps)                        */
#define IPC_IOC_RECV       0x5607   /* IpcRecv                                    */
#define IPC_IOC_REPLY      0x5608   /* IpcReply                                   */

#define IPC_ACL_ANY        0        /* any process may call */
#define IPC_ACL_DAEMONS    1        /* only MISC_DAEMONS processes may call */

#define IPC_OK             0
#define IPC_E_PERM        -1
#define IPC_E_INVAL       -2
#define IPC_E_NOENT       -3
#define IPC_E_BUSY        -4        /* endpoint queue full / name taken */
#define IPC_E_TIMEOUT     -5
#define IPC_E_DEAD        -6        /* the server went away */
#define IPC_E_EMPTY       -7
#define IPC_E_TOOBIG      -8

typedef struct { char name[IPC_NAME_MAX]; uint32_t acl; int32_t handle; } IpcCreate;
typedef struct { char name[IPC_NAME_MAX]; int32_t handle; uint32_t reserved; } IpcConnect;
typedef struct { int32_t handle; uint32_t reserved; } IpcHandle;

typedef struct {
	int32_t handle;
	uint32_t timeout_ms;          /* 1..5000 */
	const void* req;
	uint32_t req_len;
	int32_t status;               /* IPC_OK once the reply is in; else IPC_E_* */
	int32_t call_id;              /* out: pass to FETCH */
	uint32_t reserved;
} IpcCall;

typedef struct {
	int32_t call_id;
	int32_t status;               /* out: IPC_OK, or why there is no reply */
	void* resp;
	uint32_t resp_cap;
	uint32_t resp_len;            /* out */
	int32_t server_status;        /* out: the status the server put in its REPLY */
	uint32_t reserved;
} IpcFetch;

typedef struct { int32_t handle; uint32_t timeout_ms; int32_t status; uint32_t reserved; } IpcWait;

typedef struct {
	int32_t handle;
	int32_t call_id;              /* out */
	void* buf;
	uint32_t cap;
	uint32_t len;                 /* out: request length */
	int32_t sender_pid, sender_uid, sender_gid;   /* out: from the kernel */
	int32_t status;               /* out: IPC_OK / IPC_E_EMPTY / IPC_E_TOOBIG */
} IpcRecv;

typedef struct {
	int32_t handle;
	int32_t call_id;
	const void* resp;
	uint32_t len;
	int32_t server_status;        /* delivered to the caller as IpcFetch.server_status */
	uint32_t reserved;
} IpcReply;

#endif
