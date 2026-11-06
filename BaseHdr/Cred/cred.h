/**
* @file cred.h
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#ifndef __CRED_H__
#define __CRED_H__

#include <Cred/group.h>
#include <Cred/user.h>
#include <Fs/vfs.h>
#include <process.h>

#define CRED_CAP_SETUID				   0
#define CRED_CAP_SETGID				   1
#define CRED_CAP_BYPASS_PERM_CHECK	   2
#define CRED_CAP_BYPASS_SECURITY_CHECK 3
#define CRED_CAP_IPC				   4
#define CRED_CAP_SETCAP				   5

/** verify capability bit position's value **/
#define CRED_IS_CAPABLE(a, b) ((a >> b) & 0x1)

/** set a capability */
#define CRED_SET_CAP(b) (1ULL << b)

/** set root capabilities **/
#define CRED_SET_CAP_ROOT(proc) (proc->creds.caps = UINT8_MAX)

#define CRED_MARK_ROOT(proc)                                                                       \
	proc->creds.uid = 0;                                                                           \
	proc->creds.gid = 0

/**
 * @brief AuCredChangeID -- change credential ids
 * @param fd -- file descriptor
 * @param uid -- user id
 * @param gid -- groupd id number
 */
extern int AuCredChangeID(int fd, UID_NUM uid, GID_NUM gid);

/**
 * @brief AuCredAddSGroup -- add supplimentary gid to process
 * @param proc_id -- process id number
 * @param sgid -- sgid number
 */
extern int AuCredAddSGroup(int proc_id, int sgid);

/**
 * AuCredSetCap -- add capability to a process
 * @param proc_id -- process id, zero for current process
 * @param cap -- capability to add
 */
extern int AuCredSetCap(int proc_id, int cap);

/**
 * AuCredGetCap -- get capabilities of desired process
 * @param proc_id -- process id, zero for current process
 */
extern int AuCredGetCap(int proc_id);

/**
 * @brief AuCredCheckPermissions -- before giving permission,
 * check all credentials, enei aru access koriba dibi neki,
 * ji pai take korba nori tu
 * @param node -- File node
 * @param cred -- Process's credential
 */
extern int AuCredCheckPermissions(AuVFSNode* node, AuProcCredentials* cred);

/**
 * @brief AuSetUID -- change user id of a process
 * @param proc -- Process id to change their uid
 * @param uid -- User id
 */
extern int AuSetUID(int proc_id, UID_NUM uid);

/**
 * @brief AuSetGID -- change user id of a process
 * @param proc -- Process id to change their uid
 * @param uid -- User id
 */
extern int AuSetGID(int proc_id, GID_NUM gid);

/**
 * @brief AuCredIsRoot -- check if credential information
 * is in root
 * @param c -- Pointer to credential information
 */
extern bool AuCredIsRoot(AuProcCredentials* c);

/**
 * @brief AuCredInGroup -- check the credentials if it lies
 * in desired groupd
 * @param c -- Pointer to credential information
 * @param gid -- Group id to check 
 */
extern bool AuCredInGroup(AuProcCredentials* c, GID_NUM gid);

#endif
