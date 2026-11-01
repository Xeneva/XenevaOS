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

#include <Net/socket.h>
#include <Net/unix.h>
#include <Net/aunet.h>
#include <bordoisila_ptr.h>
#include <process.h>
#include <timer.h>
#include <Drivers/uart.h>

#define NET_IOV_MAX     1024
#define NET_ADDR_MAX    256

static int NetValidateMsg(msghdr* msg, int for_send) {
	size_t i;

	if (!msg)
		return -AU_ERR_EFAULT;
	if (!__BORDOISILA_UBUF_VALIDATE(msg, sizeof(msghdr)))
		return -AU_ERR_EFAULT;
	if (msg->msg_iovlen > NET_IOV_MAX)
		return -AU_ERR_EINVAL;
	if (msg->msg_iovlen) {
		iovec* iov = msg->msg_iov;
		if (!iov ||
			!__BORDOISILA_UBUF_VALIDATE(iov, msg->msg_iovlen * sizeof(iovec)))
			return -AU_ERR_EFAULT;
		for (i = 0; i < msg->msg_iovlen; i++) {
			if (iov[i].iov_len &&
				!__BORDOISILA_UBUF_VALIDATE(iov[i].iov_base, iov[i].iov_len))
				return -AU_ERR_EFAULT;
		}
	}
	if (msg->msg_name && msg->msg_namelen) {
		if (!__BORDOISILA_UBUF_VALIDATE(msg->msg_name, msg->msg_namelen))
			return -AU_ERR_EFAULT;
	}
	if (msg->msg_control && msg->msg_controllen) {
		if (!__BORDOISILA_UBUF_VALIDATE(msg->msg_control, msg->msg_controllen))
			return -AU_ERR_EFAULT;
	}
	(void)for_send;
	return 0;
}

static int NetSendMsg(AuSocket* sock, msghdr* msg, int flags) {
	int rc;

	if (!sock)
		return -AU_ERR_EBADF;
	rc = NetValidateMsg(msg, 1);
	if (rc < 0)
		return rc;
	if (!sock->send)
		return -AU_ERR_EOPNOTSUPP;
	return sock->send(sock, msg, flags);
}

static int NetRecvMsg(AuSocket* sock, msghdr* msg, int flags) {
	int rc;

	if (!sock)
		return -AU_ERR_EBADF;
	rc = NetValidateMsg(msg, 0);
	if (rc < 0)
		return rc;
	if (!sock->receive)
		return -AU_ERR_EOPNOTSUPP;
	return sock->receive(sock, msg, flags);
}

int NetSend(int sockfd, msghdr* msg, int flags) {
	AuSocket* sock = AuSocketFromFd(sockfd);

	if (!sock)
		return -AU_ERR_EBADF;
	return NetSendMsg(sock, msg, flags);
}

int NetReceive(int sockfd, msghdr* msg, int flags) {
	AuSocket* sock = AuSocketFromFd(sockfd);

	if (!sock)
		return -AU_ERR_EBADF;
	return NetRecvMsg(sock, msg, flags);
}

int NetConnect(int sockfd, sockaddr* addr, socklen_t addrlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);

	if (!sock)
		return -AU_ERR_EBADF;
	if (!addr || addrlen < sizeof(sockaddr) || addrlen > NET_ADDR_MAX)
		return -AU_ERR_EINVAL;
	if (!__BORDOISILA_UBUF_VALIDATE(addr, addrlen))
		return -AU_ERR_EFAULT;
	if (!sock->connect)
		return -AU_ERR_EOPNOTSUPP;
	return sock->connect(sock, addr, addrlen);
}

int NetBind(int sockfd, sockaddr* addr, socklen_t addrlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);

	if (!sock)
		return -AU_ERR_EBADF;
	if (!addr || addrlen < sizeof(sockaddr) || addrlen > NET_ADDR_MAX)
		return -AU_ERR_EINVAL;
	if (!__BORDOISILA_UBUF_VALIDATE(addr, addrlen))
		return -AU_ERR_EFAULT;
	if (!sock->bind)
		return -AU_ERR_EOPNOTSUPP;
	return sock->bind(sock, addr, addrlen);
}

int NetAccept(int sockfd, sockaddr* addr, socklen_t* addrlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	socklen_t alen;

	if (!sock)
		return -AU_ERR_EBADF;
	if (!sock->accept)
		return -AU_ERR_EOPNOTSUPP;
	if (addr) {
		if (!addrlen || !__BORDOISILA_UBUF_VALIDATE(addrlen, sizeof(socklen_t)))
			return -AU_ERR_EFAULT;
		alen = *addrlen;
		if (alen > NET_ADDR_MAX)
			return -AU_ERR_EINVAL;
		if (alen && !__BORDOISILA_UBUF_VALIDATE(addr, alen))
			return -AU_ERR_EFAULT;
	}
	return sock->accept(sock, addr, addrlen);
}

int NetAccept4(int sockfd, sockaddr* addr, socklen_t* addrlen, int flags) {
	AuSocket* child;
	int fd;

	if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC))
		return -AU_ERR_EINVAL;
	fd = NetAccept(sockfd, addr, addrlen);
	if (fd < 0)
		return fd;
	child = AuSocketFromFd(fd);
	if (child) {
		child->nonblock = (flags & SOCK_NONBLOCK) ? 1 : child->nonblock;
		child->cloexec = (flags & SOCK_CLOEXEC) ? 1 : 0;
	}
	return fd;
}

int NetListen(int sockfd, int backlog) {
	AuSocket* sock = AuSocketFromFd(sockfd);

	if (!sock)
		return -AU_ERR_EBADF;
	if (!sock->listen)
		return -AU_ERR_EOPNOTSUPP;
	return sock->listen(sock, backlog);
}

int NetSocketSetOpt(int sockfd, int level, int optname, const void* optval,
	socklen_t optlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	int rc;

	if (!sock)
		return -AU_ERR_EBADF;
	if (optlen > 0) {
		if (!optval)
			return -AU_ERR_EFAULT;
		if (!__BORDOISILA_UBUF_VALIDATE(optval, optlen))
			return -AU_ERR_EFAULT;
	}
	rc = AuSocketSetOpt(sockfd, level, optname, optval, optlen);
	return rc;
}

int NetGetSockOpt(int sockfd, int level, int optname, void* optval,
	socklen_t* optlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	socklen_t len;

	if (!sock)
		return -AU_ERR_EBADF;
	if (!optlen || !__BORDOISILA_UBUF_VALIDATE(optlen, sizeof(socklen_t)))
		return -AU_ERR_EFAULT;
	len = *optlen;
	if (optval && len) {
		if (!__BORDOISILA_UBUF_VALIDATE(optval, len))
			return -AU_ERR_EFAULT;
	}
	return AuSocketGetOpt(sockfd, level, optname, optval, optlen);
}

int NetShutdown(int sockfd, int how) {
	AuSocket* sock = AuSocketFromFd(sockfd);

	if (!sock)
		return -AU_ERR_EBADF;
	if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
		return -AU_ERR_EINVAL;
	if (!sock->shutdown)
		return -AU_ERR_EOPNOTSUPP;
	return sock->shutdown(sock, how);
}

int NetSocketPair(int domain, int type, int protocol, int* sv) {
	int stype = type & SOCK_TYPE_MASK;
	int flags = type & ~SOCK_TYPE_MASK;
	int ksv[2];
	int rc;
	AuSocket* a;
	AuSocket* b;

	if (!sv || !__BORDOISILA_UBUF_VALIDATE(sv, 2 * sizeof(int)))
		return -AU_ERR_EFAULT;
	if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC))
		return -AU_ERR_EINVAL;
	if (domain != AF_UNIX)
		return -AU_ERR_EAFNOSUPPORT;

	rc = AuUnixCreatePair(stype, protocol, ksv);
	if (rc < 0)
		return rc;
	a = AuSocketFromFd(ksv[0]);
	b = AuSocketFromFd(ksv[1]);
	if (a) {
		a->nonblock = (flags & SOCK_NONBLOCK) ? 1 : 0;
		a->cloexec = (flags & SOCK_CLOEXEC) ? 1 : 0;
	}
	if (b) {
		b->nonblock = (flags & SOCK_NONBLOCK) ? 1 : 0;
		b->cloexec = (flags & SOCK_CLOEXEC) ? 1 : 0;
	}
	sv[0] = ksv[0];
	sv[1] = ksv[1];
	return 0;
}

static int NetValidateAddrOut(sockaddr* addr, socklen_t* addrlen) {
	socklen_t room;

	if (!addr || !addrlen)
		return -AU_ERR_EFAULT;
	if (!__BORDOISILA_UBUF_VALIDATE(addrlen, sizeof(socklen_t)))
		return -AU_ERR_EFAULT;
	room = *addrlen;
	if (room > NET_ADDR_MAX)
		room = NET_ADDR_MAX;
	if (room && !__BORDOISILA_UBUF_VALIDATE(addr, room))
		return -AU_ERR_EFAULT;
	return 0;
}

int NetGetSockName(int sockfd, sockaddr* addr, socklen_t* addrlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	int rc;

	if (!sock)
		return -AU_ERR_EBADF;
	rc = NetValidateAddrOut(addr, addrlen);
	if (rc < 0)
		return rc;
	if (!sock->getsockname)
		return -AU_ERR_EOPNOTSUPP;
	return sock->getsockname(sock, addr, addrlen);
}

int NetGetPeerName(int sockfd, sockaddr* addr, socklen_t* addrlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	int rc;

	if (!sock)
		return -AU_ERR_EBADF;
	rc = NetValidateAddrOut(addr, addrlen);
	if (rc < 0)
		return rc;
	if (!sock->getpeername)
		return -AU_ERR_EOPNOTSUPP;
	return sock->getpeername(sock, addr, addrlen);
}

int NetSendTo(int sockfd, const void* buf, size_t len, int flags,
	const sockaddr* addr, socklen_t addrlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	iovec iov;
	msghdr msg;

	if (!sock)
		return -AU_ERR_EBADF;
	if (!sock->send)
		return -AU_ERR_EOPNOTSUPP;
	if (len && (!buf || !__BORDOISILA_UBUF_VALIDATE(buf, len)))
		return -AU_ERR_EFAULT;
	if (addr) {
		if (addrlen < sizeof(sockaddr) || addrlen > NET_ADDR_MAX)
			return -AU_ERR_EINVAL;
		if (!__BORDOISILA_UBUF_VALIDATE(addr, addrlen))
			return -AU_ERR_EFAULT;
	}
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = (void*)buf;
	iov.iov_len = len;
	msg.msg_name = (void*)addr;
	msg.msg_namelen = addr ? addrlen : 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	return sock->send(sock, &msg, flags);
}

int NetRecvFrom(int sockfd, void* buf, size_t len, int flags,
	sockaddr* addr, socklen_t* addrlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	iovec iov;
	msghdr msg;
	socklen_t room = 0;
	int rc;

	if (!sock)
		return -AU_ERR_EBADF;
	if (!sock->receive)
		return -AU_ERR_EOPNOTSUPP;
	if (len && (!buf || !__BORDOISILA_UBUF_VALIDATE(buf, len)))
		return -AU_ERR_EFAULT;
	if (addr) {
		if (!addrlen)
			return -AU_ERR_EFAULT;
		if (!__BORDOISILA_UBUF_VALIDATE(addrlen, sizeof(socklen_t)))
			return -AU_ERR_EFAULT;
		room = *addrlen;
		if (room > NET_ADDR_MAX)
			room = NET_ADDR_MAX;
		if (room && !__BORDOISILA_UBUF_VALIDATE(addr, room))
			return -AU_ERR_EFAULT;
	}
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = buf;
	iov.iov_len = len;
	msg.msg_name = addr;
	msg.msg_namelen = addr ? room : 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	rc = sock->receive(sock, &msg, flags);
	if (rc >= 0 && addr && addrlen)
		*addrlen = msg.msg_namelen;
	return rc;
}

int NetSendMmsg(int sockfd, mmsghdr* msgvec, unsigned int vlen, int flags) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	unsigned int i;
	int sent = 0;

	if (!sock)
		return -AU_ERR_EBADF;
	if (!sock->send)
		return -AU_ERR_EOPNOTSUPP;
	if (vlen == 0 || vlen > NET_IOV_MAX)
		return -AU_ERR_EINVAL;
	if (!msgvec ||
		!__BORDOISILA_UBUF_VALIDATE(msgvec, vlen * sizeof(mmsghdr)))
		return -AU_ERR_EFAULT;
	for (i = 0; i < vlen; i++) {
		int rc = NetSendMsg(sock, &msgvec[i].msg_hdr, flags);

		if (rc < 0) {
			if (sent == 0)
				return rc;
			break;
		}
		msgvec[i].msg_len = (unsigned int)rc;
		sent++;
	}
	return sent;
}

/* XEClib's timespec uses 32-bit long fields on the PE target. */
typedef struct _au_net_timespec_ {
	int32_t tv_sec;
	int32_t tv_nsec;
} AuNetTimespec;

int NetRecvMmsg(int sockfd, mmsghdr* msgvec, unsigned int vlen, int flags,
	const void* timeout) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	AuNetTimespec ts;
	uint64_t deadline = 0;
	unsigned int i;
	int received = 0;

	if (!sock)
		return -AU_ERR_EBADF;
	if (!sock->receive)
		return -AU_ERR_EOPNOTSUPP;
	if (vlen == 0 || vlen > NET_IOV_MAX)
		return -AU_ERR_EINVAL;
	if (!msgvec ||
		!__BORDOISILA_UBUF_VALIDATE(msgvec, vlen * sizeof(mmsghdr)))
		return -AU_ERR_EFAULT;
	if (timeout) {
		if (!__BORDOISILA_UBUF_VALIDATE(timeout, sizeof(AuNetTimespec)))
			return -AU_ERR_EFAULT;
		memcpy(&ts, timeout, sizeof(ts));
		if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000)
			return -AU_ERR_EINVAL;
		deadline = AuGetCurrentUS() + (uint64_t)ts.tv_sec * 1000000ULL +
			(uint64_t)ts.tv_nsec / 1000ULL;
	}

	for (i = 0; i < vlen; i++) {
		int rflags = flags;

		/* MSG_WAITFORONE makes every entry after the first nonblocking;
		 * without it a blocking call waits for vlen messages just like
		 * repeated recvmsg(2) calls. */
		if (i > 0 && (flags & MSG_WAITFORONE))
			rflags |= MSG_DONTWAIT;

		for (;;) {
			int rc;

			/* With a timeout, poll nonblocking so the deadline can
			 * interrupt the receive. Without one, preserve the caller's
			 * blocking/MSG_WAITALL flags for the protocol. */
			if (timeout)
				rc = NetRecvMsg(sock, &msgvec[i].msg_hdr,
					rflags | MSG_DONTWAIT);
			else
				rc = NetRecvMsg(sock, &msgvec[i].msg_hdr, rflags);
			if (rc >= 0) {
				msgvec[i].msg_len = (unsigned int)rc;
				received++;
				break;
			}
			if (rc != -AU_ERR_EAGAIN && rc != -AU_ERR_ETIMEDOUT) {
				if (received)
					return received;
				return rc;
			}
			if ((rflags & MSG_DONTWAIT) || !timeout) {
				if (received)
					return received;
				return rc;
			}
			if (AuGetCurrentUS() >= deadline) {
				if (received)
					return received;
				return -AU_ERR_ETIMEDOUT;
			}
			AuSocketWaitSlice(&sock->rx_wait, 5);
		}
	}
	return received;
}

int NetIfNameToIndex(const char* ifname) {
	char name[AU_IFNAME_MAX];
	int i;

	if (!ifname)
		return -AU_ERR_EFAULT;
	for (i = 0; i < AU_IFNAME_MAX; i++) {
		if (!__BORDOISILA_UBUF_VALIDATE(ifname + i, 1))
			return -AU_ERR_EFAULT;
		name[i] = ifname[i];
		if (!name[i])
			return AuNetIfNameToIndex(name);
	}
	return -AU_ERR_EINVAL;
}

int NetGetIfAddrs(void* buf, int max) {
	int count;

	if (!buf || max <= 0 || max > AU_IFADDR_MAX)
		return -AU_ERR_EINVAL;
	if (!__BORDOISILA_UBUF_VALIDATE(buf,
		(size_t)max * sizeof(AuIfAddrInfo)))
		return -AU_ERR_EFAULT;
	count = AuNetSnapshotIfAddrs((AuIfAddrInfo*)buf, max);
	return count;
}
