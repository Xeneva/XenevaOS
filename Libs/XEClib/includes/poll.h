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

#include <_xeneva.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

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

#ifndef POLLMSG
#define POLLMSG	  0x400
#define POLLRDHUP 0x2000
#endif
	typedef unsigned long nfds_t;

	struct pollfd {
		int fd;
		short events;
		short revents;
	};

	/**
     * @brief poll -- monitor multiple file descriptors
     * for events
     * @param fds -- Pointer to pollfd descriptor
     * @param num_fd -- number of file descriptor to monitor
     * @param timeout -- number of ms to count,
     * timout -1 = forever, 0 means non_block 
     */
	XE_LIB int poll(struct pollfd * fds, nfds_t num_fd, int timeout);

#ifdef __cplusplus
}
#endif

#endif