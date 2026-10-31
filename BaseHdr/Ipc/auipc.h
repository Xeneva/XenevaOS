#ifndef __AUIPC_KERNEL_H__
#define __AUIPC_KERNEL_H__

#include <process.h>

/** @brief AuIpcInitialize -- register /dev/ipc */
extern void AuIpcInitialize(void);

/** @brief AuIpcProcessGone -- a process is being reaped: tear down its endpoints
 *  (failing every call waiting on them) and cancel the calls it had in flight */
extern void AuIpcProcessGone(AuProcess* proc);

/* Kernel as a client. Non-blocking: begin queues the call, poll collects the answer,
 * abandon drops interest (a late answer is discarded). Identity is whatever the
 * kernel passes -- normally the credentials of the process on whose behalf it calls. */
extern int AuIpcKCallBegin(const char* name, int pid, int uid, int gid, const void* req, uint32_t len, int32_t* call_id);
extern int AuIpcKCallPoll(int32_t call_id, void* out, uint32_t cap, uint32_t* outlen, int32_t* server_status);
extern void AuIpcKCallAbandon(int32_t call_id);

#endif
