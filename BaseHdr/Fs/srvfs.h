#ifndef __SRVFS_H__
#define __SRVFS_H__

#include <aurora.h>
#include <Fs/vfs.h>
#include <Hal/AA64/sched.h>

/** @brief AuSrvfsInitialize -- mount the /srv proxy file system */
extern void AuSrvfsInitialize(void);

/** @brief AuSrvfsOwns -- true if fsys is the /srv proxy */
extern bool AuSrvfsOwns(AuVFSNode* fsys);

/** @brief AuSrvfsRemove -- remove a /srv file for the RemoveFile syscall; 0 on success */
extern int AuSrvfsRemove(const char* path);

/** @brief AuSrvfsNoteOpenMode -- OpenFile tells the proxy the mode of the open it is about to do */
extern void AuSrvfsNoteOpenMode(int mode);

/** @brief syscall bracket hooks: restartable operations replay per-syscall history */
extern int g_srvfs_active;
extern void AuSrvfsSyscallEnterSlow(AA64Thread* thr);
extern void AuSrvfsSyscallExitSlow(AA64Thread* thr);
static inline void AuSrvfsSyscallEnter(AA64Thread* thr) { if (g_srvfs_active) AuSrvfsSyscallEnterSlow(thr); }
static inline void AuSrvfsSyscallExit(AA64Thread* thr) { if (g_srvfs_active) AuSrvfsSyscallExitSlow(thr); }

#endif
