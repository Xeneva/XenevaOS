/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
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

#include <Net/socket.h>
#include <Hal/x86_64_hal.h>
#include <Hal/x86_64_sched.h>
#include <Hal/serial.h>
#include <process.h>


int NetSend(int sockfd, msghdr* msg, int flags) {
	x64_cli();
	if (!msg)
		return 1;
	AuThread *thr = AuGetCurrentThread();
	if (!thr)
		return 1;
	AuProcess *proc = AuProcessFindThread(thr);
	if (!proc){
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return 1;
	}
	AuVFSNode* node = proc->fds[sockfd];
	AuSocket* sock = (AuSocket*)node->device;
	if (!sock)
		return -1;
	if (sock->send)
		return sock->send(sock, msg, flags);
	return 1;
}

int NetReceive(int sockfd, msghdr *msg, int flags){
	x64_cli();
	if (!msg)
		return 1;
	AuThread *thr = AuGetCurrentThread();
	if (!thr)
		return 1;
	AuProcess *proc = AuProcessFindThread(thr);
	if (!proc){
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return 1;
	}
	AuVFSNode* node = proc->fds[sockfd];
	AuSocket* sock = (AuSocket*)node->device;
	if (!sock)
		return -1;
	if (sock->receive)
		return sock->receive(sock, msg, flags);
	return 1;
}

int NetConnect(int sockfd, sockaddr* addr, socklen_t addrlen) {
	x64_cli();
	AuThread* thr = AuGetCurrentThread();
	if (!thr)
		return 1;
	AuProcess *proc = AuProcessFindThread(thr);
	if (!proc){
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return 1;
	}

	AuVFSNode* node = proc->fds[sockfd];
	if (!node)
		return 0;
	AuSocket* sock = (AuSocket*)node->device;
	if (sock->connect)
		return sock->connect(sock, addr, addrlen);
	return 1;
}

int NetBind(int sockfd, sockaddr *addr, socklen_t addrlen){
	x64_cli();
	AuThread* thr = AuGetCurrentThread();
	if (!thr)
		return 1;
	AuProcess *proc = AuProcessFindThread(thr);
	if (!proc){
		proc = AuProcessFindSubThread(thr);
		if (!proc)
			return 1;
	}

	AuVFSNode* node = proc->fds[sockfd];
	if (!node)
		return 0;
	AuSocket* sock = (AuSocket*)node->device;
	if (sock->bind)
		return sock->bind(sock, addr, addrlen);
	return 1;
}

int NetAccept(int sockfd, sockaddr *addr, socklen_t * addrlen){
	return 0;
}

int NetListen(int sockfd, int backlog){
	return 0;
}