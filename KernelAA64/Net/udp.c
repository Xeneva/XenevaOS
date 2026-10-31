/**
* @file udp.c
* 
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
#include <process.h>
#include <Mm/kmalloc.h>
#include <Net/aunet.h>
#include <string.h>
#include <Net/ipv4.h>
#include <Net/ipv6.h>
#include <Net/udp.h>
#include <Net/ethernet.h>
#include <Sync/poll.h>
#include <Drivers/uart.h>

#define UDP_WAIT_MS     30000
#define UDP_MAX_PAYLOAD 65507
#define UDP_PORT_FIRST  49152
#define UDP_PORT_LAST   65535

list_t* udp_socket_list;

static int UDPRegistered(AuSocket* sock) {
	int i;

	if (!udp_socket_list || !sock)
		return 0;
	for (i = 0; i < (int)udp_socket_list->pointer; i++) {
		if (list_get_at(udp_socket_list, i) == sock)
			return 1;
	}
	return 0;
}

static int UDPPortInUse(uint16_t port, AuSocket* self) {
	int i;

	if (!udp_socket_list || !port)
		return 0;
	for (i = 0; i < (int)udp_socket_list->pointer; i++) {
		AuSocket* other = (AuSocket*)list_get_at(udp_socket_list, i);
		if (other && other != self && other->sessionPort == port)
			return 1;
	}
	return 0;
}

/* Ephemeral port allocation. Returns 0 on success. */
static int UDPAllocPort(AuSocket* sock) {
	uint32_t candidate;
	uint32_t i;

	if (!sock)
		return -AU_ERR_EINVAL;
	if (!udp_socket_list)
		udp_socket_list = initialize_list();
	candidate = UDP_PORT_FIRST + (uint32_t)(rand() % (UDP_PORT_LAST - UDP_PORT_FIRST));
	for (i = 0; i <= (UDP_PORT_LAST - UDP_PORT_FIRST); i++) {
		uint16_t port = (uint16_t)(UDP_PORT_FIRST +
			((candidate - UDP_PORT_FIRST + i) % (UDP_PORT_LAST - UDP_PORT_FIRST + 1)));
		if (!UDPPortInUse(port, sock)) {
			sock->sessionPort = port;
			if (!UDPRegistered(sock))
				list_add(udp_socket_list, sock);
			return 0;
		}
	}
	return -AU_ERR_EADDRINUSE;
}

static void UDPUnregister(AuSocket* sock) {
	int i;

	if (!udp_socket_list || !sock)
		return;
	for (i = 0; i < (int)udp_socket_list->pointer; i++) {
		if (list_get_at(udp_socket_list, i) == sock) {
			list_remove(udp_socket_list, i);
			return;
		}
	}
}

static size_t UDPIovTotal(const msghdr* msg) {
	size_t total = 0;
	size_t i;

	if (!msg)
		return 0;
	for (i = 0; i < msg->msg_iovlen; i++)
		total += msg->msg_iov[i].iov_len;
	return total;
}

static size_t UDPIovCopyIn(const msghdr* msg, uint8_t* dst, size_t max) {
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

static size_t UDPIovCopyOut(const msghdr* msg, const uint8_t* src, size_t len) {
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

/**
 * @brief UDPHandlePacket -- queue one incoming IPv4 datagram for its socket
 * @param packet -- pointer to IPv4 packet
 */
void UDPHandlePacket(char* packet) {
	IPv4Header* ipv4 = (IPv4Header*)packet;
	UDPHeader* udp;
	uint16_t dest_port;
	uint16_t totalLen;
	uint32_t ihl;

	if (!packet || !udp_socket_list)
		return;
	udp = (UDPHeader*)&ipv4->payload;
	memcpy(&dest_port, &udp->destPort, 2);
	memcpy(&totalLen, &ipv4->totalLength, 2);
	totalLen = ntohs(totalLen);
	ihl = (uint32_t)(ipv4->versionHeaderLen & 0x0F) * 4;
	if (ihl < 20 || totalLen < ihl + sizeof(UDPHeader))
		return;

	for (int i = 0; i < (int)udp_socket_list->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(udp_socket_list, i);
		if (sock->sessionPort == ntohs(dest_port)) {
			AuSocketAdd(sock, packet, totalLen);
			AuSocketWakeRx(sock);
			break;
		}
	}
}

void UDPHandlePacket6(IPv6Header* ipv6) {
	UDPHeader* udp;
	uint16_t dest_port;
	uint16_t payloadLen;
	size_t totalLen;
	int i;

	if (!ipv6 || !udp_socket_list)
		return;
	udp = (UDPHeader*)&ipv6->payload;
	memcpy(&dest_port, &udp->destPort, 2);
	payloadLen = ntohs(ipv6->payloadLen);
	if (payloadLen < sizeof(UDPHeader))
		return;
	totalLen = sizeof(IPv6Header) + payloadLen;

	for (i = 0; i < (int)udp_socket_list->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(udp_socket_list, i);
		if (sock->sessionPort == ntohs(dest_port)) {
			AuSocketAdd(sock, ipv6, totalLen);
			AuSocketWakeRx(sock);
			break;
		}
	}
}

static void UDPFillPeerName(msghdr* msg, const IPv4Header* ipv4,
	const IPv6Header* ipv6, const UDPHeader* udp) {
	uint8_t raw[sizeof(sockaddr_in6)];
	socklen_t len;

	if (!msg || !msg->msg_name)
		return;
	memset(raw, 0, sizeof(raw));
	if (ipv6) {
		sockaddr_in6* name = (sockaddr_in6*)raw;
		name->sin6_family = AF_INET6;
		name->sin6_port = udp->srcPort;
		memcpy(name->sin6_addr.s6_addr, ipv6->srcIP.s6_addr, 16);
		len = sizeof(sockaddr_in6);
	} else {
		sockaddr_in* name = (sockaddr_in*)raw;
		name->sin_family = AF_INET;
		name->sin_port = udp->srcPort;
		name->sin_addr.s_addr = ipv4->srcAddress;
		len = sizeof(sockaddr_in);
	}
	AuSocketCopyMsgName(msg, raw, len);
}

/**
* @brief AuUDPReceive -- UDP protocol receive interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
int AuUDPReceive(AuSocket* sock, msghdr* msg, int flags) {
	uint64_t deadline;

	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (sock->shut_rd)
		return 0;
	if (!msg || msg->msg_iovlen == 0)
		return 0;

	deadline = AuGetCurrentUS() + (uint64_t)UDP_WAIT_MS * 1000ULL;

	for (;;) {
		char* packet;

		AuNetRxPoll();
		packet = (char*)AuSocketGet(sock);
		if (packet) {
			size_t stored = *(size_t*)packet;
			uint8_t* raw = (uint8_t*)(packet + sizeof(size_t));
			uint8_t version = raw[0] >> 4;
			size_t copied = 0;

			if (version == 6) {
				IPv6Header* ipv6 = (IPv6Header*)raw;
				UDPHeader* udp = (UDPHeader*)&ipv6->payload;
				size_t len = (size_t)ntohs(ipv6->payloadLen);
				if (len >= sizeof(UDPHeader)) {
					len -= sizeof(UDPHeader);
					copied = UDPIovCopyOut(msg, udp->payload, len);
					if (copied < len)
						msg->msg_flags |= MSG_TRUNC;
					UDPFillPeerName(msg, NULL, ipv6, udp);
				}
			} else {
				IPv4Header* ipv4 = (IPv4Header*)raw;
				UDPHeader* udp = (UDPHeader*)&ipv4->payload;
				uint32_t ihl = (uint32_t)(ipv4->versionHeaderLen & 0x0F) * 4;
				uint16_t total = ntohs(ipv4->totalLength);

				if (ihl >= 20 && total > ihl + sizeof(UDPHeader) &&
					total <= stored) {
					size_t len = (size_t)total - ihl - sizeof(UDPHeader);
					copied = UDPIovCopyOut(msg, udp->payload, len);
					if (copied < len)
						msg->msg_flags |= MSG_TRUNC;
					UDPFillPeerName(msg, ipv4, NULL, udp);
				}
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

/* Record the source address chosen for an unbound datagram socket so a
 * later getsockname(2) reports the interface IP and ephemeral port. */
static void UDPSaveLocal(AuSocket* sock, int family, uint32_t ip4,
	const ip6_addr* ip6) {
	sockaddr_in* in;
	sockaddr_in6* in6;

	if (!sock || sock->local_len)
		return;
	memset(sock->local_addr, 0, sizeof(sock->local_addr));
	if (family == AF_INET6) {
		in6 = (sockaddr_in6*)sock->local_addr;
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons(sock->sessionPort);
		if (ip6)
			memcpy(in6->sin6_addr.s6_addr, ip6->s6_addr, 16);
		sock->local_len = sizeof(sockaddr_in6);
	} else {
		in = (sockaddr_in*)sock->local_addr;
		in->sin_family = AF_INET;
		in->sin_port = htons(sock->sessionPort);
		in->sin_addr.s_addr = ip4;
		sock->local_len = sizeof(sockaddr_in);
	}
	sock->family = (uint8_t)family;
}

/**
* @brief AuUDPSend -- UDP protocol send interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
int AuUDPSend(AuSocket* sock, msghdr* msg, int flags) {	sockaddr* addr;
	socklen_t addrlen;
	uint8_t* flat;
	size_t total;
	int rc;

	(void)flags;
	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (sock->shut_wr)
		return -AU_ERR_EPIPE;
	if (!msg)
		return 0;

	if (sock->sessionPort == 0) {
		rc = UDPAllocPort(sock);
		if (rc < 0)
			return rc;
	}

	if (msg->msg_name && msg->msg_namelen >= sizeof(sockaddr)) {
		addr = (sockaddr*)msg->msg_name;
		addrlen = msg->msg_namelen;
	} else if (sock->peer_len >= sizeof(sockaddr)) {
		addr = (sockaddr*)sock->peer_addr;
		addrlen = sock->peer_len;
	} else {
		return -AU_ERR_EDESTADDRREQ;
	}

	if (msg->msg_iovlen == 0)
		return 0;

	total = UDPIovTotal(msg);
	if (total > UDP_MAX_PAYLOAD)
		return -AU_ERR_EMSGSIZE;
	flat = NULL;
	if (total) {
		flat = (uint8_t*)kmalloc(total);
		if (!flat)
			return -AU_ERR_EMFILE;
		UDPIovCopyIn(msg, flat, total);
	}

	if (addr->sa_family == AF_INET6) {
		sockaddr_in6* sockin6 = (sockaddr_in6*)addr;
		AuVFSNode* nic;
		AuNetworkDevice* netdev;
		size_t total_len;
		uint16_t udpLen;
		IPv6Header* ipv6;
		UDPHeader* udp;

		if (addrlen < sizeof(sockaddr_in6)) {
			if (flat)
				kfree(flat);
			return -AU_ERR_EINVAL;
		}
		nic = AuNetworkRoute6((const ip6_addr*)&sockin6->sin6_addr);
		if (!nic) {
			if (flat)
				kfree(flat);
			return -AU_ERR_ENETUNREACH;
		}
		netdev = (AuNetworkDevice*)nic->device;
		if (!netdev) {
			if (flat)
				kfree(flat);
			return -AU_ERR_ENETUNREACH;
		}
		UDPSaveLocal(sock, AF_INET6, 0, &netdev->ipv6addr);

		udpLen = (uint16_t)(sizeof(UDPHeader) + total);
		total_len = sizeof(IPv6Header) + udpLen;
		ipv6 = (IPv6Header*)kmalloc(total_len);
		if (!ipv6) {
			if (flat)
				kfree(flat);
			return -AU_ERR_EMFILE;
		}
		memset(ipv6, 0, total_len);
		IPv6SetVerTcFl(ipv6, 6, 0, sockin6->sin6_flowinfo & 0xFFFFF);
		ipv6->payloadLen = htons(udpLen);
		ipv6->nextHeader = IPV6_NEXT_UDP;
		ipv6->hopLimit = 64;
		ip6_addr_copy(&ipv6->srcIP, &netdev->ipv6addr);
		memcpy(ipv6->destIP.s6_addr, sockin6->sin6_addr.s6_addr, 16);

		udp = (UDPHeader*)&ipv6->payload;
		udp->srcPort = htons(sock->sessionPort);
		udp->destPort = sockin6->sin6_port;
		udp->length = htons(udpLen);
		udp->checksum = 0;
		if (total)
			memcpy(udp->payload, flat, total);
		udp->checksum = htons(IPv6PseudoChecksum(&ipv6->srcIP, &ipv6->destIP,
			udpLen, IPV6_NEXT_UDP, udp, udpLen));
		if (udp->checksum == 0)
			udp->checksum = 0xFFFF;

		IPV6SendPacket(ipv6, nic);
		kfree(ipv6);
		if (flat)
			kfree(flat);
		return (int)total;
	}

	if (addr->sa_family != AF_INET || addrlen < sizeof(sockaddr_in)) {
		if (flat)
			kfree(flat);
		return addr->sa_family == AF_INET ? -AU_ERR_EINVAL : -AU_ERR_EAFNOSUPPORT;
	}

	{
		sockaddr_in* sockin = (sockaddr_in*)addr;
		AuVFSNode* nic = AuNetworkRoute(sockin->sin_addr.s_addr);
		AuNetworkDevice* netdev;
		size_t total_len;
		IPv4Header* ipv4;
		UDPHeader* udp;

		if (!nic) {
			if (flat)
				kfree(flat);
			return -AU_ERR_ENETUNREACH;
		}
		netdev = (AuNetworkDevice*)nic->device;
		if (!netdev) {
			if (flat)
				kfree(flat);
			return -AU_ERR_ENETUNREACH;
		}
		UDPSaveLocal(sock, AF_INET, netdev->ipv4addr, NULL);

		total_len = sizeof(IPv4Header) + total + sizeof(UDPHeader);
		ipv4 = (IPv4Header*)kmalloc(total_len);
		if (!ipv4) {
			if (flat)
				kfree(flat);
			return -AU_ERR_EMFILE;
		}
		memset(ipv4, 0, total_len);
		ipv4->totalLength = htons((uint16_t)total_len);
		ipv4->destAddress = sockin->sin_addr.s_addr;
		ipv4->srcAddress = netdev->ipv4addr;
		ipv4->timeToLive = 64;
		ipv4->protocol = IPV4_PROTOCOL_UDP;
		ipv4->identification = 0;
		ipv4->flagsFragOffset = htons(0x4000);
		ipv4->versionHeaderLen = 0x45;
		ipv4->typeOfService = 0;
		ipv4->headerChecksum = 0;
		ipv4->headerChecksum = htons(IPv4CalculateChecksum(ipv4));

		udp = (UDPHeader*)&ipv4->payload;
		udp->srcPort = htons(sock->sessionPort);
		udp->destPort = sockin->sin_port;
		udp->length = htons((uint16_t)(sizeof(UDPHeader) + total));
		udp->checksum = 0;
		if (total)
			memcpy(udp->payload, flat, total);

		IPV4SendPacket(ipv4, nic);
		kfree(ipv4);
		if (flat)
			kfree(flat);
		return (int)total;
	}
}

int AuUDPConnect(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	sockaddr* stored = (sockaddr*)sock->peer_addr;
	int rc;

	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (!addr || addrlen < sizeof(sockaddr))
		return -AU_ERR_EINVAL;
	if (addr->sa_family == AF_INET) {
		if (addrlen < sizeof(sockaddr_in))
			return -AU_ERR_EINVAL;
	} else if (addr->sa_family == AF_INET6) {
		if (addrlen < sizeof(sockaddr_in6))
			return -AU_ERR_EINVAL;
	} else {
		return -AU_ERR_EAFNOSUPPORT;
	}
	if (sock->sessionPort == 0) {
		rc = UDPAllocPort(sock);
		if (rc < 0)
			return rc;
	}
	if (addrlen > sizeof(sock->peer_addr))
		addrlen = sizeof(sock->peer_addr);
	memset(sock->peer_addr, 0, sizeof(sock->peer_addr));
	memcpy(stored, addr, addrlen);
	sock->peer_len = addr->sa_family == AF_INET ? sizeof(sockaddr_in) :
		sizeof(sockaddr_in6);
	if (!sock->local_len) {
		if (addr->sa_family == AF_INET) {
			sockaddr_in* in = (sockaddr_in*)addr;
			AuVFSNode* nic = AuNetworkRoute(in->sin_addr.s_addr);
			AuNetworkDevice* ndev = nic ? (AuNetworkDevice*)nic->device : NULL;
			if (ndev)
				UDPSaveLocal(sock, AF_INET, ndev->ipv4addr, NULL);
		} else if (addr->sa_family == AF_INET6) {
			sockaddr_in6* in6 = (sockaddr_in6*)addr;
			AuVFSNode* nic = AuNetworkRoute6(
				(const ip6_addr*)&in6->sin6_addr);
			AuNetworkDevice* ndev = nic ? (AuNetworkDevice*)nic->device : NULL;
			if (ndev)
				UDPSaveLocal(sock, AF_INET6, 0, &ndev->ipv6addr);
		}
	}
	sock->sockState = SOCK_STATE_CONNECTED;
	return 0;
}

/*
 * AuUDPShutdown -- shutdown(2) for connected datagrams.
 *
 * UDP has no wire-level FIN, so the flags are local: SHUT_WR makes send
 * fail with EPIPE and SHUT_RD makes receive return EOF. An unconnected
 * datagram socket has no peer to half-close and reports ENOTCONN.
 */
static int AuUDPShutdown(AuSocket* sock, int how) {
	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
		return -AU_ERR_EINVAL;
	if (sock->peer_len < sizeof(sockaddr))
		return -AU_ERR_ENOTCONN;
	if (how == SHUT_RD || how == SHUT_RDWR) {
		sock->shut_rd = 1;
		AuSocketWakeRx(sock);
	}
	if (how == SHUT_WR || how == SHUT_RDWR)
		sock->shut_wr = 1;
	return 0;
}

/**
* @brief AuUDPClose -- UDP protocol close call
* @param sock -- Pointer to socket
*/
void AuUDPClose(AuSocket* sock) {
	(void)sock;
}

/**
 * @brief AuUDPBind -- bind a local address to the current
 * socket, it also verifies if there is already a socket
 * with this local address, if not found, then assign this
 * socket with the given local address and assign it to NIC
 * for listening to incoming communication
 */
int AuUDPBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	int port = 0;
	uint8_t family;
	int i;
	int rc;

	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (!addr || addrlen < sizeof(sockaddr))
		return -AU_ERR_EINVAL;
	if (sock->sessionPort != 0)
		return -AU_ERR_EINVAL;

	if (addr->sa_family == AF_INET6) {
		sockaddr_in6* addr6 = (sockaddr_in6*)addr;
		if (addrlen < sizeof(sockaddr_in6))
			return -AU_ERR_EINVAL;
		family = AF_INET6;
		port = ntohs(addr6->sin6_port);
	} else if (addr->sa_family == AF_INET) {
		sockaddr_in* addr_in = (sockaddr_in*)addr;
		if (addrlen < sizeof(sockaddr_in))
			return -AU_ERR_EINVAL;
		family = AF_INET;
		port = ntohs(addr_in->sin_port);
	} else {
		return -AU_ERR_EAFNOSUPPORT;
	}

	if (port == 0) {
		rc = UDPAllocPort(sock);
		if (rc < 0)
			return rc;
	} else {
		if (!udp_socket_list)
			udp_socket_list = initialize_list();
		for (i = 0; i < (int)udp_socket_list->pointer; i++) {
			AuSocket* existing = (AuSocket*)list_get_at(udp_socket_list, i);
			if (existing != sock && existing->sessionPort == (uint16_t)port)
				return -AU_ERR_EADDRINUSE;
		}
		sock->sessionPort = (uint16_t)port;
	}
	sock->family = family;
	/* Store the bound address for getsockname(2), with the kernel-assigned
	 * port when the caller asked for 0. */
	memset(sock->local_addr, 0, sizeof(sock->local_addr));
	{
		socklen_t want = family == AF_INET ? sizeof(sockaddr_in) :
			sizeof(sockaddr_in6);
		socklen_t copy = addrlen > want ? want : addrlen;

		memcpy(sock->local_addr, addr, copy);
		sock->local_len = want;
		if (family == AF_INET)
			((sockaddr_in*)sock->local_addr)->sin_port =
				htons(sock->sessionPort);
		else
			((sockaddr_in6*)sock->local_addr)->sin6_port =
				htons(sock->sessionPort);
	}
	if (!UDPRegistered(sock))
		list_add(udp_socket_list, sock);
	return 0;
}

int AuUDPPoll(AuSocket* sock, int events) {
	int mask = POLLOUT;

	(void)events;
	if (!sock)
		return POLLNVAL;
	if (sock->shut_rd)
		mask |= POLLIN;
	if (sock->rxstack && sock->rxstack->itemCount)
		mask |= POLLIN;
	if (sock->so_error)
		mask |= POLLERR;
	return mask;
}

uint64_t AuUDPRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	(void)node;
	(void)file;
	(void)buffer;
	(void)len;
	return 0;
}

uint64_t AuUDPWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	(void)node;
	(void)file;
	(void)buffer;
	(void)len;
	return 0;
}

int AuUDPFileClose(AuVFSNode* fsys, AuVFSNode* file) {
	AuSocket* sock;

	(void)fsys;
	if (!file)
		return 0;
	sock = (AuSocket*)file->device;
	if (sock) {
		UDPUnregister(sock);
		if (sock->rxstack) {
			while (sock->rxstack->itemCount) {
				void* data = AuStackPop(sock->rxstack);
				if (data)
					kfree(data);
			}
			kfree(sock->rxstack);
			sock->rxstack = NULL;
		}
		kfree(sock);
	}
	kfree(file);
	return 0;
}

static int AuUDPGetSockName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	uint8_t raw[sizeof(sockaddr_in6)];
	socklen_t len;

	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (sock->local_len >= sizeof(sockaddr)) {
		/* bind(2) recorded the family, address and assigned port. */
		return AuSocketCopyAddr(addr, addrlen, sock->local_addr,
			sock->local_len);
	}
	memset(raw, 0, sizeof(raw));
	if (sock->family == AF_INET6) {
		sockaddr_in6* in6 = (sockaddr_in6*)raw;
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons(sock->sessionPort);
		len = sizeof(sockaddr_in6);
	} else {
		sockaddr_in* in = (sockaddr_in*)raw;
		in->sin_family = AF_INET;
		in->sin_port = htons(sock->sessionPort);
		len = sizeof(sockaddr_in);
	}
	return AuSocketCopyAddr(addr, addrlen, raw, len);
}

static int AuUDPGetPeerName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (sock->peer_len < sizeof(sockaddr))
		return -AU_ERR_ENOTCONN;
	return AuSocketCopyAddr(addr, addrlen, sock->peer_addr, sock->peer_len);
}

/*
 * CreateUDPSocket -- create a new UDP
 * socket
 */
int CreateUDPSocket() {
	AuSocket* sock;
	int fd;

	sock = AuNetCreateSocket();
	if (!sock)
		return -AU_ERR_EMFILE;
	sock->bind = AuUDPBind;
	sock->close = AuUDPClose;
	sock->connect = AuUDPConnect;
	sock->receive = AuUDPReceive;
	sock->send = AuUDPSend;
	sock->shutdown = AuUDPShutdown;
	sock->getsockname = AuUDPGetSockName;
	sock->getpeername = AuUDPGetPeerName;
	sock->poll = AuUDPPoll;
	sock->so_type = SOCK_DGRAM;
	sock->family = AF_INET;

	fd = AuSocketInstallFd(sock, "udp", AuUDPFileClose);
	if (fd < 0) {
		if (sock->rxstack)
			kfree(sock->rxstack);
		kfree(sock);
		return fd;
	}
	return fd;
}

/**
 * @brief UDPProtocolInstall -- initialize the UDP socket
 */
void UDPProtocolInstall() {
	udp_socket_list = initialize_list();
}

/**
 * @brief UDPProtocolGetSockList -- returns the socket
 * list
 */
list_t* UDPProtocolGetSockList() {
	return udp_socket_list;
}
