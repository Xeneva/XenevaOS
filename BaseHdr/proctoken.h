/**
* @file proctoken.h
* 
* BSD 2-Clause License
*
* Copyright (c) 2026, Manas Kamal Choudhury
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

#ifndef __PROCESS_TOKEN_H__
#define __PROCESS_TOKEN_H__

#include <process.h>
#include <stdint.h>

#define AURORA_MAX_PROC_TOKEN 50
/**
 * process token service is used to register special 
 * user space daemon services for specific kernel 
 * responsibility
 */

typedef struct _proc_token_ {
	AuProcess* proc;
	int thread_id;
} AuProcessToken;

enum _proc_tokens_ {
	PROCESS_TOKEN_NETWORK,
	PROCESS_TOKEN_DISPLAY,
	PROCESS_TOKEN_AUDIO,
	PROCESS_TOKEN_DEV,
	PROCESS_TOKEN_NETSERVER,
	PROCESS_TOKEN_LAUNCHER,
};

/**
 * @brief AuProcessTokenInitialize -- initialize token manager
 */
extern void AuProcessTokenInitialize();

/**
 * AuProcessTokenAddSelf -- add current process
 * to its belonging category
 * @param category -- category to look
 */
extern int AuProcessTokenAddSelf(uint8_t category);

/**
 * @brief AuProcessTokenGetProc -- get a process from
 * token entry
 * @param category -- category to look
 */
extern AuProcess* AuProcessTokenGetProc(uint8_t category);

/**
 * @brief AuProcessTokenGetThreadID -- get the thread id
 * of a slot
 */
extern int AuProcessTokenGetThreadID(uint8_t category);

/**
 * @brief AuProcessTokenRemoveSelf -- remove self from a token
 * @param category -- category to free
 */
extern int AuProcessTokenRemoveSelf(uint8_t category);

#endif
