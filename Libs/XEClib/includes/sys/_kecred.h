/**
* @file _kecred.h
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

#ifndef __KE_CRED_H__
#define __KE_CRED_H__

#include <_xeneva.h>
#include <stdint.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#define GID_NUM int
#define UID_NUM int

#define GID_GLOBAL_NET_COUNT  4
#define GID_GLOBAL_IPC_COUNT  4
#define GID_GLOBAL_MISC_COUNT 4

	/** offset based category **/
#define GID_GLOBAL_NET	0
#define GID_GLOBAL_IPC	GID_GLOBAL_NET_COUNT
#define GID_GLOBAL_MISC (GID_GLOBAL_NET_COUNT + GID_GLOBAL_IPC_COUNT)

	/** this are category entry, each category
    * will have its own group id
    */
	typedef enum _gid_entries_ {
		AURORA_GID_NET_SOCKET = GID_GLOBAL_NET,
		AURORA_GID_NET_RAW,
		AURORA_GID_NET_ADMIN,
		AURORA_GID_NET_MONITOR,

		/* ipc entries */
		AURORA_GID_IPC_POSTBOX = GID_GLOBAL_IPC,
		AURORA_GID_IPC_SHARED,
		AURORA_GID_IPC_SIGNAL,
		AURORA_GID_IPC_PIPE,

		AURORA_GID_MISC_WORLD = GID_GLOBAL_MISC,
		AURORA_GID_MISC_USERS,
		AURORA_GID_MISC_ADMIN,
		AURORA_GID_MISC_DAEMONS,
	};

	/**
 * @brief _KeCredChangeID -- change credential ids
 * @param fd -- file descriptor
 * @param uid -- user id
 * @param gid -- groupd id number
 */
	XE_LIB int _KeCredChangeID(int fd, UID_NUM uid, GID_NUM gid);

	/**
 * @brief _KeCredAddSGroup -- add supplimentary gid to process
 * @param proc_id -- process id to set
 * @param sgid -- sgid number
 */
	XE_LIB int _KeCredAddSGroup(int proc_id, int sgid);

	/**
 * _KeCredSetCap -- add capability to a process
 * @param proc_id -- process id, zero for current process, zero = current 
 * @param cap -- capability to add
 */
	XE_LIB int _KeCredSetCap(int proc_id, int cap);

	/**
 * _KeCredGetCap -- get capabilities of desired process
 * @param proc_id -- process id, zero for current process, zero = current 
 */
	XE_LIB int _KeCredGetCap(int proc_id);

	/**
 * @brief _KeSetUID -- change user id of a process
 * @param proc -- Process id to change their uid, zero = current 
 * @param uid -- User id
 */
	XE_LIB int _KeSetUID(int proc_id, UID_NUM uid);

	/**
 * @brief _KeSetGID -- change user id of a process
 * @param proc -- Process id to change their uid, zero = current 
 * @param uid -- User id
 */
	XE_LIB int _KeSetGID(int proc_id, GID_NUM gid);

	/**
 * @brief _KeGetGlobalGroupID -- return a group
 * id number
 * @param category -- category id
 */
	XE_LIB GID_NUM _KeGetGlobalGroupID(uint8_t category);

#ifdef __cplusplus
}
#endif

#endif
