/**
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

#include <Net/unix.h>
#include <Net/socket.h>
#include <Sync/poll.h>
#include <Mm/kmalloc.h>
#include <process.h>
#include <list.h>
#include <circbuf.h>
#include <string.h>
#include <stddef.h>
#include <Drivers/uart.h>
#include <Hal/AA64/sched.h>

#define UNIX_RX_BUF_SZ 65536
#define UNIX_SUN_PATH_OFF 2
#define UNIX_PATH_MAX 108
#define UNIX_WAIT_MS   30000

typedef struct _unix_pcb_ {
	uint8_t type;
	uint8_t bound;
	uint8_t listening;
	uint8_t peer_closed;
	/* Peer called shutdown(SHUT_WR): reads see EOF after draining, but
	 * the reverse direction stays open. */
	uint8_t peer_shut_wr;
	uint8_t connected;
	char path[108];
	int backlog;
	struct _socket_* peer;
	list_t* acceptq;
	CircBuffer* rxbuf;
	uint8_t* rxmem;
} UnixPCB;

typedef struct _unix_dgram_hdr_ {
	uint32_t len;
	char src[108];
} UnixDgramHdr;

static list_t* unix_sockets;

static void UnixFreeSocket(AuSocket* sock);

int AuUnixSend(AuSocket* sock, msghdr* msg, int flags);
int AuUnixReceive(AuSocket* sock, msghdr* msg, int flags);
int AuUnixConnect(AuSocket* sock, sockaddr* addr, socklen_t addrlen);
int AuUnixBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen);
int AuUnixListen(AuSocket* sock, int backlog);
int AuUnixAccept(AuSocket* sock, sockaddr* addr, socklen_t* addrlen);
int AuUnixPoll(AuSocket* sock, int events);
void AuUnixClose(AuSocket* sock);
int AuUnixFileClose(AuVFSNode* fsys, AuVFSNode* file);
static int AuUnixShutdown(AuSocket* sock, int how);
static int AuUnixGetSockName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen);
static int AuUnixGetPeerName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen);

static UnixPCB* UnixGetPCB(AuSocket* sock) {
	if (!sock)
		return NULL;
	return (UnixPCB*)sock->proto;
}

static void UnixEnsureRegistry(void) {
	if (!unix_sockets)
		unix_sockets = initialize_list();
}

static int UnixRegistered(AuSocket* sock) {
	int i;

	if (!unix_sockets || !sock)
		return 0;
	for (i = 0; i < (int)unix_sockets->pointer; i++) {
		if (list_get_at(unix_sockets, i) == sock)
			return 1;
	}
	return 0;
}

static void UnixUnregister(AuSocket* sock) {
	int i;

	if (!unix_sockets || !sock)
		return;
	for (i = 0; i < (int)unix_sockets->pointer; i++) {
		if (list_get_at(unix_sockets, i) == sock) {
			list_remove(unix_sockets, i);
			return;
		}
	}
}

static AuSocket* UnixAllocSocket(int type) {
	AuSocket* sock;
	UnixPCB* pcb;

	UnixEnsureRegistry();
	sock = AuNetCreateSocket();
	if (!sock)
		return NULL;
	pcb = (UnixPCB*)kmalloc(sizeof(UnixPCB));
	if (!pcb) {
		kfree(sock->rxstack);
		kfree(sock);
		return NULL;
	}
	memset(pcb, 0, sizeof(UnixPCB));
	pcb->type = (uint8_t)type;
	pcb->rxmem = (uint8_t*)kmalloc(UNIX_RX_BUF_SZ);
	if (!pcb->rxmem) {
		kfree(pcb);
		kfree(sock->rxstack);
		kfree(sock);
		return NULL;
	}
	memset(pcb->rxmem, 0, UNIX_RX_BUF_SZ);
	pcb->rxbuf = AuCircBufInitialise(pcb->rxmem, UNIX_RX_BUF_SZ);
	if (!pcb->rxbuf) {
		kfree(pcb->rxmem);
		kfree(pcb);
		kfree(sock->rxstack);
		kfree(sock);
		return NULL;
	}

	sock->proto = pcb;
	sock->family = AF_UNIX;
	sock->so_type = (uint8_t)type;
	sock->sockState = SOCK_STATE_CLOSED;
	sock->send = AuUnixSend;
	sock->receive = AuUnixReceive;
	sock->connect = AuUnixConnect;
	sock->bind = AuUnixBind;
	sock->listen = AuUnixListen;
	sock->accept = AuUnixAccept;
	sock->close = AuUnixClose;
	sock->shutdown = AuUnixShutdown;
	sock->getsockname = AuUnixGetSockName;
	sock->getpeername = AuUnixGetPeerName;
	sock->poll = AuUnixPoll;
	list_add(unix_sockets, sock);
	return sock;
}

static void UnixDetachPeer(AuSocket* sock) {
	UnixPCB* pcb = UnixGetPCB(sock);

	if (!pcb || !pcb->peer)
		return;
	{
		UnixPCB* peerpcb = UnixGetPCB(pcb->peer);
		if (peerpcb) {
			peerpcb->peer = NULL;
			peerpcb->peer_closed = 1;
			peerpcb->connected = 0;
			AuSocketWakeRx(pcb->peer);
			AuSocketWakeTx(pcb->peer);
		}
	}
	pcb->peer = NULL;
}

static void UnixDetachAllPeers(AuSocket* dead) {
	int i;

	if (!unix_sockets || !dead)
		return;
	for (i = 0; i < (int)unix_sockets->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(unix_sockets, i);
		UnixPCB* pcb = UnixGetPCB(sock);
		if (!sock || sock == dead || !pcb || pcb->peer != dead)
			continue;
		pcb->peer = NULL;
		pcb->peer_closed = 1;
		pcb->connected = 0;
		AuSocketWakeRx(sock);
		AuSocketWakeTx(sock);
	}
}

static void UnixFreeDgramQueue(AuSocket* sock) {
	if (!sock || !sock->rxstack)
		return;
	while (sock->rxstack->itemCount) {
		void* p = AuStackPop(sock->rxstack);
		if (p)
			kfree(p);
	}
}

static void UnixFreeSocket(AuSocket* sock) {
	UnixPCB* pcb = UnixGetPCB(sock);

	if (!sock)
		return;
	UnixUnregister(sock);
	UnixDetachAllPeers(sock);
	if (!pcb) {
		kfree(sock);
		return;
	}

	UnixDetachPeer(sock);

	if (pcb->acceptq) {
		while (pcb->acceptq->pointer) {
			AuSocket* child = (AuSocket*)list_remove(pcb->acceptq, 0);
			if (child)
				UnixFreeSocket(child);
		}
		kfree(pcb->acceptq);
		pcb->acceptq = NULL;
	}

	if (pcb->rxbuf)
		AuCircBufFree(pcb->rxbuf);
	if (pcb->rxmem)
		kfree(pcb->rxmem);
	pcb->rxbuf = NULL;
	pcb->rxmem = NULL;

	sock->proto = NULL;
	UnixFreeDgramQueue(sock);
	if (sock->rxstack)
		kfree(sock->rxstack);
	sock->rxstack = NULL;
	kfree(pcb);
	kfree(sock);
}

void AuUnixClose(AuSocket* sock) {
	UnixFreeSocket(sock);
}

int AuUnixFileClose(AuVFSNode* fsys, AuVFSNode* file) {
	(void)fsys;
	if (file) {
		AuSocket* sock = (AuSocket*)file->device;
		if (sock)
			UnixFreeSocket(sock);
		kfree(file);
	}
	return 0;
}

int AuUnixIsSocket(AuSocket* sock) {
	return sock && sock->family == AF_UNIX;
}

static int UnixPathFromSockaddr(char out[108], const sockaddr* addr, socklen_t addrlen) {
	const sockaddr_un* un;
	size_t max;
	size_t n;

	if (!addr || addrlen < 2 || addr->sa_family != AF_UNIX)
		return -AU_ERR_EAFNOSUPPORT;
	max = (size_t)addrlen - UNIX_SUN_PATH_OFF;
	if (max > UNIX_PATH_MAX)
		max = UNIX_PATH_MAX;
	if (max == 0)
		return -AU_ERR_EINVAL;
	un = (const sockaddr_un*)addr;
	n = 0;
	while (n < max && un->sun_path[n])
		n++;
	if (n == 0)
		return -AU_ERR_EINVAL;
	if (n >= UNIX_PATH_MAX)
		n = UNIX_PATH_MAX - 1;
	memcpy(out, un->sun_path, n);
	out[n] = '\0';
	return 0;
}

static AuSocket* UnixFindPath(const char* path, int type, int need_listen) {
	int i;
	size_t len;

	if (!unix_sockets || !path)
		return NULL;
	len = strlen(path);
	if (len == 0 || len >= 108)
		return NULL;
	for (i = 0; i < (int)unix_sockets->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(unix_sockets, i);
		UnixPCB* pcb = UnixGetPCB(sock);
		if (!pcb || !pcb->bound)
			continue;
		if ((int)pcb->type != type)
			continue;
		if (need_listen && !pcb->listening)
			continue;
		if (strcmp(pcb->path, path) == 0)
			return sock;
	}
	return NULL;
}

static size_t UnixIovTotal(const msghdr* msg) {
	size_t total = 0;
	size_t i;

	if (!msg)
		return 0;
	for (i = 0; i < msg->msg_iovlen; i++)
		total += msg->msg_iov[i].iov_len;
	return total;
}

static size_t UnixIovCopyIn(const msghdr* msg, uint8_t* dst, size_t max) {
	size_t done = 0;
	size_t i;

	for (i = 0; i < msg->msg_iovlen && done < max; i++) {
		size_t chunk = msg->msg_iov[i].iov_len;
		if (chunk > max - done)
			chunk = max - done;
		if (chunk && msg->msg_iov[i].iov_base) {
			memcpy(dst + done, msg->msg_iov[i].iov_base, chunk);
			done += chunk;
		}
	}
	return done;
}

static size_t UnixIovCopyOut(const msghdr* msg, const uint8_t* src, size_t len) {
	size_t done = 0;
	size_t i;

	for (i = 0; i < msg->msg_iovlen && done < len; i++) {
		size_t chunk = msg->msg_iov[i].iov_len;
		if (chunk > len - done)
			chunk = len - done;
		if (chunk && msg->msg_iov[i].iov_base) {
			memcpy(msg->msg_iov[i].iov_base, src + done, chunk);
			done += chunk;
		}
	}
	return done;
}

static int UnixTimerExpired(uint64_t deadline) {
	return AuGetCurrentUS() >= deadline;
}

static void UnixFillName(msghdr* msg, const char* path) {
	sockaddr_un un;

	if (!msg || !msg->msg_name)
		return;
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	if (path)
		strncpy(un.sun_path, path, sizeof(un.sun_path) - 1);
	AuSocketCopyMsgName(msg, &un, sizeof(un));
}

int AuUnixBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	UnixPCB* pcb = UnixGetPCB(sock);
	char path[108];
	int rc;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (pcb->bound)
		return -AU_ERR_EINVAL;
	rc = UnixPathFromSockaddr(path, addr, addrlen);
	if (rc < 0)
		return rc;
	if (UnixFindPath(path, pcb->type, 0))
		return -AU_ERR_EADDRINUSE;
	strncpy(pcb->path, path, sizeof(pcb->path) - 1);
	pcb->path[sizeof(pcb->path) - 1] = '\0';
	pcb->bound = 1;
	return 0;
}

int AuUnixListen(AuSocket* sock, int backlog) {
	UnixPCB* pcb = UnixGetPCB(sock);

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (pcb->type != SOCK_STREAM)
		return -AU_ERR_EOPNOTSUPP;
	if (!pcb->bound)
		return -AU_ERR_EINVAL;
	if (backlog <= 0)
		backlog = 1;
	if (backlog > SOMAXCONN)
		backlog = SOMAXCONN;
	pcb->backlog = backlog;
	if (!pcb->acceptq)
		pcb->acceptq = initialize_list();
	if (!pcb->acceptq)
		return -AU_ERR_EMFILE;
	pcb->listening = 1;
	sock->listening = 1;
	sock->sockState = SOCK_STATE_CONNECTED;
	return 0;
}

int AuUnixConnect(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	UnixPCB* pcb = UnixGetPCB(sock);
	AuSocket* listener;
	AuSocket* child;
	UnixPCB* childpcb;
	UnixPCB* lpcb;
	char path[108];
	int rc;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	rc = UnixPathFromSockaddr(path, addr, addrlen);
	if (rc < 0)
		return rc;

	listener = UnixFindPath(path, pcb->type, pcb->type == SOCK_STREAM);
	if (!listener) {
		if (pcb->type == SOCK_DGRAM)
			return -AU_ERR_ENOENT;
		return -AU_ERR_ECONNREFUSED;
	}
	lpcb = UnixGetPCB(listener);
	if (!lpcb)
		return -AU_ERR_ECONNREFUSED;
	if (pcb->type == SOCK_DGRAM) {
		pcb->peer = listener;
		pcb->connected = 1;
		sock->sockState = SOCK_STATE_CONNECTED;
		return 0;
	}
	if (!lpcb->listening || !lpcb->acceptq)
		return -AU_ERR_ECONNREFUSED;

	if (lpcb->acceptq->pointer >= (unsigned int)lpcb->backlog)
		return -AU_ERR_ECONNREFUSED;
	if (pcb->connected || pcb->peer)
		return -AU_ERR_EINVAL;
	if (!pcb->bound) {
		/* Clients are allowed to connect unnamed. */
		pcb->path[0] = '\0';
	}

	child = UnixAllocSocket(SOCK_STREAM);
	if (!child)
		return -AU_ERR_EMFILE;
	childpcb = UnixGetPCB(child);
	childpcb->peer = sock;
	childpcb->connected = 1;
	childpcb->bound = 0;
	child->sockState = SOCK_STATE_CONNECTED;

	pcb->peer = child;
	pcb->connected = 1;
	sock->sockState = SOCK_STATE_CONNECTED;
	sock->so_error = 0;
	list_add(lpcb->acceptq, child);
	AuSocketWakeAccept(listener);
	return 0;
}

int AuUnixAccept(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	UnixPCB* pcb = UnixGetPCB(sock);
	AuSocket* child;
	UnixPCB* childpcb;
	uint64_t deadline;
	int fd;

	if (!pcb || !pcb->listening || !pcb->acceptq)
		return -AU_ERR_EINVAL;
	if (pcb->acceptq->pointer == 0) {
		if (sock->nonblock)
			return -AU_ERR_EAGAIN;
		deadline = AuGetCurrentUS() + (uint64_t)UNIX_WAIT_MS * 1000ULL;
		while (pcb->acceptq->pointer == 0) {
			if (UnixTimerExpired(deadline))
				return -AU_ERR_ETIMEDOUT;
			AuSocketWaitSlice(&sock->accept_wait, AU_SOCKET_WAIT_SLICE_MS);
		}
	}

	child = (AuSocket*)list_remove(pcb->acceptq, 0);
	if (!child)
		return -AU_ERR_EAGAIN;
	childpcb = UnixGetPCB(child);
	fd = AuSocketInstallFd(child, "unix", AuUnixFileClose);
	if (fd < 0) {
		UnixFreeSocket(child);
		return fd;
	}

	if (addr && addrlen) {
		sockaddr_un un;
		UnixPCB* peerpcb = (childpcb && childpcb->peer) ?
			UnixGetPCB(childpcb->peer) : NULL;

		memset(&un, 0, sizeof(un));
		un.sun_family = AF_UNIX;
		if (peerpcb)
			strncpy(un.sun_path, peerpcb->path,
				sizeof(un.sun_path) - 1);
		AuSocketCopyAddr(addr, addrlen, &un, sizeof(un));
	}
	return fd;
}

static int UnixDgramSend(AuSocket* sock, UnixPCB* pcb, msghdr* msg, int flags) {
	(void)sock;
	AuSocket* dest = NULL;
	UnixPCB* dpcb;
	UnixDgramHdr* hdr;
	uint8_t* blob;
	char path[108];
	size_t total;
	int rc;

	(void)flags;
	if (sock->shut_wr)
		return -AU_ERR_EPIPE;
	memset(path, 0, sizeof(path));
	if (msg->msg_name && msg->msg_namelen) {
		rc = UnixPathFromSockaddr(path, (sockaddr*)msg->msg_name, msg->msg_namelen);
		if (rc < 0)
			return rc;
		dest = UnixFindPath(path, SOCK_DGRAM, 0);
		if (!dest)
			return -AU_ERR_ENOENT;
	} else if (pcb->peer) {
		dest = pcb->peer;
	} else if (pcb->peer_closed) {
		return -AU_ERR_ECONNREFUSED;
	} else {
		return -AU_ERR_EDESTADDRREQ;
	}
	dpcb = UnixGetPCB(dest);
	if (!dpcb || dpcb->type != SOCK_DGRAM)
		return -AU_ERR_ECONNREFUSED;

	total = UnixIovTotal(msg);
	if (total > 65507)
		return -AU_ERR_EMSGSIZE;
	blob = (uint8_t*)kmalloc(sizeof(UnixDgramHdr) + total);
	if (!blob)
		return -AU_ERR_EMFILE;
	hdr = (UnixDgramHdr*)blob;
	memset(hdr, 0, sizeof(UnixDgramHdr));
	hdr->len = (uint32_t)total;
	strncpy(hdr->src, pcb->path, sizeof(hdr->src) - 1);
	UnixIovCopyIn(msg, blob + sizeof(UnixDgramHdr), total);
	AuStackPush(dest->rxstack, blob);
	AuSocketWakeRx(dest);
	return (int)total;
}

static int UnixDgramReceive(AuSocket* sock, msghdr* msg, int flags) {
	uint64_t deadline = AuGetCurrentUS() + (uint64_t)UNIX_WAIT_MS * 1000ULL;

	if (sock->shut_rd)
		return 0;

	for (;;) {
		UnixDgramHdr* hdr;
		uint8_t* blob;
		size_t len;
		size_t copied;

		if (sock->rxstack && sock->rxstack->itemCount) {
			blob = (uint8_t*)AuStackPop(sock->rxstack);
			if (!blob)
				return -AU_ERR_EAGAIN;
			hdr = (UnixDgramHdr*)blob;
			len = hdr->len;
			if (len > UNIX_RX_BUF_SZ)
				len = UNIX_RX_BUF_SZ;
			copied = UnixIovCopyOut(msg, blob + sizeof(UnixDgramHdr), len);
			if (copied < hdr->len)
				msg->msg_flags |= MSG_TRUNC;
			UnixFillName(msg, hdr->src);
			kfree(blob);
			return (int)copied;
		}
		if (sock->nonblock || (flags & MSG_DONTWAIT))
			return -AU_ERR_EAGAIN;
		if (UnixTimerExpired(deadline))
			return -AU_ERR_ETIMEDOUT;
		AuSocketWaitSlice(&sock->rx_wait, AU_SOCKET_WAIT_SLICE_MS);
	}
}

static int UnixStreamSend(AuSocket* sock, UnixPCB* pcb, msghdr* msg, int flags) {
	UnixPCB* peercb;
	uint8_t* flat;
	size_t total;
	size_t sent = 0;
	uint64_t deadline;

	if (!pcb->peer || pcb->peer_closed)
		return -AU_ERR_EPIPE;
	if (sock->shut_wr)
		return -AU_ERR_EPIPE;
	peercb = UnixGetPCB(pcb->peer);
	if (!peercb || !peercb->rxbuf)
		return -AU_ERR_EPIPE;

	total = UnixIovTotal(msg);
	if (total == 0)
		return 0;
	if (total > (1024 * 1024))
		return -AU_ERR_EMSGSIZE;
	flat = (uint8_t*)kmalloc(total);
	if (!flat)
		return -AU_ERR_EMFILE;
	UnixIovCopyIn(msg, flat, total);

	deadline = AuGetCurrentUS() + (uint64_t)UNIX_WAIT_MS * 1000ULL;
	while (sent < total) {
		size_t used = AuCircBufSize(peercb->rxbuf);
		size_t avail = UNIX_RX_BUF_SZ - used;
		size_t chunk;
		size_t wrote;

		if (avail == 0) {
			if (sock->nonblock || (flags & MSG_DONTWAIT)) {
				if (sent)
					break;
				kfree(flat);
				return -AU_ERR_EAGAIN;
			}
			if (UnixTimerExpired(deadline))
				break;
			AuSocketWaitSlice(&sock->tx_wait, AU_SOCKET_WAIT_SLICE_MS);
			continue;
		}
		chunk = total - sent;
		if (chunk > avail)
			chunk = avail;
		wrote = AuCircBufWrite(peercb->rxbuf, flat + sent, chunk);
		if (wrote == 0)
			break;
		sent += wrote;
		AuSocketWakeRx(pcb->peer);
	}
	kfree(flat);
	AuSocketWakeRx(pcb->peer);
	if (sent == 0 && total > 0)
		return -AU_ERR_ETIMEDOUT;
	return (int)sent;
}

static int UnixStreamReceive(AuSocket* sock, UnixPCB* pcb, msghdr* msg, int flags) {
	uint64_t deadline = AuGetCurrentUS() + (uint64_t)UNIX_WAIT_MS * 1000ULL;

	if (sock->shut_rd)
		return 0;

	for (;;) {
		size_t got = 0;
		size_t i;

		if (pcb->rxbuf && !CircBufEmpty(pcb->rxbuf)) {
			for (i = 0; i < msg->msg_iovlen; i++) {
				size_t len = msg->msg_iov[i].iov_len;
				if (len == 0 || !msg->msg_iov[i].iov_base)
					continue;
				got += AuCircBufRead(pcb->rxbuf, (uint8_t*)msg->msg_iov[i].iov_base, len);
			}
			if (pcb->peer)
				AuSocketWakeTx(pcb->peer);
			return (int)got;
		}
		if (pcb->peer_closed || pcb->peer_shut_wr)
			return 0;
		if (sock->nonblock || (flags & MSG_DONTWAIT))
			return -AU_ERR_EAGAIN;
		if (UnixTimerExpired(deadline))
			return -AU_ERR_ETIMEDOUT;
		AuSocketWaitSlice(&sock->rx_wait, AU_SOCKET_WAIT_SLICE_MS);
	}
}

int AuUnixSend(AuSocket* sock, msghdr* msg, int flags) {
	UnixPCB* pcb = UnixGetPCB(sock);

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (!msg || msg->msg_iovlen == 0)
		return 0;
	if (pcb->type == SOCK_DGRAM)
		return UnixDgramSend(sock, pcb, msg, flags);
	if (pcb->type == SOCK_STREAM)
		return UnixStreamSend(sock, pcb, msg, flags);
	return -AU_ERR_EOPNOTSUPP;
}

int AuUnixReceive(AuSocket* sock, msghdr* msg, int flags) {
	UnixPCB* pcb = UnixGetPCB(sock);

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (!msg || msg->msg_iovlen == 0)
		return 0;
	if (pcb->type == SOCK_DGRAM)
		return UnixDgramReceive(sock, msg, flags);
	if (pcb->type == SOCK_STREAM)
		return UnixStreamReceive(sock, pcb, msg, flags);
	return -AU_ERR_EOPNOTSUPP;
}

int AuUnixPoll(AuSocket* sock, int events) {
	UnixPCB* pcb = UnixGetPCB(sock);
	int mask = 0;

	(void)events;
	if (!pcb)
		return POLLNVAL;
	if (pcb->type == SOCK_DGRAM) {
		if (sock->shut_rd)
			mask |= POLLIN;
		else if (sock->rxstack && sock->rxstack->itemCount)
			mask |= POLLIN;
		if (!sock->shut_wr)
			mask |= POLLOUT;
	} else if (pcb->listening) {
		if (pcb->acceptq && pcb->acceptq->pointer)
			mask |= POLLIN;
	} else {
		if (sock->shut_rd)
			mask |= POLLIN;
		else if (pcb->rxbuf && !CircBufEmpty(pcb->rxbuf))
			mask |= POLLIN;
		if (pcb->peer_closed)
			mask |= POLLIN | POLLHUP;
		else if (pcb->peer_shut_wr)
			mask |= POLLIN;
		if (!sock->shut_wr && pcb->peer && !pcb->peer_closed)
			mask |= POLLOUT;
	}
	if (sock->so_error)
		mask |= POLLERR;
	return mask;
}

static int AuUnixGetSockName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	UnixPCB* pcb = UnixGetPCB(sock);
	sockaddr_un un;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	if (pcb->bound)
		strncpy(un.sun_path, pcb->path, sizeof(un.sun_path) - 1);
	return AuSocketCopyAddr(addr, addrlen, &un, sizeof(un));
}

static int AuUnixGetPeerName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	UnixPCB* pcb = UnixGetPCB(sock);
	UnixPCB* peerpcb = NULL;
	sockaddr_un un;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (pcb->listening || !pcb->peer)
		return -AU_ERR_ENOTCONN;
	peerpcb = UnixGetPCB(pcb->peer);
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	if (peerpcb)
		strncpy(un.sun_path, peerpcb->path, sizeof(un.sun_path) - 1);
	return AuSocketCopyAddr(addr, addrlen, &un, sizeof(un));
}

int AuUnixCreateSocket(int type, int protocol) {
	AuSocket* sock;
	int fd;

	if (protocol != 0)
		return -AU_ERR_EINVAL;
	if (type != SOCK_STREAM && type != SOCK_DGRAM)
		return -AU_ERR_EOPNOTSUPP;
	sock = UnixAllocSocket(type);
	if (!sock)
		return -AU_ERR_EMFILE;
	fd = AuSocketInstallFd(sock, "unix", AuUnixFileClose);
	if (fd < 0) {
		UnixFreeSocket(sock);
		return fd;
	}
	return fd;
}

/*
 * AuUnixShutdown -- half-close an AF_UNIX socket.
 *
 * For streams, SHUT_WR sets peer_shut_wr on the peer rather than tearing
 * the link down: the peer reads EOF once its buffer drains but can keep
 * writing back. SHUT_RD only affects the local receive path. Datagrams
 * have no wire FIN; the flags are local to the connected pair.
 */
static int AuUnixShutdown(AuSocket* sock, int how) {
	UnixPCB* pcb = UnixGetPCB(sock);

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
		return -AU_ERR_EINVAL;
	if (pcb->listening)
		return -AU_ERR_ENOTCONN;
	if (!pcb->peer && !pcb->connected)
		return -AU_ERR_ENOTCONN;

	if (how == SHUT_RD || how == SHUT_RDWR) {
		sock->shut_rd = 1;
		AuSocketWakeRx(sock);
	}
	if (how == SHUT_WR || how == SHUT_RDWR) {
		sock->shut_wr = 1;
		if (pcb->type == SOCK_STREAM && pcb->peer) {
			UnixPCB* peerpcb = UnixGetPCB(pcb->peer);
			if (peerpcb) {
				peerpcb->peer_shut_wr = 1;
				AuSocketWakeRx(pcb->peer);
			}
		}
	}
	return 0;
}

/* Undo the first socketpair install when the second fd cannot be
 * allocated. Kept local so the generic socket layer stays unaware of
 * partially completed multi-fd syscalls. */
static void UnixRollbackFd(int fd) {
	AA64Thread* thr;
	AuProcess* proc;
	AuVFSNode* node;

	if (fd < 0 || fd >= FILE_DESC_PER_PROCESS)
		return;
	thr = AuGetCurrentThread();
	if (!thr)
		return;
	proc = AuProcessFindThread(thr);
	if (!proc)
		proc = AuProcessFindSubThread(thr);
	if (!proc)
		return;
	node = proc->fds[fd];
	if (!node)
		return;
	proc->fds[fd] = NULL;
	AuUnixFileClose(NULL, node);
}

int AuUnixCreatePair(int type, int protocol, int sv[2]) {
	AuSocket* a;
	AuSocket* b;
	UnixPCB* apcb;
	UnixPCB* bpcb;
	int fda;
	int fdb;

	if (!sv)
		return -AU_ERR_EINVAL;
	if (protocol != 0)
		return -AU_ERR_EINVAL;
	if (type != SOCK_STREAM && type != SOCK_DGRAM)
		return -AU_ERR_EOPNOTSUPP;

	a = UnixAllocSocket(type);
	if (!a)
		return -AU_ERR_EMFILE;
	b = UnixAllocSocket(type);
	if (!b) {
		UnixFreeSocket(a);
		return -AU_ERR_EMFILE;
	}

	apcb = UnixGetPCB(a);
	bpcb = UnixGetPCB(b);
	apcb->peer = b;
	bpcb->peer = a;
	apcb->connected = 1;
	bpcb->connected = 1;
	a->sockState = SOCK_STATE_CONNECTED;
	b->sockState = SOCK_STATE_CONNECTED;

	fda = AuSocketInstallFd(a, "unix", AuUnixFileClose);
	if (fda < 0) {
		UnixFreeSocket(a);
		UnixFreeSocket(b);
		return fda;
	}
	fdb = AuSocketInstallFd(b, "unix", AuUnixFileClose);
	if (fdb < 0) {
		UnixFreeSocket(b);
		UnixRollbackFd(fda);
		return fdb;
	}
	sv[0] = fda;
	sv[1] = fdb;
	return 0;
}
