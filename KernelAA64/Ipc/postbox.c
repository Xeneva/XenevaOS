/**
* @file postbox.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#include <Ipc/postbox.h>
#include <Hal/AA64/sched.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <_null.h>
#include <Fs/vfs.h>
#include <Fs/Dev/devfs.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <aucon.h>
#include <Cred/cred.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif

/**
* @brief
 * NOTE: PostBoxIPCManager is aurora's main communication manager between
 * user space processes and kernel, in future drivers will also able to
 * communicate with user processes and kernel through PostBoxIPCManager
 */

#define POSTEVENT_ALIGN 8
#define POSTEVENT_SIZE_ALIGNED                                                                     \
	(((sizeof(PostEvent) + (POSTEVENT_ALIGN - 1)) / POSTEVENT_ALIGN) * POSTEVENT_ALIGN)

PostBox* firstBox = NULL;
PostBox* lastBox = NULL;
bool _PostBoxRootCreated;
AuVFSNode* pbox;

void PostBoxAdvanceIndex(PostBox* box) {
	if (box->full)
		box->tailIdx = (box->tailIdx + 1) % box->size;

	box->headIdx = (box->headIdx + 1) % box->size;
	box->full = (box->headIdx == box->tailIdx);
}

void PostBoxRetreat(PostBox* box) {
	box->full = false;
	box->tailIdx = (box->tailIdx + 1) % box->size;
}

bool IsPostBoxEmpty(PostBox* box) {
	return (!box->full && (box->headIdx == box->tailIdx));
}

bool IsPostBoxFull(PostBox* box) {
	return box->full;
}

extern uint64_t read_sp();

/**
 * @brief PostBoxCreate -- creates a postbox
 * @param root -- is this post box root ?
 * @param tid -- thread id
 */
void PostBoxCreate(bool root, uint16_t tid) {
	PostBox* box = (PostBox*)kmalloc(sizeof(PostBox));
	memset(box, 0, sizeof(PostBox));
	box->address = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(box->address, 0, PAGE_SIZE);

	if (root && !_PostBoxRootCreated) {
		box->ownerID = POSTBOX_ROOT_ID;
		_PostBoxRootCreated = true;
	} else {
		box->ownerID = tid;
	}

	box->next = NULL;
	box->prev = NULL;
	box->headIdx = 0;
	box->tailIdx = 0;
	box->full = false;

	box->size = PAGE_SIZE / ALIGN_UP(sizeof(PostEvent), 8);

	if (firstBox == NULL) {
		firstBox = box;
		lastBox = box;
	} else {
		lastBox->next = box;
		box->prev = lastBox;
		lastBox = box;
	}
	UARTDebugOut("Postbox created for threadID : %d of size : %d num msg \n", tid, box->size);
	UARTDebugOut("SP : %x \r\n", read_sp());
}

/**
 * @brief PostBoxDestroy -- destroy given postbox
 * @param box -- Pointer to postbox need to destroy
 */
void PostBoxDestroy(PostBox* box) {
	if (firstBox == NULL)
		return;

	if (box == firstBox)
		firstBox = firstBox->next;
	else
		box->prev->next = box->next;

	if (box == lastBox) {
		lastBox = box->prev;
	} else {
		box->next->prev = box->prev;
	}

	AuPmmngrReleasePage((uint64_t)V2P((size_t)box->address));
	kfree(box);
	UARTDebugOut("[postbox]: destroyed for id \r\n");
}

/**
 * @brief PostBoxDestroyByID -- destroys a post box identified by
 * an id
 * @param id -- id of the postbox
 */
void PostBoxDestroyByID(uint16_t id) {
	PostBox* destroyable = NULL;
	for (PostBox* box = firstBox; box != NULL; box = box->next) {
		if (box->ownerID == id) {
			destroyable = box;
			break;
		}
	}

	if (destroyable)
		PostBoxDestroy(destroyable);
	return;
}

extern void enscheddebug();
/**
 * @brief PostBoxPutEvent -- put an event to a specific post box
 * @param event -- Event to put
 */
void PostBoxPutEvent(PostEvent* event) {
	enscheddebug();
	uint16_t owner_id = event->to_id;
	for (PostBox* box = firstBox; box != NULL; box = box->next) {
		if (box->ownerID == owner_id) {
			if (!IsPostBoxFull(box)) {
				uint8_t* dest = (uint8_t*)box->address + (box->headIdx * POSTEVENT_SIZE_ALIGNED);
				//memcpy(&box->address[box->headIdx], event, sizeof(PostEvent));
				memcpy(dest, event, sizeof(PostEvent));
				PostBoxAdvanceIndex(box);
			}
			break;
		}
	}

	AA64Thread* thread = AuThreadFindByID(owner_id);
	if (!thread)
		thread = AuThreadFindByIDBlockList(owner_id);
	if (!thread)
		thread = AuThreadFindByIDSleepList(owner_id);

	if (thread != NULL) {
		/* sometime i don't know, this happens */
		if (strcmp(thread->name, "exec") == 0)
			return;
		/* the owner may be running on another core between finding its
		 * box empty and pausing: unpark wakes it if blocked, else leaves
		 * a token so that pause returns at once (no lost wakeup) */
		AuThreadUnpark(thread);
	}

	return;
}

/**
 * @brief PostBoxGetEvent -- get an event from post box and copy it to a
 * memory area
 * @param event -- pointer to a memory area
 * @param root -- is this post box is root
 * @param curr_thread -- Pointer to current thread
 */
int PostBoxGetEvent(PostEvent* event, bool root, AA64Thread* curr_thread) {
	int ret_code = POSTBOX_NO_EVENT;
	uint16_t owner_id = 0;
	if (root)
		owner_id = POSTBOX_ROOT_ID;
	else
		owner_id = curr_thread->thread_id;

	for (PostBox* box = firstBox; box != NULL; box = box->next) {
		if (box->ownerID == owner_id) {
			//UARTDebugOut("[postbox]: getting event : %d %d\r\n", box->ownerID, owner_id);
			if (!IsPostBoxEmpty(box)) {
				uint8_t* src = (uint8_t*)box->address + (box->tailIdx * POSTEVENT_SIZE_ALIGNED);
				//memcpy(event, &box->address[box->tailIdx], sizeof(PostEvent));
				memcpy(event, src, sizeof(PostEvent));
				memset(src, 0, sizeof(PostEvent));
				//memset(&box->address[box->tailIdx], 0, sizeof(PostEvent));
				PostBoxRetreat(box);
				ret_code = 1;
			}
			break;
		}
	}

	return ret_code;
}

/**
 * @brief PostBoxIOControl -- I/O Control function for
 * post box manager
 * @param file -- Pointer to postbox file
 * @param code -- Post Box command
 * @param arg -- extra data
 */
int PostBoxIOControl(AuVFSNode* file, int code, void* arg) {
	int ret_code = 1;
	AA64Thread* curr_thr = AuGetCurrentThread();
	if (!curr_thr)
		return 0;
	switch (code) {
	case POSTBOX_CREATE: {
		PostBoxCreate(false, curr_thr->thread_id);
		break;
	}
	case POSTBOX_CREATE_ROOT: {
		PostBoxCreate(true, curr_thr->thread_id);
		break;
	}
	case POSTBOX_DESTROY: {
		PostBoxDestroyByID(curr_thr->thread_id);
		break;
	}
	case POSTBOX_PUT_EVENT: {
		PostEvent* event = (PostEvent*)arg;
		//UARTDebugOut("PostBoxPutEvent : %s \r\n", curr_thr->name);
		PostBoxPutEvent(event);
		break;
	}

	case POSTBOX_GET_EVENT: {
		PostEvent* e = (PostEvent*)arg;
		ret_code = PostBoxGetEvent(e, false, curr_thr);
		break;
	}
	case POSTBOX_GET_EVENT_ROOT: {
		PostEvent* e = (PostEvent*)arg;
		ret_code = PostBoxGetEvent(e, true, curr_thr);
		break;
	}
	}

	return ret_code;
}

AuVFSNode* AuPostBOXGetNode() {
	return pbox;
}

/**
 * @brief AuIPCPostBoxInitialise -- initialise
 * the post box ipc manager
 */
void AuIPCPostBoxInitialise() {
	firstBox = NULL;
	lastBox = NULL;

	/** create the postbox file */

	/** I dont know, but the sixth item in the device fs list is
	 * the postbox file node, but miraculously 6th item disappers from 
	 * the list when opened from user space that's why we are creating
	 * a dummy node in the dev list
	 */
	AuVFSNode* dev = AuVFSFind("/dev");
	/*AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "dummy");
	aa64_data_cache_clean_range(&node->filename, 32);
	node->flags = FS_FLAG_DEVICE;
	node->device = dev;
	node->read = 0;
	node->write = 0;
	node->iocontrol = PostBoxIOControl;
	dsb_sy_barrier();
	AuDevFSAddFile(dev, "/", node);*/

	AuVFSNode* node2 = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node2, 0, sizeof(AuVFSNode));
	strcpy(node2->filename, "postbox");
	node2->flags = FS_FLAG_DEVICE;
	node2->device = dev;
	node2->read = 0;
	node2->write = 0;
	node2->uid = 0;
	node2->gid = AuCredGetGroupID(AURORA_GID_IPC_POSTBOX);
	node2->iocontrol = PostBoxIOControl;
	dsb_sy_barrier();
	AuDevFSAddFile(dev, "/", node2);

	//aa64_data_cache_clean_range(node, sizeof(AuVFSNode));
	pbox = node2;
	_PostBoxRootCreated = false;
	AuTextOut("[aurora]: PostBox IPC mounted and initialized to /dev/pbox \r\n");
	AuTextOut("[aurora]: PostEvent size : %d bytes \r\n", sizeof(PostEvent));
	AuTextOut("[aurora]: PostBox size aligned : %d number \r\n",
			  (4096 / ALIGN_UP(sizeof(PostEvent), 8)));
}