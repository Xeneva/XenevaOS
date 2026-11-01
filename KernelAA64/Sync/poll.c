/**
* BSD 2-Clause License
*
* Copyright (c) 2022 - 2026, Manas Kamal Choudhury
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

#include <Sync/poll.h>
#include <bordoisila_ptr.h>
#include <Log/klog.h>
#include <Log/_print.h>
#include <Hal/AA64/sched.h>
#include <timer.h>
#include <Fs/vfs.h>
#include <process.h>
#include <Drivers/uart.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <fixup.h>

#define POLL_MAX_FD_MONITOR 1024

AuVFSNode* AuPollGetFile(AA64Thread* thr, int fd) {
	if (!thr)
		return NULL;
	if (fd == -1)
		return NULL;
	AuProcess* proc = AuProcessFindThread(thr);
	if (!proc) {
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return NULL;
	}

	if (fd >= FILE_DESC_PER_PROCESS)
		return NULL;
	AuVFSNode* file = proc->fds[fd];
	if (!file) //bad file descriptor
		return NULL;
	return file;
}

/**
 * @brief AuPollWait -- helper function to create a wait queue
 * @param q -- Pointer to queue
 * @param pt -- Pointer to PollTable
 */
void AuPollWait(AuWaitQueue* q, AuPollTable* pt) {
	if (!q || !pt)
		return;
	AuWaitQEntry* e = (AuWaitQEntry*)kmalloc(sizeof(AuWaitQEntry));
	if (!e) {
		pt->error = -1;
		return;
	}
	memset(e, 0, sizeof(AuWaitQEntry));
	e->thread = pt->thread;
	//e->fn = pt->fn;
	e->prev = pt->priv;
	e->owner_next = pt->chain;
	pt->chain = e;
	AuWaitQueueAdd(q, e);
}

/**
 * @brief AuPoll -- poll system call 
 * @param fds -- Pointer to file descriptor array
 * @param num_fd -- number of file descriptor to monitor
 * @param timeout -- timeout in millisecond
 */
int AuPoll(_AuUser_PollFD fds[], nfds_t num_fd, int timeout) {
	if (!fds)
		return -1;

	if (num_fd > POLL_MAX_FD_MONITOR)
		return -1;

	if (!__BORDOISILA_UBUF_VALIDATE(fds, sizeof(_AuUser_PollFD) * num_fd)) {
		BPrintK(BORDOISILA_ERROR, "AuPoll: number of fd pointer out of range \r\n");
		return -1;
	}

	AA64Thread* curr_thr = AuGetCurrentThread();

	_AuUser_PollFD* _kfds = (_AuUser_PollFD*)kmalloc(sizeof(_AuUser_PollFD) * num_fd);
	memset(_kfds, 0, sizeof(_AuUser_PollFD) * num_fd);

	/** copy it from user */
	if (__bordoisila_copy_from_user(_kfds, fds, sizeof(_AuUser_PollFD) * num_fd) != 0) {
		kfree(_kfds);
		return -1;
	}

	AuPollTable pt;
	pt.thread = curr_thr;
	pt.error = 0;
	pt.chain = NULL;
	pt.priv = NULL;
	AuPollTable* config = &pt;

	uint64_t deadline = 0;
	if (timeout > 0)
		deadline = AuGetCurrentUS() + (uint64_t)timeout * 1000ULL;

	int ready = 0;
	uint16_t mask = 0;
	while (1) {
		/** check all file descriptors */
		for (int i = 0; i < num_fd; i++) {
			mask = 0;
			_kfds[i].revents = 0;
			if (fds[i].fd < 0)
				continue;
			AuVFSNode* file = AuPollGetFile(curr_thr, _kfds[i].fd);
			if (!file)
				mask = POLLNVAL;
			else if (file->poll) {
				mask = file->poll(file, file, config);
			} else
				mask = POLLIN | POLLOUT; // regular file

			/** the poll system call suggest that it revent should have
            *  POLLERR | POLLHUP | POLLNVAL present
            */
			mask &= (uint16_t)fds[i].events | POLLERR | POLLHUP | POLLNVAL;
			fds[i].revents = mask;
			if (mask)
				ready++;
		}
		config = NULL;
		if (ready > 0)
			break;

		/** check for timeout value , 0 means non-blocking and non sleeping, 
     * -1 means forever
    */
		if (timeout == 0)
			break;

		if (timeout > 0 && AuGetCurrentUS() >= deadline)
			break;

		/** we should also check for signal, but skipping for now */

		if (timeout > 0) {
			uint64_t now = AuGetCurrentUS();
			uint64_t ms = (deadline > now) ?
				(deadline - now + 999ULL) / 1000ULL : 1;
			curr_thr->blocked_on_poll = 1;
			AuSleepThread(curr_thr, ms);
		} else {
			curr_thr->blocked_on_poll = 1;
			AuBlockThread(curr_thr);
		}
		AuScheduleNext();
		curr_thr->blocked_on_poll = 0;
	}

	kfree(_kfds);

	while (pt.chain) {
		AuWaitQEntry* next = pt.chain->owner_next;
		AuWaitQueueRemove(pt.chain);
		kfree(pt.chain);
		pt.chain = next;
	}

	return ready;
}
