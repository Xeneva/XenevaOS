/**
* @file capability.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
* All rights reserved.
* 
* Author:
*      Saankhya Srikanth, saankhyas18@gmail.com
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


#include <Cap/capability.h>
#include <process.h>
#include <string.h>
#include <aucon.h>
#include <_null.h>
#include <Fs/pipe.h>
#include <stdint.h>
#include <Drivers/uart.h>

/*
 * NOTE on concurrency: every existing syscall entry point in
 * Kernel/Serv/fileserv.cpp already runs with interrupts disabled
 * (x64_cli()) for the duration of the call, and XenevaOS's
 * multiprocessor scheduler is not active yet (see readme.md). That is
 * the same assumption these functions rely on -- callers are expected
 * to hold that same single-CPU-in-kernel invariant. Once SMP
 * scheduling lands this table will need its own lock (Phase 8 of the
 * roadmap calls this out explicitly under "race conditions" /
 * "concurrent capability lookup/removal") and every function below
 * should take it before touching proc->caps.
 */

/* internal helper: is `fd` a valid index into the capability table? */
static unsigned char cap_slot_in_range(int rq) {
	return (rq >= 0) && (rq < FILE_DESC_PER_PROCESS);
}

/* internal helper: slot pointer, or NULL if fd is out of range */
static AuCapability* cap_slot(AuProcess* proc, int fd) {
	if (!proc || !cap_slot_in_range(fd))
		return NULL;
	return &proc->caps[fd];
}

int BordoisilaCapCreate(void* procptr, int fd, void* object, uint8_t type, CapRights rights) {
	AuProcess* proc = (AuProcess*)procptr;
	AuCapability* cap = cap_slot(proc, fd);
	if (!cap)
		return CAP_ERR_INVALID;
	if (!object)
		return CAP_ERR_INVALID;

	cap->object = object;
	cap->object_type = type;
	/* a freshly minted capability may never exceed CAP_FILE_RIGHTS_MASK
	 * for the file object type in V0; this keeps a caller from handing
	 * in a bogus/garbage mask and getting more than the model allows */
	if (type == CAP_OBJ_FILE)
		cap->rights = rights & CAP_FILE_RIGHTS_MASK;
	else if (type == CAP_OBJ_PCIDEV)
		cap->rights = rights & CAP_HW_RIGHTS_MASK;
	else
		cap->rights = CAP_RIGHTS_NONE;

	cap->owner = proc->creds.uid;
	/* hardware handles belong to the process that opened them */
	cap->flags = (type == CAP_OBJ_PCIDEV) ? CAP_FLAG_NO_INHERIT : CAP_FLAG_NONE;
	cap->valid = true;
	return CAP_OK;
}

AuCapability* BordoisilaCapLookup(void* procptr, int fd) {
	AuProcess* proc = (AuProcess*)procptr;
	AuCapability* cap = cap_slot(proc, fd);
	if (!cap)
		return NULL;
	if (!cap->valid)
		return NULL;
	return cap;
}

bool BordoisilaCapCheckRights(void* procptr, int fd, CapRights required) {
	AuProcess* proc = (AuProcess*)procptr;
	AuCapability* cap = BordoisilaCapLookup(proc, fd);

	if (!cap)
		return false;

	/*AuTextOut("[CAP] Check fd=%d req=%x have=%x\r\n",
    fd, required, cap->rights);*/
	//UARTDebugOut("[CAP] Check fd=%d req=%x have=%x \r\n", fd, required, cap->rights);

	return (cap->rights & required) == required;
}

int BordoisilaCapDup(void* procptr, int oldfd, int newfd) {
	AuProcess* proc = (AuProcess*)procptr;
	AuCapability* src = BordoisilaCapLookup(proc, oldfd);
	if (!src)
		return CAP_ERR_INVALID;

	if (!cap_slot_in_range(newfd))
		return CAP_ERR_INVALID;

	if (proc->caps[newfd].valid)
		return CAP_ERR_INVALID;

	if (proc->fds[newfd])
		return CAP_ERR_INVALID;

	proc->fds[newfd] = proc->fds[oldfd];

	if (proc->fds[newfd]) {
		proc->fds[newfd]->fileCopyCount++;
		/* pipe2 and dup share one node. Each fd owns one pipe refcount. */
		if ((proc->fds[newfd]->flags & FS_FLAG_PIPE) && proc->fds[newfd]->device) {
			AuPipe* pipe = (AuPipe*)proc->fds[newfd]->device;
			pipe->refcount++;
		}
	}

	AuCapability* dst = &proc->caps[newfd];

	dst->object = src->object;
	dst->object_type = src->object_type;
	dst->rights = src->rights;
	dst->owner = src->owner;
	dst->flags = src->flags;
	dst->valid = true;

	return CAP_OK;
}

int BordoisilaCapRestrict(void* procptr, int fd, CapRights new_rights) {
	AuProcess* proc = (AuProcess*)procptr;
	AuCapability* cap = BordoisilaCapLookup(proc, fd);
	if (!cap)
		return CAP_ERR_INVALID;

	/* rights may only shrink: intersect rather than assign, so this
	 * can never be used to grant a bit the capability didn't already
	 * have (section 5: "rights can only become more restrictive") */
	cap->rights = cap->rights & new_rights;
	return CAP_OK;
}

void BordoisilaCapDestroy(void* procptr, int fd) {
	AuProcess* proc = (AuProcess*)procptr;

	AuCapability* cap = cap_slot(proc, fd);
	if (!cap || !cap->valid)
		return;

	if (cap->object_type == CAP_OBJ_PCIDEV)
		AuHwdevCapReleased(proc, cap);
	cap->object = NULL;
	cap->object_type = 0;
	cap->rights = CAP_RIGHTS_NONE;
	cap->owner = 0;
	cap->flags = CAP_FLAG_NONE;
	cap->valid = false;
}

void BordoisilaCapCleanupProcess(void* procptr) {
	AuProcess* proc = (AuProcess*)procptr;
	if (!proc)
		return;
	for (int i = 0; i < FILE_DESC_PER_PROCESS; i++) {
		AuCapability* cap = &proc->caps[i];
		if (cap->valid) {
			if (cap->object_type == CAP_OBJ_PCIDEV)
				AuHwdevCapReleased(proc, cap);
			cap->object = NULL;
			cap->object_type = 0;
			cap->rights = CAP_RIGHTS_NONE;
			cap->owner = 0;
			cap->flags = CAP_FLAG_NONE;
			cap->valid = false;
		}
	}
}

void BordoisilaCapInheritTable(void* parentptr, void* childptr) {
	if (!parentptr || !childptr)
		return;
	AuProcess* parent = (AuProcess*)parentptr;
	AuProcess* child = (AuProcess*)childptr;

	/* NOTE: as of this writing AuCreateProcessSlot()/AuCreateProcess()
	 * zero-initialize fds[] rather than copying the parent's open
	 * files, so there is currently nothing for capabilities to mirror
	 * at fork/spawn time -- this mirrors section 7 of the design doc
	 * ("similar to how file descriptors are already inherited") for
	 * whenever fd inheritance itself is implemented. Until then this
	 * only matters for slots the caller populates manually. Flagging
	 * this gap for design review rather than papering over it. */
	for (int i = 0; i < FILE_DESC_PER_PROCESS; i++) {
		AuCapability* src = &parent->caps[i];
		if (!src->valid)
			continue;
		if (src->flags & CAP_FLAG_NO_INHERIT)
			continue;
		if (!child->fds[i])
			continue;

		AuCapability* dst = &child->caps[i];
		dst->object = src->object;
		dst->object_type = src->object_type;
		/* rights never increase across inheritance */
		dst->rights = src->rights;
		dst->owner = child->creds.uid;
		dst->flags = src->flags;
		dst->valid = true;
	}
}