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
#include <Net/aunet.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <process.h>
#include <stack.h>
#include <Net/tcp.h>
#include <Net/ipv4.h>
#include <Net/ipv6.h>
#include <Net/arp.h>
#include <Net/route.h>
#include <Net/aunet.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/sched.h>
#include <Drivers/uart.h>
#include <circbuf.h>
#include <_null.h>

extern int rand();

list_t* tcpSocketList;
static int _tcp_port_ = 49152;

#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(push, 1)
#endif
typedef struct _tcpcheckheader_ {
	uint32_t source;
	uint32_t destination;
	uint8_t zeros;
	uint8_t protocol;
	uint16_t tcpLen;
	uint8_t tcpHeader[];
} TCPCheckHeader;
#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(pop)
#endif

static uint16_t CalculateTCPChecksum(TCPCheckHeader* p, TCPHeader* h, void* d, size_t payloadsz) {
	uint32_t sum = 0;
	uint16_t* s = (uint16_t*)p;
	int i;

	for (i = 0; i < 6; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
	}

	s = (uint16_t*)h;
	for (i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
	}

	if (d && payloadsz) {
		uint16_t dwords = payloadsz / 2;
		s = (uint16_t*)d;
		for (unsigned int n = 0; n < dwords; ++n) {
			sum += ntohs(s[n]);
			if (sum > 0xFFFF)
				sum = (sum >> 16) + (sum & 0xFFFF);
		}

		if (dwords * 2 != payloadsz) {
			uint8_t* t = (uint8_t*)d;
			uint8_t tmp[2];
			tmp[0] = t[dwords * sizeof(uint16_t)];
			tmp[1] = 0;
			sum += ntohs(*(uint16_t*)tmp);
			if (sum > 0xFFFF)
				sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	return ~(sum & 0xFFFF) & 0xFFFF;
}

static TCPControlBlock* TCPGetPCB(AuSocket* sock) {
	if (!sock)
		return NULL;
	return (TCPControlBlock*)sock->proto;
}

static uint16_t TCPFlagsOf(TCPHeader* tcp) {
	return ntohs(tcp->dataOffsetFlags) & 0x01FF;
}

static int TCPHdrBytes(TCPHeader* tcp) {
	return ((ntohs(tcp->dataOffsetFlags) >> 12) & 0xF) * 4;
}

static uint16_t TCPWindowOf(AuSocket* sock) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	size_t used;

	if (!pcb || !pcb->rxbuf)
		return TCP_DEFAULT_WIN_SZ;
	used = AuCircBufSize((CircBuffer*)pcb->rxbuf);
	if (used >= TCP_RX_BUF_SZ)
		return 0;
	/* TCP window is a 16-bit field (RFC 9293). A 64 KiB buffer must not
	 * advertise 65536, which truncates to zero. */
	if ((TCP_RX_BUF_SZ - used) > 65535)
		return 65535;
	return (uint16_t)(TCP_RX_BUF_SZ - used);
}

static int TCPPortInUse(uint16_t port) {
	int i;

	if (!tcpSocketList)
		return 0;
	for (i = 0; i < (int)tcpSocketList->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(tcpSocketList, i);
		if (sock && sock->sessionPort == port)
			return 1;
	}
	return 0;
}

static void TCPRegisterSocket(AuSocket* sock) {
	int i;

	if (!tcpSocketList || !sock)
		return;
	for (i = 0; i < (int)tcpSocketList->pointer; i++) {
		if (list_get_at(tcpSocketList, i) == sock)
			return;
	}
	list_add(tcpSocketList, sock);
}

static void TCPUnregisterSocket(AuSocket* sock) {
	int i;

	if (!tcpSocketList || !sock)
		return;
	for (i = 0; i < (int)tcpSocketList->pointer; i++) {
		if (list_get_at(tcpSocketList, i) == sock) {
			list_remove(tcpSocketList, i);
			return;
		}
	}
}

static void AuTCPObtainPort(AuSocket* sock) {
	int tries = 0;

	while (tries < 16384) {
		int port = _tcp_port_++;
		if (_tcp_port_ > 65535)
			_tcp_port_ = 49152;
		if (!TCPPortInUse((uint16_t)port)) {
			sock->sessionPort = (uint16_t)port;
			TCPRegisterSocket(sock);
			return;
		}
		tries++;
	}
	sock->sessionPort = (uint16_t)_tcp_port_++;
	TCPRegisterSocket(sock);
}

/* Ethernet sends are dropped when the neighbor entry is missing, and
 * IPV4SendPacket does not say so. Advance snd_nxt only after this
 * returns true, or the first ClientHello is consumed and never
 * retransmitted and the TLS handshake fails once. */
static int TCPLinkReady(TCPControlBlock* pcb) {
	AuNetworkDevice* ndev;
	AuRouteResult rr;
	uint32_t hop;

	if (!pcb || !pcb->nic)
		return 0;
	ndev = (AuNetworkDevice*)pcb->nic->device;
	if (!ndev)
		return 0;
	if (pcb->is_ipv6)
		return 1;
	if (ndev->type == NETDEV_TYPE_LOOPBACK)
		return 1;
	if (ndev->type != NETDEV_TYPE_ETHERNET && ndev->type != NETDEV_TYPE_802_11)
		return 1;
	hop = pcb->remote_ip;
	if (AuRouteLookup4(hop, &rr) == 0 && (rr.flags & RTF_GATEWAY) && rr.nexthop)
		hop = rr.nexthop;
	return AuARPResolve(pcb->nic, hop) != NULL;
}

static int TCPSendSegment(AuSocket* sock, uint16_t flags, const void* payload, size_t payloadLen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	AuNetworkDevice* ndev;
	TCPHeader* tcp;
	size_t totalLen;
	uint32_t seq;

	if (!pcb || !pcb->nic)
		return -1;
	ndev = (AuNetworkDevice*)pcb->nic->device;
	if (!ndev)
		return -1;
	if (!TCPLinkReady(pcb))
		return -1;

	seq = pcb->snd_nxt;

	if (pcb->is_ipv6) {
		IPv6Header* ipv6;
		uint16_t tcpLen = (uint16_t)(sizeof(TCPHeader) + payloadLen);

		totalLen = sizeof(IPv6Header) + tcpLen;
		ipv6 = (IPv6Header*)kmalloc(totalLen);
		if (!ipv6)
			return -1;
		memset(ipv6, 0, totalLen);
		IPv6SetVerTcFl(ipv6, 6, 0, 0);
		ipv6->payloadLen = htons(tcpLen);
		ipv6->nextHeader = IPV6_NEXT_TCP;
		ipv6->hopLimit = 64;
		ip6_addr_copy(&ipv6->srcIP, &ndev->ipv6addr);
		ip6_addr_copy(&ipv6->destIP, &pcb->remote_ip6);

		tcp = (TCPHeader*)&ipv6->payload;
		tcp->srcPort = htons(sock->sessionPort);
		tcp->destPort = htons(pcb->remote_port);
		tcp->sequenceNum = htonl(seq);
		tcp->ackNum = htonl(pcb->rcv_nxt);
		tcp->dataOffsetFlags = htons(flags | 0x5000);
		tcp->window = htons(TCPWindowOf(sock));
		tcp->checksum = 0;
		tcp->urgentPointer = 0;
		if (payload && payloadLen)
			memcpy(&ipv6->payload[sizeof(TCPHeader)], (void*)payload, payloadLen);

		tcp->checksum = htons(IPv6PseudoChecksum(&ipv6->srcIP, &ipv6->destIP,
			tcpLen, IPV6_NEXT_TCP, tcp, tcpLen));
		IPV6SendPacket(ipv6, pcb->nic);
		kfree(ipv6);
	} else {
		IPv4Header* ipv4;
		TCPCheckHeader checkhdr;

		totalLen = sizeof(IPv4Header) + sizeof(TCPHeader) + payloadLen;
		ipv4 = (IPv4Header*)kmalloc(totalLen);
		if (!ipv4)
			return -1;
		memset(ipv4, 0, totalLen);

		ipv4->versionHeaderLen = 0x45;
		ipv4->typeOfService = 0;
		ipv4->totalLength = htons((uint16_t)totalLen);
		sock->ipv4Iden++;
		ipv4->identification = htons(sock->ipv4Iden);
		ipv4->flagsFragOffset = htons(0x4000);
		ipv4->timeToLive = 64;
		ipv4->protocol = IPV4_PROTOCOL_TCP;
		ipv4->srcAddress = ndev->ipv4addr;
		ipv4->destAddress = pcb->remote_ip;
		ipv4->headerChecksum = 0;
		ipv4->headerChecksum = htons(IPv4CalculateChecksum(ipv4));

		tcp = (TCPHeader*)&ipv4->payload;
		tcp->srcPort = htons(sock->sessionPort);
		tcp->destPort = htons(pcb->remote_port);
		tcp->sequenceNum = htonl(seq);
		tcp->ackNum = htonl(pcb->rcv_nxt);
		tcp->dataOffsetFlags = htons(flags | 0x5000);
		tcp->window = htons(TCPWindowOf(sock));
		tcp->checksum = 0;
		tcp->urgentPointer = 0;
		if (payload && payloadLen)
			memcpy(&ipv4->payload[sizeof(TCPHeader)], (void*)payload, payloadLen);

		checkhdr.source = ipv4->srcAddress;
		checkhdr.destination = ipv4->destAddress;
		checkhdr.zeros = 0;
		checkhdr.protocol = IPV4_PROTOCOL_TCP;
		checkhdr.tcpLen = htons((uint16_t)(sizeof(TCPHeader) + payloadLen));
		tcp->checksum = htons(CalculateTCPChecksum(&checkhdr, tcp,
			payloadLen ? &ipv4->payload[sizeof(TCPHeader)] : NULL, payloadLen));

		IPV4SendPacket(ipv4, pcb->nic);
		kfree(ipv4);
	}

	if (flags & TCP_FLAGS_SYN)
		pcb->snd_nxt = seq + 1;
	else {
		pcb->snd_nxt = seq + (uint32_t)payloadLen;
		if (flags & TCP_FLAGS_FIN)
			pcb->snd_nxt += 1;
	}
	return 0;
}

static void TCPSendReset(AuVFSNode* nic, IPv4Header* ip, TCPHeader* tcp, size_t segLen) {
	AuNetworkDevice* ndev;
	IPv4Header* ipv4;
	TCPHeader* rst;
	TCPCheckHeader checkhdr;
	uint16_t flags;
	uint32_t seq;
	uint32_t ack;
	size_t totalLen;

	if (!nic)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (!ndev)
		return;

	flags = TCPFlagsOf(tcp);
	if (flags & TCP_FLAGS_RST)
		return;

	totalLen = sizeof(IPv4Header) + sizeof(TCPHeader);
	ipv4 = (IPv4Header*)kmalloc(totalLen);
	if (!ipv4)
		return;
	memset(ipv4, 0, totalLen);

	ipv4->versionHeaderLen = 0x45;
	ipv4->totalLength = htons((uint16_t)totalLen);
	ipv4->identification = 0;
	ipv4->flagsFragOffset = htons(0x4000);
	ipv4->timeToLive = 64;
	ipv4->protocol = IPV4_PROTOCOL_TCP;
	ipv4->srcAddress = ndev->ipv4addr;
	ipv4->destAddress = ip->srcAddress;
	ipv4->headerChecksum = 0;
	ipv4->headerChecksum = htons(IPv4CalculateChecksum(ipv4));

	if (flags & TCP_FLAGS_ACK) {
		seq = ntohl(tcp->ackNum);
		ack = 0;
		flags = TCP_FLAGS_RST;
	} else {
		seq = 0;
		ack = ntohl(tcp->sequenceNum) + (uint32_t)segLen;
		flags = TCP_FLAGS_RST | TCP_FLAGS_ACK;
	}

	rst = (TCPHeader*)&ipv4->payload;
	rst->srcPort = tcp->destPort;
	rst->destPort = tcp->srcPort;
	rst->sequenceNum = htonl(seq);
	rst->ackNum = htonl(ack);
	rst->dataOffsetFlags = htons(flags | 0x5000);
	rst->window = 0;
	rst->checksum = 0;
	rst->urgentPointer = 0;

	checkhdr.source = ipv4->srcAddress;
	checkhdr.destination = ipv4->destAddress;
	checkhdr.zeros = 0;
	checkhdr.protocol = IPV4_PROTOCOL_TCP;
	checkhdr.tcpLen = htons(sizeof(TCPHeader));
	rst->checksum = htons(CalculateTCPChecksum(&checkhdr, rst, NULL, 0));
	IPV4SendPacket(ipv4, nic);
	kfree(ipv4);
}

static void TCPFreePCB(TCPControlBlock* pcb) {
	if (!pcb)
		return;
	if (pcb->rxbuf) {
		AuCircBufFree((CircBuffer*)pcb->rxbuf);
		pcb->rxbuf = NULL;
	}
	if (pcb->rxmem) {
		kfree(pcb->rxmem);
		pcb->rxmem = NULL;
	}
	if (pcb->acceptq) {
		while (pcb->acceptq->pointer)
			list_remove(pcb->acceptq, 0);
		kfree(pcb->acceptq);
		pcb->acceptq = NULL;
	}
	kfree(pcb);
}

static TCPControlBlock* TCPAllocPCB(void) {
	TCPControlBlock* pcb = (TCPControlBlock*)kmalloc(sizeof(TCPControlBlock));
	if (!pcb)
		return NULL;
	memset(pcb, 0, sizeof(TCPControlBlock));
	pcb->state = TCP_STATE_CLOSED;
	pcb->mss = TCP_MSS;
	pcb->snd_wnd = TCP_DEFAULT_WIN_SZ;
	pcb->rcv_wnd = 65535;
	pcb->rxmem = (uint8_t*)kmalloc(TCP_RX_BUF_SZ);
	if (!pcb->rxmem) {
		kfree(pcb);
		return NULL;
	}
	memset(pcb->rxmem, 0, TCP_RX_BUF_SZ);
	pcb->rxbuf = AuCircBufInitialise(pcb->rxmem, TCP_RX_BUF_SZ);
	if (!pcb->rxbuf) {
		kfree(pcb->rxmem);
		kfree(pcb);
		return NULL;
	}
	return pcb;
}

static AuSocket* TCPAllocSocket(void);
int AuTCPBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen);
int AuTCPListen(AuSocket* sock, int backlog);
int AuTCPAccept(AuSocket* sock, sockaddr* addr, socklen_t* addrlen);
int AuTCPReceive(AuSocket* sock, msghdr* msg, int flags);
int AuTCPSend(AuSocket* sock, msghdr* msg, int flags);
void AuTCPClose(AuSocket* sock);
int AuTCPConnect(AuSocket* sock, sockaddr* addr, socklen_t addrlen);
int AuTCPFileClose(AuVFSNode* fsys, AuVFSNode* file);

static AuSocket* TCPAllocSocket(void) {
	AuSocket* sock = AuNetCreateSocket();
	TCPControlBlock* pcb;

	if (!sock)
		return NULL;
	pcb = TCPAllocPCB();
	if (!pcb) {
		if (sock->rxstack)
			kfree(sock->rxstack);
		kfree(sock);
		return NULL;
	}
	sock->send = AuTCPSend;
	sock->receive = AuTCPReceive;
	sock->connect = AuTCPConnect;
	sock->bind = AuTCPBind;
	sock->close = AuTCPClose;
	sock->listen = AuTCPListen;
	sock->accept = AuTCPAccept;
	sock->proto = pcb;
	sock->sockState = SOCK_STATE_CLOSED;
	return sock;
}

static AuSocket* TCPFindSocket(uint32_t srcIP, uint16_t srcPort, uint16_t destPort) {
	AuSocket* listener = NULL;
	int i;

	if (!tcpSocketList)
		return NULL;
	for (i = 0; i < (int)tcpSocketList->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(tcpSocketList, i);
		TCPControlBlock* pcb;

		if (!sock || sock->sessionPort != destPort)
			continue;
		pcb = TCPGetPCB(sock);
		if (!pcb || pcb->is_ipv6)
			continue;
		if (pcb->state == TCP_STATE_LISTEN) {
			listener = sock;
			continue;
		}
		if (pcb->remote_ip == srcIP && pcb->remote_port == srcPort)
			return sock;
	}
	return listener;
}

static AuSocket* TCPFindSocket6(const ip6_addr* srcIP, uint16_t srcPort, uint16_t destPort) {
	AuSocket* listener = NULL;
	int i;

	if (!tcpSocketList || !srcIP)
		return NULL;
	for (i = 0; i < (int)tcpSocketList->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(tcpSocketList, i);
		TCPControlBlock* pcb;

		if (!sock || sock->sessionPort != destPort)
			continue;
		pcb = TCPGetPCB(sock);
		if (!pcb)
			continue;
		if (pcb->state == TCP_STATE_LISTEN) {
			/* Prefer IPv6 listeners; also accept dual use of listen sockets */
			listener = sock;
			continue;
		}
		if (pcb->is_ipv6 && ip6_addr_equal(&pcb->remote_ip6, srcIP) &&
			pcb->remote_port == srcPort)
			return sock;
	}
	return listener;
}

static void TCPQueueAccept(AuSocket* listener, AuSocket* child) {
	TCPControlBlock* pcb = TCPGetPCB(listener);

	if (!pcb)
		return;
	if (!pcb->acceptq)
		pcb->acceptq = initialize_list();
	if (pcb->acceptq)
		list_add(pcb->acceptq, child);
}

static int TCPWriteRx(TCPControlBlock* pcb, const uint8_t* data, size_t len) {
	CircBuffer* buf;

	if (!pcb || !pcb->rxbuf || !data || !len)
		return 0;
	buf = (CircBuffer*)pcb->rxbuf;
	return (int)AuCircBufWrite(buf, data, len);
}

static int TCPSendAck(AuSocket* sock) {
	return TCPSendSegment(sock, TCP_FLAGS_ACK, NULL, 0);
}

int AuTCPAcknowledge(AuVFSNode* nic, AuSocket* sock, IPv4Header* ippack, size_t payloadLen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	TCPHeader* tcp;

	if (!pcb || !ippack)
		return -1;
	if (!pcb->nic)
		pcb->nic = nic;
	tcp = (TCPHeader*)&ippack->payload;
	pcb->rcv_nxt = ntohl(tcp->sequenceNum) + (uint32_t)payloadLen;
	return TCPSendAck(sock);
}

int AuTCPReceive(AuSocket* sock, msghdr* msg, int flags) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	CircBuffer* buf;
	size_t want;
	size_t got = 0;
	uint8_t* dest;

	(void)flags;
	if (!pcb)
		return -1;
	if (!msg || msg->msg_iovlen == 0)
		return 0;
	if (msg->msg_iovlen > 1)
		return -1;

	AuNetRxPoll();
	buf = (CircBuffer*)pcb->rxbuf;
	want = msg->msg_iov[0].iov_len;
	dest = (uint8_t*)msg->msg_iov[0].iov_base;
	if (!buf || CircBufEmpty(buf)) {
		if (pcb->fin_recvd || pcb->state == TCP_STATE_CLOSE_WAIT ||
			pcb->state == TCP_STATE_CLOSED)
			return 0;
		return -1;
	}

	{
		uint16_t before = TCPWindowOf(sock);
		got = AuCircBufRead(buf, dest, want);
		/* Filling the 64KB buffer advertises window 0. Nothing in the read
		 * path sends a segment, so the peer waits on its persist timer
		 * before the rest of a large body is sent. */
		if (got > 0 &&
			(pcb->state == TCP_STATE_ESTABLISHED || pcb->state == TCP_STATE_CLOSE_WAIT)) {
			uint16_t after = TCPWindowOf(sock);
			if (before < (uint16_t)(TCP_MSS * 2) && after > before)
				TCPSendAck(sock);
		}
	}

	if (msg->msg_name) {
		if (pcb->is_ipv6 && msg->msg_namelen >= sizeof(sockaddr_in6)) {
			sockaddr_in6* in6 = (sockaddr_in6*)msg->msg_name;
			in6->sin6_family = AF_INET6;
			in6->sin6_port = htons(pcb->remote_port);
			in6->sin6_flowinfo = 0;
			memcpy(in6->sin6_addr.s6_addr, pcb->remote_ip6.s6_addr, 16);
			in6->sin6_scope_id = 0;
			msg->msg_namelen = sizeof(sockaddr_in6);
		} else if (msg->msg_namelen >= sizeof(sockaddr_in)) {
			sockaddr_in* in = (sockaddr_in*)msg->msg_name;
			in->sin_family = AF_INET;
			in->sin_port = htons(pcb->remote_port);
			in->sin_addr.s_addr = pcb->remote_ip;
			msg->msg_namelen = sizeof(sockaddr_in);
		}
	}
	return (int)got;
}

int AuTCPSend(AuSocket* sock, msghdr* msg, int flags) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	const uint8_t* src;
	size_t remaining;
	size_t sent = 0;

	(void)flags;
	if (!pcb)
		return -1;
	if (pcb->state != TCP_STATE_ESTABLISHED && pcb->state != TCP_STATE_CLOSE_WAIT)
		return -1;
	if (!msg || msg->msg_iovlen == 0)
		return 0;
	if (msg->msg_iovlen > 1)
		return -1;

	src = (const uint8_t*)msg->msg_iov[0].iov_base;
	remaining = msg->msg_iov[0].iov_len;
	while (remaining) {
		size_t chunk = remaining;
		uint16_t sendFlags = TCP_FLAGS_ACK | TCP_FLAGS_PSH;
		if (chunk > pcb->mss)
			chunk = pcb->mss;
		if (TCPSendSegment(sock, sendFlags, src + sent, chunk) != 0)
			break;
		sent += chunk;
		remaining -= chunk;
	}
	AuNetRxPoll();
	return (int)sent;
}

void AuTCPClose(AuSocket* sock) {
	TCPControlBlock* pcb = TCPGetPCB(sock);

	if (!pcb)
		return;
	if (pcb->state == TCP_STATE_ESTABLISHED || pcb->state == TCP_STATE_SYN_RECEIVED) {
		TCPSendSegment(sock, TCP_FLAGS_FIN | TCP_FLAGS_ACK, NULL, 0);
		pcb->state = TCP_STATE_FIN_WAIT_1;
	} else if (pcb->state == TCP_STATE_CLOSE_WAIT) {
		TCPSendSegment(sock, TCP_FLAGS_FIN | TCP_FLAGS_ACK, NULL, 0);
		pcb->state = TCP_STATE_LAST_ACK;
	} else if (pcb->state == TCP_STATE_LISTEN) {
		int i;
		pcb->state = TCP_STATE_CLOSED;
		if (tcpSocketList) {
			for (i = (int)tcpSocketList->pointer - 1; i >= 0; i--) {
				AuSocket* child = (AuSocket*)list_get_at(tcpSocketList, i);
				TCPControlBlock* childpcb = TCPGetPCB(child);
				if (childpcb && childpcb->parent == sock) {
					TCPSendSegment(child, TCP_FLAGS_RST | TCP_FLAGS_ACK, NULL, 0);
					childpcb->state = TCP_STATE_CLOSED;
				}
			}
		}
	} else if (pcb->state != TCP_STATE_CLOSED && pcb->state != TCP_STATE_TIME_WAIT) {
		TCPSendSegment(sock, TCP_FLAGS_RST | TCP_FLAGS_ACK, NULL, 0);
		pcb->state = TCP_STATE_CLOSED;
	}
}

/* Userspace compares connect() against this. XEClib's EINPROGRESS is 119. */
#define AU_TCP_EINPROGRESS 119

/* One poll of a handshake that was started with sock->nonblock. */
static int AuTCPConnectPoll(AuSocket* sock, TCPControlBlock* pcb) {
	uint64_t ns, nss;

	AuNetRxPoll();
	if (sock->sockState == SOCK_STATE_CONNECTION_RST || pcb->state == TCP_STATE_CLOSED) {
		UARTDebugOut("[aurora]: TCP connection reset\r\n");
		return -1;
	}
	if (pcb->state == TCP_STATE_ESTABLISHED) {
		sock->sockState = SOCK_STATE_CONNECTED;
		UARTDebugOut("[aurora]: TCP connection succeeded\r\n");
		return 0;
	}
	if (pcb->state != TCP_STATE_SYN_SENT) {
		UARTDebugOut("[aurora]: TCP connect failed, state %d\r\n", pcb->state);
		return -1;
	}
	aa64_calculate_ticks(0, 0, &ns, &nss);
	if (ns > pcb->syn_sec || (ns == pcb->syn_sec && nss > pcb->syn_sub)) {
		if (pcb->syn_tries >= 3) {
			UARTDebugOut("[aurora]: TCP connect timeout\r\n");
			pcb->state = TCP_STATE_CLOSED;
			return -1;
		}
		UARTDebugOut("[aurora]: TCP retrying connection\r\n");
		pcb->snd_nxt = pcb->iss;
		if (TCPSendSegment(sock, TCP_FLAGS_SYN, NULL, 0) != 0) {
			pcb->syn_tries++;
			aa64_calculate_ticks(1, 0, &pcb->syn_sec, &pcb->syn_sub);
			return -AU_TCP_EINPROGRESS;
		}
		pcb->syn_tries++;
		aa64_calculate_ticks(1, 0, &pcb->syn_sec, &pcb->syn_sub);
	}
	return -AU_TCP_EINPROGRESS;
}

int AuTCPConnect(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	AuVFSNode* nic;
	AuNetworkDevice* ndev;
	uint64_t s, ss = 0;
	uint64_t ns, nss = 0;
	int attempts = 0;

	if (!pcb || !addr)
		return -1;

	if (pcb->state == TCP_STATE_ESTABLISHED) {
		sock->sockState = SOCK_STATE_CONNECTED;
		return 0;
	}
	/* A second connect() on a non-blocking socket only checks the handshake.
	 * Falling through would mint a new sequence number and send another SYN. */
	if (pcb->state == TCP_STATE_SYN_SENT && sock->nonblock)
		return AuTCPConnectPoll(sock, pcb);

	UARTDebugOut("[aurora]: TCP connect \r\n");
	if (sock->sessionPort == 0)
		AuTCPObtainPort(sock);
	else
		TCPRegisterSocket(sock);

	if (addr->sa_family == AF_INET6) {
		sockaddr_in6* sockdata6 = (sockaddr_in6*)addr;
		(void)addrlen;
		pcb->is_ipv6 = 1;
		nic = AuNetworkRoute6((const ip6_addr*)&sockdata6->sin6_addr);
		if (!nic)
			return -114; /* ENETUNREACH */
		ndev = (AuNetworkDevice*)nic->device;
		if (!ndev)
			return -1;
		pcb->nic = nic;
		memcpy(pcb->remote_ip6.s6_addr, sockdata6->sin6_addr.s6_addr, 16);
		pcb->remote_port = ntohs(sockdata6->sin6_port);
	} else {
		sockaddr_in* sockdata = (sockaddr_in*)addr;
		pcb->is_ipv6 = 0;
		nic = AuNetworkRoute(sockdata->sin_addr.s_addr);
		if (!nic) {
			UARTDebugOut("[aurora]: TCP connect ENETUNREACH\r\n");
			return -114; /* ENETUNREACH */
		}
		ndev = (AuNetworkDevice*)nic->device;
		if (!ndev) {
			UARTDebugOut("[aurora]: TCP connect, no NIC data\r\n");
			return -1;
		}
		pcb->nic = nic;
		pcb->remote_ip = sockdata->sin_addr.s_addr;
		pcb->remote_port = ntohs(sockdata->sin_port);
	}

	pcb->iss = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
	if (pcb->iss == 0)
		pcb->iss = 1;
	pcb->snd_una = pcb->iss;
	pcb->snd_nxt = pcb->iss;
	pcb->rcv_nxt = 0;
	sock->ipv4Iden = (uint16_t)rand();
	sock->sockState = SOCK_STATE_WAITING_FOR_CONNECTION;
	pcb->state = TCP_STATE_SYN_SENT;

	/* A miss here means the gateway ARP is not in yet. Stay in SYN_SENT
	 * and let the wait below send the SYN again instead of failing the
	 * first connect of the boot. */
	TCPSendSegment(sock, TCP_FLAGS_SYN, NULL, 0);

	aa64_calculate_ticks(1, 0, &pcb->syn_sec, &pcb->syn_sub);
	pcb->syn_tries = 0;
	/* The overlapped GET path sets this so eight handshakes share one thread.
	 * A plain connect() leaves it clear and still waits here. */
	if (sock->nonblock)
		return -AU_TCP_EINPROGRESS;

	aa64_calculate_ticks(1, 0, &s, &ss);
	while (pcb->state == TCP_STATE_SYN_SENT) {
		AuNetRxPoll();
		AuSleepThread(AuGetCurrentThread(), 20);
		AuScheduleNext();
		aa64_calculate_ticks(0, 0, &ns, &nss);

		if (sock->sockState == SOCK_STATE_CONNECTION_RST) {
			UARTDebugOut("[aurora]: TCP connection reset\r\n");
			return -1;
		}
		if ((ns > s || (ns == s && nss > ss))) {
			if (attempts == 3) {
				UARTDebugOut("[aurora]: TCP connect timeout\r\n");
				pcb->state = TCP_STATE_CLOSED;
				return -1;
			}
			UARTDebugOut("[aurora]: TCP retrying connection\r\n");
			pcb->snd_nxt = pcb->iss;
			TCPSendSegment(sock, TCP_FLAGS_SYN, NULL, 0);
			aa64_calculate_ticks(1, 0, &s, &ss);
			attempts++;
		}
	}

	if (pcb->state != TCP_STATE_ESTABLISHED) {
		UARTDebugOut("[aurora]: TCP connect failed, state %d\r\n", pcb->state);
		return -1;
	}
	sock->sockState = SOCK_STATE_CONNECTED;
	UARTDebugOut("[aurora]: TCP connection succeeded\r\n");
	return 0;
}

int AuTCPBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	int port = 0;
	int i;

	if (!addr || !pcb)
		return -1;
	if (sock->sessionPort != 0)
		return -1;

	(void)addrlen;
	if (addr->sa_family == AF_INET6) {
		sockaddr_in6* addr6 = (sockaddr_in6*)addr;
		pcb->is_ipv6 = 1;
		port = ntohs(addr6->sin6_port);
	} else {
		sockaddr_in* addr_in = (sockaddr_in*)addr;
		pcb->is_ipv6 = 0;
		port = ntohs(addr_in->sin_port);
	}

	if (port == 0) {
		AuTCPObtainPort(sock);
		return 0;
	}
	if (!tcpSocketList)
		return -1;
	for (i = 0; i < (int)tcpSocketList->pointer; i++) {
		AuSocket* other = (AuSocket*)list_get_at(tcpSocketList, i);
		TCPControlBlock* otherpcb = TCPGetPCB(other);
		if (other && other->sessionPort == (uint16_t)port && otherpcb &&
			otherpcb->state == TCP_STATE_LISTEN)
			return -1;
	}
	sock->sessionPort = (uint16_t)port;
	TCPRegisterSocket(sock);
	UARTDebugOut("[aurora]: TCP bound to port %d\r\n", port);
	return 0;
}

int AuTCPListen(AuSocket* sock, int backlog) {
	TCPControlBlock* pcb = TCPGetPCB(sock);

	if (!pcb)
		return -1;
	if (sock->sessionPort == 0)
		return -1;
	if (backlog <= 0)
		backlog = 1;
	pcb->backlog = backlog;
	if (!pcb->acceptq)
		pcb->acceptq = initialize_list();
	pcb->state = TCP_STATE_LISTEN;
	sock->sockState = SOCK_STATE_CONNECTED;
	UARTDebugOut("[aurora]: TCP listen on %d\r\n", sock->sessionPort);
	return 0;
}

int AuTCPAccept(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	AuSocket* child = NULL;
	TCPControlBlock* childpcb;
	AA64Thread* thread;
	AuProcess* proc;
	AuVFSNode* node;
	int fd;
	uint64_t s, ss = 0;
	uint64_t ns, nss = 0;

	if (!pcb || pcb->state != TCP_STATE_LISTEN || !pcb->acceptq)
		return -1;

	aa64_calculate_ticks(30, 0, &s, &ss);
	while (pcb->acceptq->pointer == 0) {
		AuSleepThread(AuGetCurrentThread(), 20);
		AuScheduleNext();
		aa64_calculate_ticks(0, 0, &ns, &nss);
		if (ns > s || (ns == s && nss > ss))
			return -1;
	}

	child = (AuSocket*)list_remove(pcb->acceptq, 0);
	if (!child)
		return -1;
	childpcb = TCPGetPCB(child);
	thread = AuGetCurrentThread();
	if (!thread)
		return -1;
	proc = AuProcessFindThread(thread);
	if (!proc)
		proc = AuProcessFindSubThread(thread);
	if (!proc)
		return -1;

	fd = AuProcessGetFileDesc(proc);
	node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!node)
		return -1;
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "tcp");
	node->flags |= FS_FLAG_SOCKET;
	node->device = child;
	node->close = AuTCPFileClose;
	node->iocontrol = SocketIOControl;
	proc->fds[fd] = node;

	if (addr && addrlen && childpcb) {
		if (childpcb->is_ipv6 && *addrlen >= sizeof(sockaddr_in6)) {
			sockaddr_in6* in6 = (sockaddr_in6*)addr;
			in6->sin6_family = AF_INET6;
			in6->sin6_port = htons(childpcb->remote_port);
			in6->sin6_flowinfo = 0;
			memcpy(in6->sin6_addr.s6_addr, childpcb->remote_ip6.s6_addr, 16);
			in6->sin6_scope_id = 0;
			*addrlen = sizeof(sockaddr_in6);
		} else if (*addrlen >= sizeof(sockaddr_in)) {
			sockaddr_in* in = (sockaddr_in*)addr;
			in->sin_family = AF_INET;
			in->sin_port = htons(childpcb->remote_port);
			in->sin_addr.s_addr = childpcb->remote_ip;
			*addrlen = sizeof(sockaddr_in);
		}
	}
	return fd;
}

static void TCPHandleListenSyn(AuSocket* listener, IPv4Header* pack, TCPHeader* tcp, AuVFSNode* nic) {
	TCPControlBlock* lpcb = TCPGetPCB(listener);
	AuSocket* child;
	TCPControlBlock* pcb;
	uint32_t seq;

	if (!lpcb)
		return;
	if (lpcb->acceptq && (int)lpcb->acceptq->pointer >= lpcb->backlog) {
		TCPSendReset(nic, pack, tcp, 1);
		return;
	}

	child = TCPAllocSocket();
	if (!child) {
		TCPSendReset(nic, pack, tcp, 1);
		return;
	}
	pcb = TCPGetPCB(child);
	seq = ntohl(tcp->sequenceNum);
	child->sessionPort = listener->sessionPort;
	child->ipv4Iden = (uint16_t)rand();
	pcb->nic = nic;
	pcb->parent = listener;
	pcb->remote_ip = pack->srcAddress;
	pcb->remote_port = ntohs(tcp->srcPort);
	pcb->irs = seq;
	pcb->rcv_nxt = seq + 1;
	pcb->iss = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
	if (pcb->iss == 0)
		pcb->iss = 1;
	pcb->snd_una = pcb->iss;
	pcb->snd_nxt = pcb->iss;
	pcb->snd_wnd = ntohs(tcp->window);
	pcb->state = TCP_STATE_SYN_RECEIVED;
	child->sockState = SOCK_STATE_WAITING_FOR_CONNECTION;
	TCPRegisterSocket(child);
	TCPSendSegment(child, TCP_FLAGS_SYN | TCP_FLAGS_ACK, NULL, 0);
}

static void TCPHandleEstablished(AuSocket* sock, IPv4Header* pack, TCPHeader* tcp,
	const uint8_t* payload, size_t payloadLen, uint16_t flags) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	uint32_t seq;
	uint32_t ack;
	int wrote;
	int needAck = 0;

	(void)pack;
	if (!pcb)
		return;
	seq = ntohl(tcp->sequenceNum);
	ack = ntohl(tcp->ackNum);

	if (flags & TCP_FLAGS_ACK) {
		if ((int32_t)(ack - pcb->snd_una) > 0 && (int32_t)(pcb->snd_nxt - ack) >= 0)
			pcb->snd_una = ack;
		pcb->snd_wnd = ntohs(tcp->window);

		if (pcb->state == TCP_STATE_SYN_RECEIVED && ack == pcb->snd_nxt) {
			pcb->state = TCP_STATE_ESTABLISHED;
			sock->sockState = SOCK_STATE_CONNECTED;
			if (pcb->parent)
				TCPQueueAccept(pcb->parent, sock);
		}
		if (pcb->state == TCP_STATE_FIN_WAIT_1 && ack == pcb->snd_nxt)
			pcb->state = TCP_STATE_FIN_WAIT_2;
		if (pcb->state == TCP_STATE_LAST_ACK && ack == pcb->snd_nxt)
			pcb->state = TCP_STATE_CLOSED;
		if (pcb->state == TCP_STATE_CLOSING && ack == pcb->snd_nxt)
			pcb->state = TCP_STATE_TIME_WAIT;
	}

	if (pcb->state == TCP_STATE_SYN_SENT)
		return;

	if (payloadLen) {
		if (seq != pcb->rcv_nxt) {
			TCPSendAck(sock);
			return;
		}
		/* A short accept would ACK bytes the buffer did not keep. Refuse
		 * the segment and advertise the space that is actually free. */
		if ((size_t)TCPWindowOf(sock) < payloadLen) {
			needAck = 1;
		} else {
			wrote = TCPWriteRx(pcb, payload, payloadLen);
			if (wrote > 0) {
				pcb->rcv_nxt += (uint32_t)wrote;
				needAck = 1;
			}
		}
	}

	if (flags & TCP_FLAGS_FIN) {
		if (seq + (uint32_t)payloadLen == pcb->rcv_nxt || seq == pcb->rcv_nxt) {
			pcb->rcv_nxt += 1;
			pcb->fin_recvd = 1;
			needAck = 1;
			if (pcb->state == TCP_STATE_ESTABLISHED)
				pcb->state = TCP_STATE_CLOSE_WAIT;
			else if (pcb->state == TCP_STATE_FIN_WAIT_1)
				pcb->state = TCP_STATE_CLOSING;
			else if (pcb->state == TCP_STATE_FIN_WAIT_2)
				pcb->state = TCP_STATE_TIME_WAIT;
		}
	}

	if (needAck)
		TCPSendAck(sock);
}

void TCPHandlePacket(IPv4Header* pack, AuVFSNode* nic) {
	int ihl;
	int doff;
	uint16_t tot;
	uint16_t destPort;
	uint16_t srcPort;
	uint16_t flags;
	size_t payloadLen;
	size_t segLen;
	TCPHeader* tcp;
	AuSocket* sock;
	TCPControlBlock* pcb;
	const uint8_t* payload;

	if (!pack)
		return;
	ihl = (pack->versionHeaderLen & 0x0F) * 4;
	if (ihl < 20)
		return;
	tcp = (TCPHeader*)((uint8_t*)pack + ihl);
	doff = TCPHdrBytes(tcp);
	if (doff < 20)
		return;
	tot = ntohs(pack->totalLength);
	if (tot < (uint16_t)(ihl + doff))
		return;
	payloadLen = (size_t)tot - (size_t)ihl - (size_t)doff;
	payload = (const uint8_t*)tcp + doff;
	flags = TCPFlagsOf(tcp);
	destPort = ntohs(tcp->destPort);
	srcPort = ntohs(tcp->srcPort);
	segLen = payloadLen;
	if (flags & TCP_FLAGS_SYN)
		segLen++;
	if (flags & TCP_FLAGS_FIN)
		segLen++;

	sock = TCPFindSocket(pack->srcAddress, srcPort, destPort);
	if (!sock) {
		if (!(flags & TCP_FLAGS_RST))
			TCPSendReset(nic, pack, tcp, segLen);
		return;
	}
	pcb = TCPGetPCB(sock);
	if (!pcb)
		return;

	if (flags & TCP_FLAGS_RST) {
		sock->sockState = SOCK_STATE_CONNECTION_RST;
		pcb->state = TCP_STATE_CLOSED;
		return;
	}

	if (pcb->state == TCP_STATE_LISTEN) {
		if (flags & TCP_FLAGS_SYN)
			TCPHandleListenSyn(sock, pack, tcp, nic);
		return;
	}

	if (pcb->state == TCP_STATE_SYN_SENT) {
		if ((flags & (TCP_FLAGS_SYN | TCP_FLAGS_ACK)) == (TCP_FLAGS_SYN | TCP_FLAGS_ACK)) {
			uint32_t ack = ntohl(tcp->ackNum);
			if (ack != pcb->iss + 1) {
				TCPSendReset(nic, pack, tcp, segLen);
				return;
			}
			pcb->irs = ntohl(tcp->sequenceNum);
			pcb->rcv_nxt = pcb->irs + 1;
			pcb->snd_una = ack;
			pcb->snd_wnd = ntohs(tcp->window);
			if (!pcb->nic)
				pcb->nic = nic;
			pcb->state = TCP_STATE_ESTABLISHED;
			sock->sockState = SOCK_STATE_CONNECTED;
			TCPSendAck(sock);
			return;
		}
		if (flags & TCP_FLAGS_SYN) {
			pcb->irs = ntohl(tcp->sequenceNum);
			pcb->rcv_nxt = pcb->irs + 1;
			pcb->state = TCP_STATE_SYN_RECEIVED;
			pcb->snd_nxt = pcb->iss;
			TCPSendSegment(sock, TCP_FLAGS_SYN | TCP_FLAGS_ACK, NULL, 0);
			return;
		}
		return;
	}

	TCPHandleEstablished(sock, pack, tcp, payload, payloadLen, flags);
}

static void TCPHandleListenSyn6(AuSocket* listener, IPv6Header* pack, TCPHeader* tcp, AuVFSNode* nic) {
	TCPControlBlock* lpcb = TCPGetPCB(listener);
	AuSocket* child;
	TCPControlBlock* pcb;
	uint32_t seq;

	if (!lpcb)
		return;
	if (lpcb->acceptq && (int)lpcb->acceptq->pointer >= lpcb->backlog)
		return;

	child = TCPAllocSocket();
	if (!child)
		return;
	pcb = TCPGetPCB(child);
	seq = ntohl(tcp->sequenceNum);
	child->sessionPort = listener->sessionPort;
	child->ipv4Iden = (uint16_t)rand();
	pcb->nic = nic;
	pcb->parent = listener;
	pcb->is_ipv6 = 1;
	ip6_addr_copy(&pcb->remote_ip6, &pack->srcIP);
	pcb->remote_port = ntohs(tcp->srcPort);
	pcb->irs = seq;
	pcb->rcv_nxt = seq + 1;
	pcb->iss = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
	if (pcb->iss == 0)
		pcb->iss = 1;
	pcb->snd_una = pcb->iss;
	pcb->snd_nxt = pcb->iss;
	pcb->snd_wnd = ntohs(tcp->window);
	pcb->state = TCP_STATE_SYN_RECEIVED;
	child->sockState = SOCK_STATE_WAITING_FOR_CONNECTION;
	TCPRegisterSocket(child);
	TCPSendSegment(child, TCP_FLAGS_SYN | TCP_FLAGS_ACK, NULL, 0);
}

void TCPHandlePacket6(IPv6Header* pack, AuVFSNode* nic) {
	int doff;
	uint16_t destPort;
	uint16_t srcPort;
	uint16_t flags;
	size_t payloadLen;
	TCPHeader* tcp;
	AuSocket* sock;
	TCPControlBlock* pcb;
	const uint8_t* payload;

	if (!pack)
		return;
	tcp = (TCPHeader*)&pack->payload;
	doff = TCPHdrBytes(tcp);
	if (doff < 20)
		return;
	payloadLen = (size_t)ntohs(pack->payloadLen);
	if (payloadLen < (size_t)doff)
		return;
	payloadLen -= (size_t)doff;
	payload = (const uint8_t*)tcp + doff;
	flags = TCPFlagsOf(tcp);
	destPort = ntohs(tcp->destPort);
	srcPort = ntohs(tcp->srcPort);

	sock = TCPFindSocket6(&pack->srcIP, srcPort, destPort);
	if (!sock)
		return;
	pcb = TCPGetPCB(sock);
	if (!pcb)
		return;

	if (flags & TCP_FLAGS_RST) {
		sock->sockState = SOCK_STATE_CONNECTION_RST;
		pcb->state = TCP_STATE_CLOSED;
		return;
	}

	if (pcb->state == TCP_STATE_LISTEN) {
		if (flags & TCP_FLAGS_SYN)
			TCPHandleListenSyn6(sock, pack, tcp, nic);
		return;
	}

	if (pcb->state == TCP_STATE_SYN_SENT) {
		if ((flags & (TCP_FLAGS_SYN | TCP_FLAGS_ACK)) == (TCP_FLAGS_SYN | TCP_FLAGS_ACK)) {
			uint32_t ack = ntohl(tcp->ackNum);
			if (ack != pcb->iss + 1)
				return;
			pcb->irs = ntohl(tcp->sequenceNum);
			pcb->rcv_nxt = pcb->irs + 1;
			pcb->snd_una = ack;
			pcb->snd_wnd = ntohs(tcp->window);
			if (!pcb->nic)
				pcb->nic = nic;
			pcb->is_ipv6 = 1;
			ip6_addr_copy(&pcb->remote_ip6, &pack->srcIP);
			pcb->state = TCP_STATE_ESTABLISHED;
			sock->sockState = SOCK_STATE_CONNECTED;
			TCPSendAck(sock);
			return;
		}
		return;
	}

	TCPHandleEstablished(sock, NULL, tcp, payload, payloadLen, flags);
}

int AuTCPFileClose(AuVFSNode* fsys, AuVFSNode* file) {
	AuSocket* sock;

	(void)fsys;
	if (!file)
		return 0;
	sock = (AuSocket*)file->device;
	if (sock) {
		AuTCPClose(sock);
		TCPUnregisterSocket(sock);
		if (sock->rxstack) {
			while (sock->rxstack->itemCount) {
				void* data = AuStackPop(sock->rxstack);
				kfree(data);
			}
			kfree(sock->rxstack);
		}
		TCPFreePCB(TCPGetPCB(sock));
		sock->proto = NULL;
		kfree(sock);
	}
	kfree(file);
	UARTDebugOut("[aurora]: TCP socket closed\r\n");
	return 0;
}

int CreateTCPSocket() {
	int fd = -1;
	AA64Thread* thread = AuGetCurrentThread();
	AuProcess* proc;
	AuSocket* sock;
	AuVFSNode* node;

	if (!thread)
		return -1;
	proc = AuProcessFindThread(thread);
	if (!proc)
		proc = AuProcessFindSubThread(thread);
	if (!proc)
		return -1;
	sock = TCPAllocSocket();
	if (!sock)
		return -1;
	fd = AuProcessGetFileDesc(proc);
	node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "tcp");
	node->flags |= FS_FLAG_SOCKET;
	node->device = sock;
	node->close = AuTCPFileClose;
	node->iocontrol = SocketIOControl;
	proc->fds[fd] = node;
	UARTDebugOut("[aurora]: TCP socket created\r\n");
	return fd;
}

list_t* TCPGetSocketList() {
	return tcpSocketList;
}

void TCPProtocolInstall() {
	tcpSocketList = initialize_list();
	UARTDebugOut("[aurora]: TCP protocol installed\r\n");
}
