/**
* @file icmpv6.c
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

#include <Net/icmpv6.h>
#include <Net/ndp.h>
#include <Net/socket.h>
#include <Net/aunet.h>
#include <Net/ipv6.h>
#include <process.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <_null.h>
#include <Drivers/uart.h>
#include <Hal/AA64/sched.h>
#include <Sync/poll.h>

AuSocket* current_icmpv6_sock;

void ICMPv6Initialise() {
	current_icmpv6_sock = 0;
}

void AuICMPv6Handle(IPv6Header* ipv6, AuVFSNode* nic) {
	ICMPv6Header* header;
	AuNetworkDevice* netdev;
	uint16_t payloadLen;
	size_t totalLen;
	IPv6Header* resp;
	ICMPv6Header* reply;

	if (!ipv6 || !nic)
		return;

	payloadLen = ntohs(ipv6->payloadLen);
	if (payloadLen < sizeof(ICMPv6Header))
		return;

	header = (ICMPv6Header*)&ipv6->payload;
	netdev = (AuNetworkDevice*)nic->device;
	if (!netdev)
		return;

	switch (header->type) {
	case ICMPV6_ND_NS:
		NDHandleNeighborSolicit(ipv6, nic);
		break;
	case ICMPV6_ND_NA:
		NDHandleNeighborAdvert(ipv6, nic);
		break;
	case ICMPV6_ECHO_REQUEST: {
		if (ip6_addr_is_zero(&netdev->ipv6addr))
			return;

		totalLen = sizeof(IPv6Header) + payloadLen;
		resp = (IPv6Header*)kmalloc(totalLen);
		if (!resp)
			return;
		memcpy(resp, ipv6, totalLen);
		ip6_addr_copy(&resp->destIP, &ipv6->srcIP);
		ip6_addr_copy(&resp->srcIP, &netdev->ipv6addr);
		resp->hopLimit = 64;
		resp->nextHeader = IPV6_NEXT_ICMPV6;
		resp->payloadLen = htons(payloadLen);

		reply = (ICMPv6Header*)&resp->payload;
		reply->type = ICMPV6_ECHO_REPLY;
		reply->code = 0;
		reply->checksum = 0;
		reply->checksum = htons(IPv6PseudoChecksum(&resp->srcIP, &resp->destIP,
			payloadLen, IPV6_NEXT_ICMPV6, reply, payloadLen));

		IPV6SendPacket(resp, nic);
		kfree(resp);
		break;
	}
	case ICMPV6_ECHO_REPLY:
		if (current_icmpv6_sock) {
			AuSocketAdd(current_icmpv6_sock, ipv6, (size_t)(sizeof(IPv6Header) + payloadLen));
			AuSocketWakeRx(current_icmpv6_sock);
		}
		break;
	default:
		break;
	}
}

static size_t ICMPv6IovCopyIn(const msghdr* msg, uint8_t* dst, size_t max) {
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

static size_t ICMPv6IovCopyOut(const msghdr* msg, const uint8_t* src, size_t len) {
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

static int AuICMPv6Receive(AuSocket* sock, msghdr* msg, int flags) {
	uint64_t deadline;

	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (!msg || msg->msg_iovlen == 0)
		return 0;

	deadline = AuGetCurrentUS() + (uint64_t)30000 * 1000ULL;
	for (;;) {
		char* packet;

		AuNetRxPoll();
		packet = (char*)AuSocketGet(sock);
		if (packet) {
			size_t stored = *(size_t*)packet;
			IPv6Header* src = (IPv6Header*)(packet + sizeof(size_t));
			size_t plen = stored > sizeof(IPv6Header) ?
				stored - sizeof(IPv6Header) : 0;
			size_t copied;

			copied = ICMPv6IovCopyOut(msg, src->payload, plen);
			if (copied < plen)
				msg->msg_flags |= MSG_TRUNC;

			if (msg->msg_name && msg->msg_namelen >= sizeof(sockaddr_in6)) {
				sockaddr_in6* name = (sockaddr_in6*)msg->msg_name;
				name->sin6_family = AF_INET6;
				name->sin6_port = 0;
				name->sin6_flowinfo = 0;
				memcpy(name->sin6_addr.s6_addr, src->srcIP.s6_addr, 16);
				/* Carry hop limit for ping (same idea as IPv4 sin_zero[0]). */
				name->sin6_scope_id = src->hopLimit;
				msg->msg_namelen = sizeof(sockaddr_in6);
			}
			kfree(packet);
			return (int)copied;
		}

		if (sock->nonblock || (flags & MSG_DONTWAIT))
			return -AU_ERR_EAGAIN;
		if (AuGetCurrentUS() >= deadline)
			return -AU_ERR_ETIMEDOUT;
		AuSocketWaitSlice(&sock->rx_wait, AU_SOCKET_WAIT_SLICE_MS);
	}
}

static int AuICMPv6Send(AuSocket* sock, msghdr* msg, int flags) {
	sockaddr_in6* name;
	AuVFSNode* nic;
	AuNetworkDevice* netdev;
	size_t payload;
	size_t totalLen;
	IPv6Header* pkt;
	uint16_t payloadLen;

	(void)sock;
	(void)flags;
	if (!msg || msg->msg_iovlen == 0)
		return 0;
	if (!msg->msg_name || msg->msg_namelen < sizeof(sockaddr_in6))
		return -AU_ERR_EDESTADDRREQ;

	name = (sockaddr_in6*)msg->msg_name;
	if (name->sin6_family != AF_INET6)
		return -AU_ERR_EAFNOSUPPORT;

	payload = 0;
	for (size_t i = 0; i < msg->msg_iovlen; i++)
		payload += msg->msg_iov[i].iov_len;
	if (payload > 65527)
		return -AU_ERR_EMSGSIZE;

	nic = AuNetworkRoute6((const ip6_addr*)&name->sin6_addr);
	if (!nic)
		return -AU_ERR_ENETUNREACH;
	netdev = (AuNetworkDevice*)nic->device;
	if (!netdev || ip6_addr_is_zero(&netdev->ipv6addr))
		return -AU_ERR_ENETUNREACH;

	payloadLen = (uint16_t)payload;
	totalLen = sizeof(IPv6Header) + payloadLen;
	pkt = (IPv6Header*)kmalloc(totalLen);
	if (!pkt)
		return -AU_ERR_EMFILE;
	memset(pkt, 0, totalLen);

	IPv6SetVerTcFl(pkt, 6, 0, name->sin6_flowinfo & 0xFFFFF);
	pkt->payloadLen = htons(payloadLen);
	pkt->nextHeader = IPV6_NEXT_ICMPV6;
	pkt->hopLimit = 64;
	ip6_addr_copy(&pkt->srcIP, &netdev->ipv6addr);
	memcpy(pkt->destIP.s6_addr, name->sin6_addr.s6_addr, 16);
	ICMPv6IovCopyIn(msg, pkt->payload, payload);

	/* Recompute ICMPv6 checksum if caller left it zero */
	{
		ICMPv6Header* icmp = (ICMPv6Header*)pkt->payload;
		if (icmp->checksum == 0) {
			icmp->checksum = htons(IPv6PseudoChecksum(&pkt->srcIP, &pkt->destIP,
				payloadLen, IPV6_NEXT_ICMPV6, icmp, payloadLen));
		}
	}

	IPV6SendPacket(pkt, nic);
	AuNetRxPoll();
	kfree(pkt);
	return (int)payloadLen;
}

static int AuICMPv6Poll(AuSocket* sock, int events) {
	int mask = POLLOUT;

	(void)events;
	if (!sock)
		return POLLNVAL;
	if (sock->rxstack && sock->rxstack->itemCount)
		mask |= POLLIN;
	if (sock->so_error)
		mask |= POLLERR;
	return mask;
}

static void AuICMPv6Close(AuSocket* sock) {
	(void)sock;
}

static int AuICMPv6Bind(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	(void)sock;
	(void)addr;
	(void)addrlen;
	return 0;
}

static int AuICMPv6FileClose(AuVFSNode* fsys, AuVFSNode* file) {
	AuSocket* sock = (AuSocket*)file->device;

	(void)fsys;
	if (sock) {
		if (sock->rxstack) {
			while (sock->rxstack->itemCount) {
				void* data = AuStackPop(sock->rxstack);
				kfree(data);
			}
			kfree(sock->rxstack);
		}
		kfree(sock);
	}
	kfree(file);
	current_icmpv6_sock = NULL;
	return 0;
}

int CreateICMPv6Socket() {
	AuSocket* sock;
	int fd;

	if (current_icmpv6_sock)
		return -AU_ERR_EADDRINUSE;

	sock = AuNetCreateSocket();
	if (!sock)
		return -AU_ERR_EMFILE;
	sock->bind = AuICMPv6Bind;
	sock->close = AuICMPv6Close;
	sock->connect = 0;
	sock->receive = AuICMPv6Receive;
	sock->send = AuICMPv6Send;
	sock->poll = AuICMPv6Poll;
	sock->so_type = SOCK_DGRAM;
	sock->family = AF_INET6;

	fd = AuSocketInstallFd(sock, "icmpv6", AuICMPv6FileClose);
	if (fd < 0) {
		if (sock->rxstack)
			kfree(sock->rxstack);
		kfree(sock);
		return fd;
	}
	current_icmpv6_sock = sock;
	return fd;
}
