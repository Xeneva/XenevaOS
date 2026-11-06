/**
* @file group.h
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

#ifndef __GROUP_H__
#define __GROUP_H__

#include <stdint.h>


#define GID_NUM int

/** @brief
 * gid general file could be any randome number
 * defined by user administration system, must match
 * the user id number 
 */
#define AURORA_GID_GENERAL_FILE -1

#define GROUP_NAME_NETWORK     "network"
#define GROUP_NAME_AUDIO       "audio"
#define GROUP_NAME_TTY         "tty"
#define GROUP_NAME_STORAGE     "storage"
#define GROUP_NAME_GRAPHICS    "graphics"
#define GROUP_NAME_IPC_POSTBOX "postbox_ipc"

#define GROUP_ID_ROOT 0

#define GID_GLOBAL_NET_COUNT 4
#define GID_GLOBAL_IPC_COUNT 4
#define GID_GLOBAL_MISC_COUNT 4

#define AURORA_MAX_GROUPS  (GID_GLOBAL_NET_COUNT + \
                           GID_GLOBAL_IPC_COUNT + \
                           GID_GLOBAL_MISC_COUNT )

/** offset based category **/
#define GID_GLOBAL_NET 0
#define GID_GLOBAL_IPC GID_GLOBAL_NET_COUNT
#define GID_GLOBAL_MISC  (GID_GLOBAL_NET_COUNT + GID_GLOBAL_IPC_COUNT)

/** this are category entry, each category
 * will have its own group id
 */
typedef enum _gid_entries_ {
	AURORA_GID_NET_SOCKET = GID_GLOBAL_NET,
    AURORA_GID_NET_RAW,
    AURORA_GID_NET_ADMIN,
    AURORA_GID_NET_MONITOR,

    /* ipc entries */
    AURORA_GID_IPC_POSTBOX = GID_GLOBAL_IPC ,
    AURORA_GID_IPC_SHARED,
    AURORA_GID_IPC_SIGNAL,
    AURORA_GID_IPC_PIPE,

    AURORA_GID_MISC_WORLD = GID_GLOBAL_MISC,
    AURORA_GID_MISC_USERS,
    AURORA_GID_MISC_ADMIN,
    AURORA_GID_MISC_DAEMONS,
}GID_ENTRIES;

/**
 * @brief AuCredGroupInitialize -- initialize default group
 * id's which is zero
 */
extern void AuCredGroupInitialize();

/**
 * AuCredGroupAdd -- add group value to a category
 * @param category -- category number
 * @param value -- value to add
 */
extern int AuCredGroupAdd(uint8_t category, GID_NUM value);

/**
 * @brief AuCredGetGroupCategory -- get a group category
 * number by looking it name
 * @param string -- name of the group
 */
extern uint8_t AuCredGetGroupCategory(const char* string);

/**
 * @brief AuCredGetGroupID -- return a group
 * id number
 * @param category -- group category value
 */
extern GID_NUM AuCredGetGroupID(uint8_t category);

#endif
