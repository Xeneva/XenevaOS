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
/* Sockets whose ACK is held until the current virtio RX drain completes. */
static TCPControlBlock* tcp_ack_head;

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

static uint16_t CalculateTCPChecksum(TCPCheckHeader* p, TCPHeader* h,
	size_t hdrsz, void* d, size_t payloadsz) {
	uint32_t sum = 0;
	uint16_t* s = (uint16_t*)p;
	size_t hwords;
	size_t dwords;
	size_t i;

	if (hdrsz < sizeof(TCPHeader))
		hdrsz = sizeof(TCPHeader);

	for (i = 0; i < 6; ++i)
		sum += ntohs(s[i]);

	/* The TCP checksum covers the whole header, including options such as
	 * the MSS option carried by SYN and SYN-ACK. Accumulate in 32 bits and
	 * fold once at the end; the largest segment is 32 KiB, so the sum
	 * cannot overflow. */
	hwords = hdrsz / 2;
	s = (uint16_t*)h;
	for (i = 0; i < hwords; ++i)
		sum += ntohs(s[i]);

	if (d && payloadsz) {
		dwords = payloadsz / 2;
		s = (uint16_t*)d;
		for (i = 0; i < dwords; ++i)
			sum += ntohs(s[i]);

		if (payloadsz & 1) {
			const uint8_t* tail = (const uint8_t*)d;

			/* The final odd byte is the high byte of the last word. */
			sum += (uint16_t)tail[dwords * 2] << 8;
		}
	}

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

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

static size_t TCPRxFree(TCPControlBlock* pcb) {
	size_t used;

	if (!pcb || !pcb->rxbuf)
		return TCP_DEFAULT_WIN_SZ;
	used = AuCircBufSize((CircBuffer*)pcb->rxbuf);
	if (used >= TCP_RX_BUF_SZ)
		return 0;
	return TCP_RX_BUF_SZ - used;
}

/* Return the value for the 16-bit window field. Before the handshake has
 * negotiated RFC 7323 scaling the field is the literal byte count; after it,
 * the field is the free space shifted by our advertised scale. */
static uint16_t TCPWindowOf(AuSocket* sock, int syn) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	size_t free;

	if (!pcb || !pcb->rxbuf)
		return TCP_DEFAULT_WIN_SZ;
	free = TCPRxFree(pcb);
	if (!syn && pcb->ws_enabled)
		free >>= pcb->rcv_wscale;
	/* A 16-bit field must never advertise 65536: that truncates to zero. */
	if (free > 65535)
		return 65535;
	return (uint16_t)free;
}

/* Pick the largest segment this path can carry. Loopback reinjects at IP and
 * never touches L2, so it can use a much larger MSS than an Ethernet path.
 * External IPv6 subtracts 60 bytes of headers from a 1500-byte MTU. */
static uint16_t TCPPathMSS(AuVFSNode* nic, int is_ipv6, uint32_t remote_ip,
	const ip6_addr* remote_ip6) {
	AuNetworkDevice* ndev = NULL;

	if (nic && nic->device)
		ndev = (AuNetworkDevice*)nic->device;
	if (ndev && ndev->type == NETDEV_TYPE_LOOPBACK)
		return TCP_MSS_LOOPBACK;
	if (is_ipv6) {
		if (remote_ip6 && AuAddrIsLocal6(remote_ip6))
			return TCP_MSS_LOOPBACK;
		return TCP_MSS_IPV6;
	}
	if (AuAddrIsLocal4(remote_ip))
		return TCP_MSS_LOOPBACK;
	return TCP_MSS_IPV4;
}

/* Parse the MSS option from a SYN or SYN-ACK. Returns 0 when the option is
 * absent, so the caller can apply the RFC 1122 default. */
static uint16_t TCPParseMSS(const TCPHeader* tcp, int doff) {
	const uint8_t* opt;
	int len;
	int i = 0;

	if (!tcp || doff <= (int)sizeof(TCPHeader))
		return 0;
	opt = (const uint8_t*)tcp + sizeof(TCPHeader);
	len = doff - (int)sizeof(TCPHeader);
	while (i < len) {
		uint8_t kind = opt[i];
		uint8_t olen;

		if (kind == TCP_OPTION_END)
			break;
		if (kind == TCP_OPTION_NOP) {
			i++;
			continue;
		}
		if (i + 1 >= len)
			break;
		olen = opt[i + 1];
		if (olen < 2 || i + olen > len)
			break;
		if (kind == TCP_OPTION_MSS && olen == TCP_OPTION_MSS_LEN) {
			uint16_t mss = (uint16_t)(((uint16_t)opt[i + 2] << 8) |
				opt[i + 3]);
			return mss < TCP_MSS_MIN ? TCP_MSS_MIN : mss;
		}
		i += olen;
	}
	return 0;
}

/* Parse the RFC 7323 window-scale option from a SYN or SYN-ACK. Returns 1
 * when present (shift 0 is valid) and stores the shift in *out. */
static int TCPParseWS(const TCPHeader* tcp, int doff, uint8_t* out) {
	const uint8_t* opt;
	int len;
	int i = 0;

	if (!tcp || !out || doff <= (int)sizeof(TCPHeader))
		return 0;
	opt = (const uint8_t*)tcp + sizeof(TCPHeader);
	len = doff - (int)sizeof(TCPHeader);
	while (i < len) {
		uint8_t kind = opt[i];
		uint8_t olen;

		if (kind == TCP_OPTION_END)
			break;
		if (kind == TCP_OPTION_NOP) {
			i++;
			continue;
		}
		if (i + 1 >= len)
			break;
		olen = opt[i + 1];
		if (olen < 2 || i + olen > len)
			break;
		if (kind == TCP_OPTION_WS && olen == TCP_OPTION_WS_LEN) {
			uint8_t shift = opt[i + 2];

			*out = shift > 14 ? 14 : shift;
			return 1;
		}
		i += olen;
	}
	return 0;
}

/* Clamp our send MSS to what the peer can receive. A missing option means the
 * RFC 1122 default of 536 bytes, not the path MTU. */
static void TCPApplyPeerMSS(TCPControlBlock* pcb, uint16_t peer_mss) {
	if (!pcb)
		return;
	if (!peer_mss)
		peer_mss = TCP_MSS_MIN;
	if (peer_mss < pcb->mss)
		pcb->mss = peer_mss;
	if (pcb->mss < TCP_MSS_MIN)
		pcb->mss = TCP_MSS_MIN;
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

/* A connect() to a local port that carries a socket but no listener has
 * nothing to complete the handshake.  Reject it up front so the SYN is not
 * re-injected into our own socket. */
static int TCPLocalPortRefused(uint32_t ip, uint16_t port) {
	int i;

	if (!tcpSocketList || !AuAddrIsLocal4(ip))
		return 0;
	for (i = 0; i < (int)tcpSocketList->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(tcpSocketList, i);
		TCPControlBlock* pcb;
		if (!sock || sock->sessionPort != port)
			continue;
		pcb = TCPGetPCB(sock);
		if (pcb && pcb->state == TCP_STATE_LISTEN)
			return 0;
	}
	return TCPPortInUse(port);
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
	uint16_t tcpHdrLen;
	uint16_t tcpLen;
	int syn;

	if (!pcb || !pcb->nic)
		return -1;
	ndev = (AuNetworkDevice*)pcb->nic->device;
	if (!ndev)
		return -1;
	if (!TCPLinkReady(pcb))
		return -1;

	seq = pcb->snd_nxt;
	syn = (flags & TCP_FLAGS_SYN) != 0;
	/* SYN and SYN-ACK carry an MSS option and, when offered, an RFC 7323
	 * window-scale option padded with one NOP: 8 bytes total. The checksum
	 * must cover the option bytes too. */
	tcpHdrLen = (uint16_t)(sizeof(TCPHeader) + (syn ? 8 : 0));
	tcpLen = (uint16_t)(tcpHdrLen + payloadLen);

	if (pcb->is_ipv6) {
		IPv6Header* ipv6;

		totalLen = sizeof(IPv6Header) + tcpLen;
		ipv6 = (IPv6Header*)kmalloc(totalLen);
		if (!ipv6)
			return -1;
		/* Zero only the headers. The payload area is fully overwritten by
		 * the memcpy below; zeroing a whole 32 KiB segment first would add
		 * a full MiB of memset traffic to every MiB transferred. */
		memset(ipv6, 0, sizeof(IPv6Header) + tcpHdrLen);
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
		tcp->dataOffsetFlags = htons(flags |
			(uint16_t)((tcpHdrLen / 4) << 12));
		tcp->window = htons(TCPWindowOf(sock, syn));
		tcp->checksum = 0;
		tcp->urgentPointer = 0;
		if (syn) {
			uint8_t* opt = (uint8_t*)tcp + sizeof(TCPHeader);

			opt[0] = TCP_OPTION_MSS;
			opt[1] = TCP_OPTION_MSS_LEN;
			opt[2] = (uint8_t)(pcb->rcv_mss >> 8);
			opt[3] = (uint8_t)(pcb->rcv_mss & 0xFF);
			if (pcb->ws_offer) {
				opt[4] = TCP_OPTION_WS;
				opt[5] = TCP_OPTION_WS_LEN;
				opt[6] = pcb->rcv_wscale;
				opt[7] = TCP_OPTION_NOP;
			} else {
				opt[4] = TCP_OPTION_END;
				opt[5] = TCP_OPTION_END;
				opt[6] = TCP_OPTION_END;
				opt[7] = TCP_OPTION_END;
			}
		}
		if (payload && payloadLen)
			memcpy((uint8_t*)tcp + tcpHdrLen, (void*)payload, payloadLen);

		tcp->checksum = htons(IPv6PseudoChecksum(&ipv6->srcIP, &ipv6->destIP,
			tcpLen, IPV6_NEXT_TCP, tcp, tcpLen));
		/* Sequence space must be advanced before the packet is handed to
		 * the stack: loopback delivery runs TCP input inline, and an ACK
		 * for this segment would otherwise look out of window. */
		if (flags & TCP_FLAGS_SYN)
			pcb->snd_nxt = seq + 1;
		else {
			pcb->snd_nxt = seq + (uint32_t)payloadLen;
			if (flags & TCP_FLAGS_FIN)
				pcb->snd_nxt += 1;
		}
		IPV6SendPacket(ipv6, pcb->nic);
		kfree(ipv6);
	} else {
		IPv4Header* ipv4;
		TCPCheckHeader checkhdr;

		totalLen = sizeof(IPv4Header) + tcpLen;
		ipv4 = (IPv4Header*)kmalloc(totalLen);
		if (!ipv4)
			return -1;
		/* See the IPv6 branch: only the headers need clearing. */
		memset(ipv4, 0, sizeof(IPv4Header) + tcpHdrLen);

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
		tcp->dataOffsetFlags = htons(flags |
			(uint16_t)((tcpHdrLen / 4) << 12));
		tcp->window = htons(TCPWindowOf(sock, syn));
		tcp->checksum = 0;
		tcp->urgentPointer = 0;
		if (syn) {
			uint8_t* opt = (uint8_t*)tcp + sizeof(TCPHeader);

			opt[0] = TCP_OPTION_MSS;
			opt[1] = TCP_OPTION_MSS_LEN;
			opt[2] = (uint8_t)(pcb->rcv_mss >> 8);
			opt[3] = (uint8_t)(pcb->rcv_mss & 0xFF);
			if (pcb->ws_offer) {
				opt[4] = TCP_OPTION_WS;
				opt[5] = TCP_OPTION_WS_LEN;
				opt[6] = pcb->rcv_wscale;
				opt[7] = TCP_OPTION_NOP;
			} else {
				opt[4] = TCP_OPTION_END;
				opt[5] = TCP_OPTION_END;
				opt[6] = TCP_OPTION_END;
				opt[7] = TCP_OPTION_END;
			}
		}
		if (payload && payloadLen)
			memcpy((uint8_t*)tcp + tcpHdrLen, (void*)payload, payloadLen);

		checkhdr.source = ipv4->srcAddress;
		checkhdr.destination = ipv4->destAddress;
		checkhdr.zeros = 0;
		checkhdr.protocol = IPV4_PROTOCOL_TCP;
		checkhdr.tcpLen = htons(tcpLen);
		tcp->checksum = htons(CalculateTCPChecksum(&checkhdr, tcp, tcpHdrLen,
			payloadLen ? (uint8_t*)tcp + tcpHdrLen : NULL, payloadLen));

		if (flags & TCP_FLAGS_SYN)
			pcb->snd_nxt = seq + 1;
		else {
			pcb->snd_nxt = seq + (uint32_t)payloadLen;
			if (flags & TCP_FLAGS_FIN)
				pcb->snd_nxt += 1;
		}
		IPV4SendPacket(ipv4, pcb->nic);
		kfree(ipv4);
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
	rst->checksum = htons(CalculateTCPChecksum(&checkhdr, rst,
		sizeof(TCPHeader), NULL, 0));
	IPV4SendPacket(ipv4, nic);
	kfree(ipv4);
}

static void TCPUnlinkDeferred(TCPControlBlock* pcb);

static void TCPFreePCB(TCPControlBlock* pcb) {
	if (!pcb)
		return;
	if (pcb->ack_deferred)
		TCPUnlinkDeferred(pcb);
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
	pcb->mss = TCP_MSS_IPV4;
	pcb->rcv_mss = TCP_MSS_IPV4;
	pcb->snd_wnd = TCP_DEFAULT_WIN_SZ;
	pcb->rcv_wnd = 65535;
	pcb->rcv_wscale = TCP_RCV_WSCALE;
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
static int AuTCPPoll(AuSocket* sock, int events);
static int AuTCPShutdown(AuSocket* sock, int how);
static int AuTCPGetSockName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen);
static int AuTCPGetPeerName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen);
static void TCPSaveLocal(AuSocket* sock, const sockaddr* addr,
	socklen_t addrlen);
static void TCPFreeSocket(AuSocket* sock);

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
	sock->shutdown = AuTCPShutdown;
	sock->getsockname = AuTCPGetSockName;
	sock->getpeername = AuTCPGetPeerName;
	sock->poll = AuTCPPoll;
	sock->proto = pcb;
	pcb->owner = sock;
	sock->so_type = SOCK_STREAM;
	sock->family = AF_INET;
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
	if (pcb->acceptq) {
		list_add(pcb->acceptq, child);
		AuSocketWakeAccept(listener);
	}
}

static int TCPWriteRx(TCPControlBlock* pcb, const uint8_t* data, size_t len) {
	CircBuffer* buf;

	if (!pcb || !pcb->rxbuf || !data || !len)
		return 0;
	buf = (CircBuffer*)pcb->rxbuf;
	return (int)AuCircBufWrite(buf, data, len);
}

static int TCPSendAck(AuSocket* sock) {
	TCPControlBlock* pcb = TCPGetPCB(sock);

	/* Every ACK carries the current receive window, so a pending delayed
	 * ACK is satisfied by this segment. */
	if (pcb)
		pcb->ack_pending = 0;
	return TCPSendSegment(sock, TCP_FLAGS_ACK, NULL, 0);
}

static void TCPUnlinkDeferred(TCPControlBlock* pcb) {
	TCPControlBlock** pp = &tcp_ack_head;

	while (*pp) {
		if (*pp == pcb) {
			*pp = pcb->ack_next;
			break;
		}
		pp = &(*pp)->ack_next;
	}
	pcb->ack_deferred = 0;
	pcb->ack_next = NULL;
}

/* Hold the ACK until AuNetRxPoll finishes draining the ring. A bulk
 * receive then produces one ACK per drain instead of one per two
 * segments, which is what Linux does and what keeps the TX path out of
 * the per-packet budget. */
static void TCPDeferAck(AuSocket* sock) {
	TCPControlBlock* pcb = TCPGetPCB(sock);

	if (!pcb || pcb->ack_deferred)
		return;
	pcb->ack_deferred = 1;
	pcb->ack_next = tcp_ack_head;
	tcp_ack_head = pcb;
}

void AuTCPFlushDeferredAcks(void) {
	TCPControlBlock* pcb;

	while ((pcb = tcp_ack_head)) {
		tcp_ack_head = pcb->ack_next;
		pcb->ack_next = NULL;
		pcb->ack_deferred = 0;
		if (pcb->owner && pcb->ack_pending)
			TCPSendAck(pcb->owner);
	}
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

static size_t TCPRxUsed(TCPControlBlock* pcb) {
	if (!pcb || !pcb->rxbuf)
		return 0;
	return AuCircBufSize((CircBuffer*)pcb->rxbuf);
}

/* Non-destructive read used by MSG_PEEK. */
static size_t TCPPeekBuf(CircBuffer* buf, uint8_t* dst, size_t len) {
	size_t have;
	size_t i;

	if (!buf || !dst || !len || buf->max == 0)
		return 0;
	have = AuCircBufSize(buf);
	if (have > len)
		have = len;
	for (i = 0; i < have; i++)
		dst[i] = buf->buffer[(buf->tail + i) % buf->max];
	return have;
}

static void TCPFillPeerName(AuSocket* sock, TCPControlBlock* pcb, msghdr* msg) {
	uint8_t raw[sizeof(sockaddr_in6)];
	socklen_t len;

	(void)sock;
	if (!msg || !msg->msg_name || !pcb)
		return;
	memset(raw, 0, sizeof(raw));
	if (pcb->is_ipv6) {
		sockaddr_in6* in6 = (sockaddr_in6*)raw;
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons(pcb->remote_port);
		memcpy(in6->sin6_addr.s6_addr, pcb->remote_ip6.s6_addr, 16);
		len = sizeof(sockaddr_in6);
	} else {
		sockaddr_in* in = (sockaddr_in*)raw;
		in->sin_family = AF_INET;
		in->sin_port = htons(pcb->remote_port);
		in->sin_addr.s_addr = pcb->remote_ip;
		len = sizeof(sockaddr_in);
	}
	AuSocketCopyMsgName(msg, raw, len);
}

int AuTCPReceive(AuSocket* sock, msghdr* msg, int flags) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	CircBuffer* buf;
	uint64_t deadline;
	size_t want = 0;
	size_t got = 0;
	size_t i;
	int waitall;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (sock->shut_rd)
		return 0;
	if (!msg || msg->msg_iovlen == 0)
		return 0;
	for (i = 0; i < msg->msg_iovlen; i++)
		want += msg->msg_iov[i].iov_len;
	if (want == 0)
		return 0;

	buf = (CircBuffer*)pcb->rxbuf;
	waitall = (flags & MSG_WAITALL) && !(flags & MSG_PEEK);
	deadline = AuGetCurrentUS() + (uint64_t)30000 * 1000ULL;

	for (;;) {
		if (TCPRxUsed(pcb)) {
			size_t before = TCPRxFree(pcb);

			for (i = 0; i < msg->msg_iovlen && got < want; i++) {
				size_t len = msg->msg_iov[i].iov_len;
				uint8_t* base = (uint8_t*)msg->msg_iov[i].iov_base;

				if (!len || !base)
					continue;
				if (len > want - got)
					len = want - got;
				if (flags & MSG_PEEK)
					got += TCPPeekBuf(buf, base, len);
				else
					got += AuCircBufRead(buf, base, len);
			}

			if (got > 0 && !(flags & MSG_PEEK) &&
				(pcb->state == TCP_STATE_ESTABLISHED ||
					pcb->state == TCP_STATE_CLOSE_WAIT)) {
				size_t after = TCPRxFree(pcb);
				size_t reopen = (size_t)pcb->rcv_mss * 2;

				/* Flush a delayed ACK as soon as the reader drains
				 * data, and still send a window update if the buffer
				 * had been nearly full. */
				if (pcb->ack_pending ||
					(before < reopen && after > before))
					TCPSendAck(sock);
			}
			if (got == 0)
				return -AU_ERR_EAGAIN;
			if (!waitall || got >= want || pcb->fin_recvd) {
				TCPFillPeerName(sock, pcb, msg);
				return (int)got;
			}
			continue;
		}

		if (pcb->fin_recvd || pcb->state == TCP_STATE_CLOSE_WAIT ||
			pcb->state == TCP_STATE_CLOSED ||
			sock->sockState == SOCK_STATE_CONNECTION_RST) {
			TCPFillPeerName(sock, pcb, msg);
			return (int)got;
		}
		if (sock->nonblock || (flags & MSG_DONTWAIT)) {
			if (got)
				return (int)got;
			return -AU_ERR_EAGAIN;
		}
		if (AuGetCurrentUS() >= deadline) {
			if (got)
				return (int)got;
			return -AU_ERR_ETIMEDOUT;
		}
		AuNetRxPoll();
		if (TCPRxUsed(pcb))
			continue;
		AuSocketWaitSlice(&sock->rx_wait, AU_SOCKET_WAIT_SLICE_MS);
	}
}

int AuTCPSend(AuSocket* sock, msghdr* msg, int flags) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	uint64_t deadline;
	size_t sent = 0;
	size_t i;
	int timedout = 0;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (sock->shut_wr)
		return -AU_ERR_EPIPE;
	if (pcb->state != TCP_STATE_ESTABLISHED && pcb->state != TCP_STATE_CLOSE_WAIT)
		return -AU_ERR_EPIPE;
	if (!msg || msg->msg_iovlen == 0)
		return 0;

	deadline = AuGetCurrentUS() + (uint64_t)30000 * 1000ULL;

	for (i = 0; i < msg->msg_iovlen && !timedout; i++) {
		const uint8_t* base = (const uint8_t*)msg->msg_iov[i].iov_base;
		size_t len = msg->msg_iov[i].iov_len;
		size_t off = 0;

		if (!len || !base)
			continue;
		while (off < len) {
			uint32_t unacked = pcb->snd_nxt - pcb->snd_una;
			uint32_t win = pcb->snd_wnd > unacked ? pcb->snd_wnd - unacked : 0;
			size_t chunk;

			if (pcb->state != TCP_STATE_ESTABLISHED &&
				pcb->state != TCP_STATE_CLOSE_WAIT) {
				if (sent)
					return (int)sent;
				return -AU_ERR_EPIPE;
			}
			if (win == 0) {
				if (sock->nonblock || (flags & MSG_DONTWAIT)) {
					if (sent)
						return (int)sent;
					return -AU_ERR_EAGAIN;
				}
				if (AuGetCurrentUS() >= deadline) {
					timedout = 1;
					break;
				}
				AuNetRxPoll();
				{
					uint32_t unacked = pcb->snd_nxt - pcb->snd_una;
					if (pcb->snd_wnd > unacked)
						continue;
				}
				AuSocketWaitSlice(&sock->tx_wait, AU_SOCKET_WAIT_SLICE_MS);
				continue;
			}
			chunk = len - off;
			if (chunk > pcb->mss)
				chunk = pcb->mss;
			if (chunk > win)
				chunk = win;
			if (TCPSendSegment(sock, TCP_FLAGS_ACK | TCP_FLAGS_PSH,
				base + off, chunk) != 0) {
				if (sent)
					return (int)sent;
				return -AU_ERR_EAGAIN;
			}
			sent += chunk;
			off += chunk;
		}
	}
	AuNetRxPoll();
	if (sent == 0 && timedout)
		return -AU_ERR_ETIMEDOUT;
	return (int)sent;
}

void AuTCPClose(AuSocket* sock) {
	TCPControlBlock* pcb = TCPGetPCB(sock);

	if (!pcb)
		return;
	if (pcb->state == TCP_STATE_ESTABLISHED || pcb->state == TCP_STATE_SYN_RECEIVED) {
		pcb->state = TCP_STATE_FIN_WAIT_1;
		TCPSendSegment(sock, TCP_FLAGS_FIN | TCP_FLAGS_ACK, NULL, 0);
	} else if (pcb->state == TCP_STATE_CLOSE_WAIT) {
		pcb->state = TCP_STATE_LAST_ACK;
		TCPSendSegment(sock, TCP_FLAGS_FIN | TCP_FLAGS_ACK, NULL, 0);
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

/*
 * AuTCPShutdown -- half-close a connected TCP socket.
 *
 * SHUT_WR sends FIN exactly once but leaves the receive path alive, so a
 * request/response protocol can finish reading the peer's reply after it
 * has stopped writing. SHUT_RD turns receive into an immediate EOF and
 * discards nothing already queued in the wire; a later close still sends
 * the normal FIN/RST.
 */
static int AuTCPShutdown(AuSocket* sock, int how) {
	TCPControlBlock* pcb = TCPGetPCB(sock);

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
		return -AU_ERR_EINVAL;
	if (pcb->state == TCP_STATE_CLOSED || pcb->state == TCP_STATE_LISTEN ||
		pcb->state == TCP_STATE_SYN_SENT || pcb->state == TCP_STATE_SYN_RECEIVED)
		return -AU_ERR_ENOTCONN;

	if (how == SHUT_RD || how == SHUT_RDWR) {
		sock->shut_rd = 1;
		AuSocketWakeRx(sock);
	}
	if ((how == SHUT_WR || how == SHUT_RDWR) && !sock->shut_wr) {
		sock->shut_wr = 1;
		if (pcb->state == TCP_STATE_ESTABLISHED) {
			pcb->state = TCP_STATE_FIN_WAIT_1;
			TCPSendSegment(sock, TCP_FLAGS_FIN | TCP_FLAGS_ACK, NULL, 0);
		} else if (pcb->state == TCP_STATE_CLOSE_WAIT) {
			pcb->state = TCP_STATE_LAST_ACK;
			TCPSendSegment(sock, TCP_FLAGS_FIN | TCP_FLAGS_ACK, NULL, 0);
		}
		/* FIN_WAIT_1/2, CLOSING and TIME_WAIT already sent their FIN. */
	}
	return 0;
}

/* Userspace compares connect() against this. XEClib's EINPROGRESS is 119. */
#define AU_TCP_EINPROGRESS 119

/* One poll of a handshake that was started with sock->nonblock. */
static int AuTCPConnectPoll(AuSocket* sock, TCPControlBlock* pcb) {
	uint64_t ns, nss;

	AuNetRxPoll();
	if (sock->sockState == SOCK_STATE_CONNECTION_RST ||
		pcb->state == TCP_STATE_CLOSED) {
		if (!sock->so_error)
			sock->so_error = AU_ERR_ECONNREFUSED;
		return -sock->so_error;
	}
	if (pcb->state == TCP_STATE_ESTABLISHED) {
		sock->sockState = SOCK_STATE_CONNECTED;
		sock->so_error = 0;
		return 0;
	}
	if (pcb->state != TCP_STATE_SYN_SENT) {
		if (!sock->so_error)
			sock->so_error = AU_ERR_ECONNREFUSED;
		return -sock->so_error;
	}
	aa64_calculate_ticks(0, 0, &ns, &nss);
	if (ns > pcb->syn_sec || (ns == pcb->syn_sec && nss > pcb->syn_sub)) {
		if (pcb->syn_tries >= 3) {
			pcb->state = TCP_STATE_CLOSED;
			sock->so_error = AU_ERR_ETIMEDOUT;
			return -AU_ERR_ETIMEDOUT;
		}
		pcb->snd_nxt = pcb->iss;
		pcb->syn_tries++;
		/* A missed ARP entry keeps the SYN queued; retry on the next
		 * poll and let the caller decide how long to wait. */
		if (TCPSendSegment(sock, TCP_FLAGS_SYN, NULL, 0) != 0) {
			aa64_calculate_ticks(1, 0, &pcb->syn_sec, &pcb->syn_sub);
			return -AU_ERR_EINPROGRESS;
		}
		aa64_calculate_ticks(1, 0, &pcb->syn_sec, &pcb->syn_sub);
	}
	return -AU_ERR_EINPROGRESS;
}

int AuTCPConnect(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	AuVFSNode* nic;
	AuNetworkDevice* ndev;
	uint64_t deadline;
	int attempts = 0;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (!addr || addrlen < sizeof(sockaddr))
		return -AU_ERR_EINVAL;

	if (pcb->state == TCP_STATE_LISTEN)
		return -AU_ERR_EINVAL;
	if (pcb->state == TCP_STATE_ESTABLISHED) {
		sock->sockState = SOCK_STATE_CONNECTED;
		sock->so_error = 0;
		return -AU_ERR_EISCONN;
	}
	/* A second connect() on a non-blocking socket only checks the handshake.
	 * Falling through would mint a new sequence number and send another SYN. */
	if (pcb->state == TCP_STATE_SYN_SENT && sock->nonblock)
		return AuTCPConnectPoll(sock, pcb);

	if (addr->sa_family == AF_INET6) {
		sockaddr_in6* sockdata6 = (sockaddr_in6*)addr;
		if (addrlen < sizeof(sockaddr_in6))
			return -AU_ERR_EINVAL;
		pcb->is_ipv6 = 1;
		sock->family = AF_INET6;
		nic = AuNetworkRoute6((const ip6_addr*)&sockdata6->sin6_addr);
		if (!nic)
			return -AU_ERR_ENETUNREACH;
		ndev = (AuNetworkDevice*)nic->device;
		if (!ndev)
			return -AU_ERR_ENETUNREACH;
		pcb->nic = nic;
		memcpy(pcb->remote_ip6.s6_addr, sockdata6->sin6_addr.s6_addr, 16);
		pcb->remote_port = ntohs(sockdata6->sin6_port);
	} else if (addr->sa_family == AF_INET) {
		sockaddr_in* sockdata = (sockaddr_in*)addr;
		if (addrlen < sizeof(sockaddr_in))
			return -AU_ERR_EINVAL;
		pcb->is_ipv6 = 0;
		sock->family = AF_INET;
		nic = AuNetworkRoute(sockdata->sin_addr.s_addr);
		if (!nic)
			return -AU_ERR_ENETUNREACH;
		ndev = (AuNetworkDevice*)nic->device;
		if (!ndev)
			return -AU_ERR_ENETUNREACH;
		pcb->nic = nic;
		pcb->remote_ip = sockdata->sin_addr.s_addr;
		pcb->remote_port = ntohs(sockdata->sin_port);
		/* Connecting a bound, non-listening local port to itself would
		 * otherwise match our own SYN in TCPFindSocket and self-open.
		 * Linux refuses this path with ECONNREFUSED. */
		if (AuAddrIsLocal4(pcb->remote_ip) &&
			TCPLocalPortRefused(pcb->remote_ip, pcb->remote_port))
			return -AU_ERR_ECONNREFUSED;
	} else {
		return -AU_ERR_EAFNOSUPPORT;
	}

	/* Loopback can carry a much larger segment than Ethernet. The peer's
	 * SYN/SYN-ACK may clamp this down further once it arrives. */
	pcb->rcv_mss = TCPPathMSS(nic, pcb->is_ipv6, pcb->remote_ip,
		pcb->is_ipv6 ? &pcb->remote_ip6 : NULL);
	pcb->mss = pcb->rcv_mss;

	if (sock->sessionPort == 0)
		AuTCPObtainPort(sock);
	else
		TCPRegisterSocket(sock);

	if (!sock->local_len) {
		if (pcb->is_ipv6) {
			sockaddr_in6 local6;

			memset(&local6, 0, sizeof(local6));
			local6.sin6_family = AF_INET6;
			local6.sin6_port = htons(sock->sessionPort);
			if (ndev)
				memcpy(local6.sin6_addr.s6_addr,
					ndev->ipv6addr.s6_addr, 16);
			TCPSaveLocal(sock, (sockaddr*)&local6, sizeof(local6));
		} else {
			sockaddr_in local4;

			memset(&local4, 0, sizeof(local4));
			local4.sin_family = AF_INET;
			local4.sin_port = htons(sock->sessionPort);
			local4.sin_addr.s_addr = ndev ? ndev->ipv4addr : 0;
			TCPSaveLocal(sock, (sockaddr*)&local4, sizeof(local4));
		}
	}

	pcb->iss = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
	if (pcb->iss == 0)
		pcb->iss = 1;
	pcb->snd_una = pcb->iss;
	pcb->snd_nxt = pcb->iss;
	pcb->rcv_nxt = 0;
	pcb->syn_tries = 0;
	pcb->fin_recvd = 0;
	/* Offer RFC 7323 scaling in our SYN. The connection only enables it
	 * after the peer's SYN-ACK carries the option too. */
	pcb->ws_offer = 1;
	pcb->ws_enabled = 0;
	pcb->snd_wscale = 0;
	sock->ipv4Iden = (uint16_t)rand();
	sock->sockState = SOCK_STATE_WAITING_FOR_CONNECTION;
	sock->so_error = 0;
	pcb->state = TCP_STATE_SYN_SENT;

	/* A miss here means the gateway ARP is not in yet. Stay in SYN_SENT
	 * and let the wait below send the SYN again instead of failing the
	 * first connect of the boot. */
	TCPSendSegment(sock, TCP_FLAGS_SYN, NULL, 0);
	aa64_calculate_ticks(1, 0, &pcb->syn_sec, &pcb->syn_sub);

	if (sock->nonblock)
		return -AU_ERR_EINPROGRESS;

	deadline = AuGetCurrentUS() + (uint64_t)30000 * 1000ULL;
	while (pcb->state == TCP_STATE_SYN_SENT) {
		uint64_t ns, nss;
		uint64_t s, ss;

		AuNetRxPoll();
		if (pcb->state != TCP_STATE_SYN_SENT)
			continue;
		AuSocketWaitSlice(&sock->tx_wait, AU_SOCKET_WAIT_SLICE_MS);

		if (sock->sockState == SOCK_STATE_CONNECTION_RST ||
			pcb->state == TCP_STATE_CLOSED) {
			if (!sock->so_error)
				sock->so_error = AU_ERR_ECONNREFUSED;
			return -sock->so_error;
		}
		if (AuGetCurrentUS() >= deadline || attempts >= 4) {
			pcb->state = TCP_STATE_CLOSED;
			sock->so_error = AU_ERR_ETIMEDOUT;
			return -AU_ERR_ETIMEDOUT;
		}
		aa64_calculate_ticks(0, 0, &ns, &nss);
		aa64_calculate_ticks(1, 0, &s, &ss);
		if (ns > pcb->syn_sec || (ns == pcb->syn_sec && nss > pcb->syn_sub)) {
			pcb->snd_nxt = pcb->iss;
			TCPSendSegment(sock, TCP_FLAGS_SYN, NULL, 0);
			pcb->syn_sec = s;
			pcb->syn_sub = ss;
			attempts++;
		}
	}

	if (pcb->state != TCP_STATE_ESTABLISHED) {
		if (!sock->so_error)
			sock->so_error = AU_ERR_ECONNREFUSED;
		return -sock->so_error;
	}
	sock->sockState = SOCK_STATE_CONNECTED;
	sock->so_error = 0;
	return 0;
}

int AuTCPBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	int port = 0;
	int i;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (!addr || addrlen < sizeof(sockaddr))
		return -AU_ERR_EINVAL;
	if (sock->sessionPort != 0)
		return -AU_ERR_EINVAL;

	if (addr->sa_family == AF_INET6) {
		sockaddr_in6* addr6 = (sockaddr_in6*)addr;
		if (addrlen < sizeof(sockaddr_in6))
			return -AU_ERR_EINVAL;
		pcb->is_ipv6 = 1;
		sock->family = AF_INET6;
		port = ntohs(addr6->sin6_port);
		{
			int any = 1;
			int i;
			for (i = 0; i < 16; i++) {
				if (addr6->sin6_addr.s6_addr[i]) {
					any = 0;
					break;
				}
			}
			if (!any)
				pcb->nic = AuNetworkRoute6(
					(const ip6_addr*)&addr6->sin6_addr);
		}
	} else if (addr->sa_family == AF_INET) {
		sockaddr_in* addr_in = (sockaddr_in*)addr;
		if (addrlen < sizeof(sockaddr_in))
			return -AU_ERR_EINVAL;
		pcb->is_ipv6 = 0;
		sock->family = AF_INET;
		port = ntohs(addr_in->sin_port);
		if (addr_in->sin_addr.s_addr)
			pcb->nic = AuNetworkRoute(addr_in->sin_addr.s_addr);
	} else {
		return -AU_ERR_EAFNOSUPPORT;
	}

	if (port == 0) {
		AuTCPObtainPort(sock);
		TCPSaveLocal(sock, addr, addrlen);
		return 0;
	}
	if (!tcpSocketList)
		return -AU_ERR_EMFILE;
	for (i = 0; i < (int)tcpSocketList->pointer; i++) {
		AuSocket* other = (AuSocket*)list_get_at(tcpSocketList, i);
		TCPControlBlock* otherpcb = TCPGetPCB(other);
		if (other && other->sessionPort == (uint16_t)port && otherpcb &&
			otherpcb->state == TCP_STATE_LISTEN) {
			if (!sock->reuseaddr || other != sock)
				return -AU_ERR_EADDRINUSE;
		}
	}
	sock->sessionPort = (uint16_t)port;
	TCPRegisterSocket(sock);
	TCPSaveLocal(sock, addr, addrlen);
	return 0;
}

int AuTCPListen(AuSocket* sock, int backlog) {
	TCPControlBlock* pcb = TCPGetPCB(sock);

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (sock->sessionPort == 0)
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
	pcb->state = TCP_STATE_LISTEN;
	sock->listening = 1;
	sock->sockState = SOCK_STATE_CONNECTED;
	return 0;
}

int AuTCPAccept(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	AuSocket* child = NULL;
	TCPControlBlock* childpcb;
	uint64_t deadline;
	int fd;

	if (!pcb || pcb->state != TCP_STATE_LISTEN || !pcb->acceptq)
		return -AU_ERR_EINVAL;

	if (pcb->acceptq->pointer == 0) {
		if (sock->nonblock)
			return -AU_ERR_EAGAIN;
		deadline = AuGetCurrentUS() + (uint64_t)30000 * 1000ULL;
		while (pcb->acceptq->pointer == 0) {
			if (AuGetCurrentUS() >= deadline)
				return -AU_ERR_ETIMEDOUT;
			AuNetRxPoll();
			if (pcb->acceptq->pointer)
				continue;
			AuSocketWaitSlice(&sock->accept_wait, AU_SOCKET_WAIT_SLICE_MS);
		}
	}

	child = (AuSocket*)list_remove(pcb->acceptq, 0);
	if (!child)
		return -AU_ERR_EAGAIN;
	childpcb = TCPGetPCB(child);
	if (childpcb)
		childpcb->parent = NULL;
	child->nonblock = 0;
	child->listening = 0;

	fd = AuSocketInstallFd(child, "tcp", AuTCPFileClose);
	if (fd < 0) {
		TCPFreeSocket(child);
		return fd;
	}

	if (addr && addrlen) {
		uint8_t raw[sizeof(sockaddr_in6)];
		socklen_t len = 0;

		memset(raw, 0, sizeof(raw));
		if (childpcb && childpcb->is_ipv6) {
			sockaddr_in6* in6 = (sockaddr_in6*)raw;
			in6->sin6_family = AF_INET6;
			in6->sin6_port = htons(childpcb->remote_port);
			memcpy(in6->sin6_addr.s6_addr, childpcb->remote_ip6.s6_addr,
				16);
			len = sizeof(sockaddr_in6);
		} else if (childpcb) {
			sockaddr_in* in = (sockaddr_in*)raw;
			in->sin_family = AF_INET;
			in->sin_port = htons(childpcb->remote_port);
			in->sin_addr.s_addr = childpcb->remote_ip;
			len = sizeof(sockaddr_in);
		}
		if (len)
			AuSocketCopyAddr(addr, addrlen, raw, len);
		else
			*addrlen = 0;
	}
	return fd;
}

static void TCPSaveLocal(AuSocket* sock, const sockaddr* addr,
	socklen_t addrlen) {
	socklen_t copy;

	if (!sock || !addr)
		return;
	memset(sock->local_addr, 0, sizeof(sock->local_addr));
	if (addr->sa_family == AF_INET) {
		copy = sizeof(sockaddr_in);
		if (addrlen < copy)
			copy = addrlen;
		memcpy(sock->local_addr, addr, copy);
		((sockaddr_in*)sock->local_addr)->sin_port =
			htons(sock->sessionPort);
		sock->local_len = sizeof(sockaddr_in);
	} else if (addr->sa_family == AF_INET6) {
		copy = sizeof(sockaddr_in6);
		if (addrlen < copy)
			copy = addrlen;
		memcpy(sock->local_addr, addr, copy);
		((sockaddr_in6*)sock->local_addr)->sin6_port =
			htons(sock->sessionPort);
		sock->local_len = sizeof(sockaddr_in6);
	} else {
		sock->local_len = 0;
	}
}

static int TCPWildcardAddr(const uint8_t* raw, socklen_t len) {
	if (len < sizeof(sockaddr))
		return 1;
	if (((const sockaddr*)raw)->sa_family == AF_INET) {
		const sockaddr_in* in = (const sockaddr_in*)raw;
		return in->sin_addr.s_addr == 0;
	}
	if (((const sockaddr*)raw)->sa_family == AF_INET6) {
		const sockaddr_in6* in6 = (const sockaddr_in6*)raw;
		int i;

		for (i = 0; i < 16; i++) {
			if (in6->sin6_addr.s6_addr[i])
				return 0;
		}
		return 1;
	}
	return 1;
}

static void TCPLocalIP(TCPControlBlock* pcb, uint32_t* ip4, ip6_addr* ip6) {
	AuNetworkDevice* ndev = NULL;

	if (pcb && pcb->nic)
		ndev = (AuNetworkDevice*)pcb->nic->device;
	if (ip4)
		*ip4 = ndev ? ndev->ipv4addr : 0;
	if (ip6) {
		memset(ip6, 0, sizeof(*ip6));
		if (ndev)
			ip6_addr_copy(ip6, &ndev->ipv6addr);
	}
}

static int AuTCPGetSockName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	uint8_t raw[sizeof(sockaddr_in6)];
	socklen_t len;
	int ipv6;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (sock->local_len >= sizeof(sockaddr))
		return AuSocketCopyAddr(addr, addrlen, sock->local_addr,
			sock->local_len);
	ipv6 = (sock->family == AF_INET6) || pcb->is_ipv6;
	memset(raw, 0, sizeof(raw));
	if (ipv6) {
		sockaddr_in6* in6 = (sockaddr_in6*)raw;
		ip6_addr local6;

		TCPLocalIP(pcb, NULL, &local6);
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons(sock->sessionPort);
		memcpy(in6->sin6_addr.s6_addr, local6.s6_addr, 16);
		len = sizeof(sockaddr_in6);
	} else {
		sockaddr_in* in = (sockaddr_in*)raw;
		uint32_t local4;

		TCPLocalIP(pcb, &local4, NULL);
		in->sin_family = AF_INET;
		in->sin_port = htons(sock->sessionPort);
		in->sin_addr.s_addr = local4;
		len = sizeof(sockaddr_in);
	}
	return AuSocketCopyAddr(addr, addrlen, raw, len);
}

static int AuTCPGetPeerName(AuSocket* sock, sockaddr* addr, socklen_t* addrlen) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	uint8_t raw[sizeof(sockaddr_in6)];
	socklen_t len;

	if (!pcb)
		return -AU_ERR_ENOTSOCK;
	if (pcb->state == TCP_STATE_LISTEN || pcb->state == TCP_STATE_CLOSED ||
		pcb->state == TCP_STATE_SYN_SENT || pcb->remote_port == 0)
		return -AU_ERR_ENOTCONN;
	memset(raw, 0, sizeof(raw));
	if (pcb->is_ipv6) {
		sockaddr_in6* in6 = (sockaddr_in6*)raw;
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons(pcb->remote_port);
		memcpy(in6->sin6_addr.s6_addr, pcb->remote_ip6.s6_addr, 16);
		len = sizeof(sockaddr_in6);
	} else {
		sockaddr_in* in = (sockaddr_in*)raw;
		in->sin_family = AF_INET;
		in->sin_port = htons(pcb->remote_port);
		in->sin_addr.s_addr = pcb->remote_ip;
		len = sizeof(sockaddr_in);
	}
	return AuSocketCopyAddr(addr, addrlen, raw, len);
}

static int AuTCPPoll(AuSocket* sock, int events) {
	TCPControlBlock* pcb = TCPGetPCB(sock);
	int mask = 0;

	(void)events;
	if (!pcb)
		return POLLNVAL;

	if (pcb->state == TCP_STATE_LISTEN) {
		if (pcb->acceptq && pcb->acceptq->pointer)
			mask |= POLLIN;
		return mask;
	}

	if (sock->so_error || sock->sockState == SOCK_STATE_CONNECTION_RST)
		mask |= POLLERR;

	if (pcb->state == TCP_STATE_SYN_SENT || pcb->state == TCP_STATE_SYN_RECEIVED) {
		if (pcb->state == TCP_STATE_SYN_RECEIVED)
			mask |= POLLOUT;
		return mask;
	}

	if (sock->shut_rd)
		mask |= POLLIN;
	if (TCPRxUsed(pcb))
		mask |= POLLIN;
	if (pcb->fin_recvd || pcb->state == TCP_STATE_CLOSE_WAIT ||
		pcb->state == TCP_STATE_CLOSED)
		mask |= POLLIN | POLLHUP;
	if (!sock->shut_wr &&
		(pcb->state == TCP_STATE_ESTABLISHED || pcb->state == TCP_STATE_CLOSE_WAIT)) {
		uint32_t unacked = pcb->snd_nxt - pcb->snd_una;
		if (pcb->snd_wnd > unacked)
			mask |= POLLOUT;
	}
	return mask;
}

static void TCPHandleListenSyn(AuSocket* listener, IPv4Header* pack,
	TCPHeader* tcp, AuVFSNode* nic, uint16_t peer_mss) {
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
	child->family = AF_INET;
	if (listener->local_len && !TCPWildcardAddr(listener->local_addr,
		listener->local_len)) {
		memcpy(child->local_addr, listener->local_addr,
			sizeof(child->local_addr));
		child->local_len = listener->local_len;
	}
	pcb->nic = nic;
	pcb->parent = listener;
	pcb->remote_ip = pack->srcAddress;
	pcb->remote_port = ntohs(tcp->srcPort);
	/* The child, not the listener, sends the SYN-ACK and carries the data.
	 * Size its MSS from the path the SYN arrived on, then clamp it to what
	 * the client is willing to receive. */
	pcb->rcv_mss = TCPPathMSS(nic, 0, pack->srcAddress, NULL);
	pcb->mss = pcb->rcv_mss;
	TCPApplyPeerMSS(pcb, peer_mss);
	{
		uint8_t peer_ws;

		if (TCPParseWS(tcp, TCPHdrBytes(tcp), &peer_ws)) {
			pcb->ws_offer = 1;
			pcb->ws_enabled = 1;
			pcb->snd_wscale = peer_ws;
		}
	}
	pcb->irs = seq;
	pcb->rcv_nxt = seq + 1;
	pcb->iss = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
	if (pcb->iss == 0)
		pcb->iss = 1;
	pcb->snd_una = pcb->iss;
	pcb->snd_nxt = pcb->iss;
	/* The window in the SYN itself is never scaled (RFC 7323). */
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
	int deferAck = 0;
	int dataReady = (payloadLen == 0);

	(void)pack;
	if (!pcb)
		return;
	seq = ntohl(tcp->sequenceNum);
	ack = ntohl(tcp->ackNum);

	if (flags & TCP_FLAGS_ACK) {
		if ((int32_t)(ack - pcb->snd_una) > 0 && (int32_t)(pcb->snd_nxt - ack) >= 0)
			pcb->snd_una = ack;
		pcb->snd_wnd = (uint32_t)ntohs(tcp->window) << pcb->snd_wscale;
		AuSocketWakeTx(sock);

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
		if (TCPRxFree(pcb) < payloadLen) {
			needAck = 1;
		} else {
			wrote = TCPWriteRx(pcb, payload, payloadLen);
			if (wrote > 0) {
				pcb->rcv_nxt += (uint32_t)wrote;
				dataReady = ((size_t)wrote == payloadLen);
				pcb->ack_pending++;
				/* Delayed ACK: hold the ACK until the current RX drain
				 * ends, then send one per drain. A drain usually covers a
				 * whole slirp burst, so this matches a NAPI batch and
				 * avoids a TX frame per two segments. Genuinely short
				 * segments still ACK immediately so an interactive peer
				 * never waits for a second segment. */
				if (payloadLen < pcb->rcv_mss / 2)
					needAck = 1;
				else if (pcb->ack_pending >= 2)
					deferAck = 1;
			}
		}
	}

	if ((flags & TCP_FLAGS_FIN) && dataReady &&
		seq + (uint32_t)payloadLen == pcb->rcv_nxt) {
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

	if (needAck)
		TCPSendAck(sock);
	else if (deferAck)
		TCPDeferAck(sock);
	if (payloadLen || (flags & TCP_FLAGS_FIN))
		AuSocketWakeRx(sock);
}

void TCPHandlePacket(IPv4Header* pack, AuVFSNode* nic) {
	int ihl;
	int doff;
	uint16_t tot;
	uint16_t destPort;
	uint16_t srcPort;
	uint16_t flags;
	uint16_t peer_mss = 0;
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
	if (flags & TCP_FLAGS_SYN)
		peer_mss = TCPParseMSS(tcp, doff);
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

	if (pcb->state == TCP_STATE_LISTEN) {
		if (flags & TCP_FLAGS_SYN)
			TCPHandleListenSyn(sock, pack, tcp, nic, peer_mss);
		return;
	}

	if (flags & TCP_FLAGS_RST) {
		sock->sockState = SOCK_STATE_CONNECTION_RST;
		sock->so_error = (pcb->state == TCP_STATE_SYN_SENT) ?
			AU_ERR_ECONNREFUSED : AU_ERR_ECONNRESET;
		pcb->state = TCP_STATE_CLOSED;
		AuSocketWakeRx(sock);
		AuSocketWakeTx(sock);
		AuSocketWakeAccept(sock);
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
			{
				uint8_t peer_ws;

				if (TCPParseWS(tcp, TCPHdrBytes(tcp), &peer_ws)) {
					pcb->snd_wscale = peer_ws;
					pcb->ws_enabled = 1;
				}
			}
			/* SYN-ACK window is unscaled; later ACKs are shifted. */
			pcb->snd_wnd = ntohs(tcp->window);
			TCPApplyPeerMSS(pcb, peer_mss);
			if (!pcb->nic)
				pcb->nic = nic;
			pcb->state = TCP_STATE_ESTABLISHED;
			sock->sockState = SOCK_STATE_CONNECTED;
			sock->so_error = 0;
			TCPSendAck(sock);
			AuSocketWakeTx(sock);
			return;
		}
		if (flags & TCP_FLAGS_SYN) {
			pcb->irs = ntohl(tcp->sequenceNum);
			pcb->rcv_nxt = pcb->irs + 1;
			TCPApplyPeerMSS(pcb, peer_mss);
			pcb->state = TCP_STATE_SYN_RECEIVED;
			pcb->snd_nxt = pcb->iss;
			TCPSendSegment(sock, TCP_FLAGS_SYN | TCP_FLAGS_ACK, NULL, 0);
			return;
		}
		return;
	}

	TCPHandleEstablished(sock, pack, tcp, payload, payloadLen, flags);
}

static void TCPHandleListenSyn6(AuSocket* listener, IPv6Header* pack,
	TCPHeader* tcp, AuVFSNode* nic, uint16_t peer_mss) {
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
	child->family = AF_INET6;
	if (listener->local_len && !TCPWildcardAddr(listener->local_addr,
		listener->local_len)) {
		memcpy(child->local_addr, listener->local_addr,
			sizeof(child->local_addr));
		child->local_len = listener->local_len;
	}
	pcb->nic = nic;
	pcb->parent = listener;
	pcb->is_ipv6 = 1;
	ip6_addr_copy(&pcb->remote_ip6, &pack->srcIP);
	pcb->remote_port = ntohs(tcp->srcPort);
	pcb->rcv_mss = TCPPathMSS(nic, 1, 0, &pack->srcIP);
	pcb->mss = pcb->rcv_mss;
	TCPApplyPeerMSS(pcb, peer_mss);
	{
		uint8_t peer_ws;

		if (TCPParseWS(tcp, TCPHdrBytes(tcp), &peer_ws)) {
			pcb->ws_offer = 1;
			pcb->ws_enabled = 1;
			pcb->snd_wscale = peer_ws;
		}
	}
	pcb->irs = seq;
	pcb->rcv_nxt = seq + 1;
	pcb->iss = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
	if (pcb->iss == 0)
		pcb->iss = 1;
	pcb->snd_una = pcb->iss;
	pcb->snd_nxt = pcb->iss;
	/* The window in the SYN itself is never scaled (RFC 7323). */
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
	uint16_t peer_mss = 0;
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
	if (flags & TCP_FLAGS_SYN)
		peer_mss = TCPParseMSS(tcp, doff);
	destPort = ntohs(tcp->destPort);
	srcPort = ntohs(tcp->srcPort);

	sock = TCPFindSocket6(&pack->srcIP, srcPort, destPort);
	if (!sock)
		return;
	pcb = TCPGetPCB(sock);
	if (!pcb)
		return;

	if (pcb->state == TCP_STATE_LISTEN) {
		if (flags & TCP_FLAGS_SYN)
			TCPHandleListenSyn6(sock, pack, tcp, nic, peer_mss);
		return;
	}

	if (flags & TCP_FLAGS_RST) {
		sock->sockState = SOCK_STATE_CONNECTION_RST;
		sock->so_error = (pcb->state == TCP_STATE_SYN_SENT) ?
			AU_ERR_ECONNREFUSED : AU_ERR_ECONNRESET;
		pcb->state = TCP_STATE_CLOSED;
		AuSocketWakeRx(sock);
		AuSocketWakeTx(sock);
		AuSocketWakeAccept(sock);
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
			{
				uint8_t peer_ws;

				if (TCPParseWS(tcp, TCPHdrBytes(tcp), &peer_ws)) {
					pcb->snd_wscale = peer_ws;
					pcb->ws_enabled = 1;
				}
			}
			/* SYN-ACK window is unscaled; later ACKs are shifted. */
			pcb->snd_wnd = ntohs(tcp->window);
			TCPApplyPeerMSS(pcb, peer_mss);
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

static void TCPFreeSocket(AuSocket* sock) {
	TCPControlBlock* pcb;

	if (!sock)
		return;
	pcb = TCPGetPCB(sock);
	TCPUnregisterSocket(sock);
	/* Children waiting in the accept queue are not reachable from any
	 * process yet; free them with the listener. */
	if (pcb && pcb->acceptq) {
		while (pcb->acceptq->pointer) {
			AuSocket* child = (AuSocket*)list_remove(pcb->acceptq, 0);
			if (child)
				TCPFreeSocket(child);
		}
	}
	if (sock->rxstack) {
		while (sock->rxstack->itemCount) {
			void* data = AuStackPop(sock->rxstack);
			if (data)
				kfree(data);
		}
		kfree(sock->rxstack);
		sock->rxstack = NULL;
	}
	if (pcb) {
		TCPFreePCB(pcb);
		sock->proto = NULL;
	}
	kfree(sock);
}

int AuTCPFileClose(AuVFSNode* fsys, AuVFSNode* file) {
	AuSocket* sock;

	(void)fsys;
	if (!file)
		return 0;
	sock = (AuSocket*)file->device;
	if (sock) {
		AuTCPClose(sock);
		TCPFreeSocket(sock);
	}
	kfree(file);
	return 0;
}

int CreateTCPSocket() {
	AuSocket* sock;
	int fd;

	sock = TCPAllocSocket();
	if (!sock)
		return -AU_ERR_EMFILE;
	fd = AuSocketInstallFd(sock, "tcp", AuTCPFileClose);
	if (fd < 0) {
		TCPFreeSocket(sock);
		return fd;
	}
	return fd;
}

list_t* TCPGetSocketList() {
	return tcpSocketList;
}

void TCPProtocolInstall() {
	tcpSocketList = initialize_list();
	AU_NET_LOG("[aurora]: TCP protocol installed\r\n");
}
