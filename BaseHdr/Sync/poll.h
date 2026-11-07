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

#ifndef __POLL_H__
#define __POLL_H__

#include <stdint.h>
#include <Sync/waitq.h>
#include <Hal/AA64/sched.h>

#define POLLIN	   0x001
#define POLLPRI	   0x002
#define POLLOUT	   0x004
#define POLLERR	   0x008
#define POLLHUP	   0x010
#define POLLNVAL   0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#define POLLWRNORM 0x100
#define POLLWRBAND 0x200

#define EPOLLIN			POLLIN
#define EPOLLPRI		POLLPRI
#define EPOLLOUT		POLLOUT
#define EPOLLERR		POLLERR
#define EPOLLHUP		POLLHUP
#define EPOLLRDNORM		POLLRDNORM
#define EPOLLWRNORM		POLLWRNORM
#define EPOLLONESHOT	(1u << 30)
#define EPOLLET			(1u << 31)
#define EPOLL_FLAG_BITS (EPOLLET | EPOLLONESHOT)

typedef unsigned long int nfds_t;

typedef struct AuPollTable {
	AA64Thread* thread;
	/* should have wait function here*/
	void* priv;
	AuWaitQEntry* chain;
	int error;
} AuPollTable;

typedef struct _user_poll_fd {
	int fd;		   /* file descriptor to poll*/
	short events;  /* requested events */
	short revents; /* returned events */
} _AuUser_PollFD;
/**
 * @brief AuPollWait -- helper function to create a wait queue
 * @param q -- Pointer to queue
 * @param pt -- Pointer to PollTable
 */
AU_EXTERN AU_EXPORT void AuPollWait(AuWaitQueue* q, AuPollTable* pt);
/**
 * @brief AuPoll -- poll system call 
 * @param fds -- Pointer to file descriptor array
 * @param num_fd -- number of file descriptor to monitor
 * @param timeout -- timeout in millisecond
 */
extern int AuPoll(_AuUser_PollFD fds[], nfds_t num_fd, int timeout);

#endif