/**
* @file postbox.h
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#ifndef __POSTBOX_H__
#define __POSTBOX_H__

#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif
#ifdef ARCH_X64
#include <Hal/x86_64_sched.h>
#elif ARCH_ARM64
#include <Hal/AA64/sched.h>
#endif
#include <Fs/vfs.h>


#define POSTBOX_CREATE  401
#define POSTBOX_DESTROY 402
#define POSTBOX_PUT_EVENT  403
#define POSTBOX_GET_EVENT  404
#define POSTBOX_CREATE_ROOT  405
#define POSTBOX_GET_EVENT_ROOT  406

#define POSTBOX_NO_EVENT  -1
#define POSTBOX_ROOT_ID    1

//#pragma pack(push,1)
/*
 * PostEvent -- event message structure
 */
typedef struct _post_event_ {
	uint8_t type;
	uint16_t to_id;
	uint16_t from_id;
	uint32_t dword;
	uint32_t dword2;
	uint32_t dword3;
	uint32_t dword4;
	uint32_t dword5;
	uint32_t dword6;
	uint32_t dword7;
	uint32_t dword8;
	uint32_t* pValue;
	uint32_t *pValue1;
	char* charValue;
	unsigned char* charValue2;
	char charValue3[100];
}PostEvent;
//#pragma pack(pop)

#ifdef ARCH_X64
#pragma pack(push,1)
#endif
typedef struct _postbox_ {
	uint64_t* address;
	uint16_t ownerID;
	int headIdx;
	int tailIdx;
	bool full;
	uint16_t size;
	struct _postbox_ *next;
	struct _postbox_* prev;
}PostBox;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

/**
 * @brief AuIPCPostBoxInitialise -- initialise
 * the post box ipc manager
 */
extern void AuIPCPostBoxInitialise();

/**
 * @brief PostBoxPutEvent -- put an event to a specific post box
 * @param event -- Event to put
 */
extern void PostBoxPutEvent(PostEvent* event);

#ifdef ARCH_X64
/*
 * PostBoxGetEvent -- get an event from post box and copy it to a
 * memory area
 * @param event -- pointer to a memory area
 * @param root -- is this post box is root
 * @param curr_thread -- Pointer to current thread
 */
extern int PostBoxGetEvent(PostEvent* event, bool root, AuThread* curr_thread);
#elif ARCH_ARM64
/**
 * @brief PostBoxGetEvent -- get an event from post box and copy it to a
 * memory area
 * @param event -- pointer to a memory area
 * @param root -- is this post box is root
 * @param curr_thread -- Pointer to current thread
 */
extern int PostBoxGetEvent(PostEvent* event, bool root, AA64Thread* curr_thread);
#endif

extern AuVFSNode* AuPostBOXGetNode();

/**
* @brief PostBoxCreate -- creates a postbox
* @param root -- is this post box root ?
* @param tid -- thread id
*/
extern void PostBoxCreate(bool root, uint16_t tid);

/**
* @brief PostBoxDestroyByID -- destroys a post box identified by
* an id
* @param id -- id of the postbox
*/
extern void PostBoxDestroyByID(uint16_t id);

/**
 * @brief PostBoxIOControl -- I/O Control function for
 * post box manager
 * @param file -- Pointer to postbox file
 * @param code -- Post Box command
 * @param arg -- extra data
 */
extern int PostBoxIOControl(AuVFSNode* file, int code, void* arg);


#endif