/**
* @file ipv4.c
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

#include <Net/socket.h>
#include <Net/tcp.h>
#include <Net/icmp.h>
#include <Net/ipv4.h>
#include <Net/udp.h>
#include <Net/aunet.h>
#include <Net/route.h>
#include <Net/ethernet.h>
#include <Net/arp.h>
#include <Net/udp.h>
#include <Net/packet.h>
#include <Net/netfilter.h>
#include <_null.h>
#include <aucon.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>

/* Per-socket creation messages sit on the socket() hot path and flood the
 * UART during benchmarks. Define NET_DEBUG to re-enable them. */
#ifdef NET_DEBUG
#define NET_SOCKET_DEBUG(...) AU_NET_LOG(__VA_ARGS__)
#else
#define NET_SOCKET_DEBUG(...) do { } while (0)
#endif
#include <string.h>
#include <Hal/AA64/sched.h>

uint16_t IPv4CalculateChecksum(IPv4Header* p) {
	uint32_t sum = 0;
	uint16_t* s = (uint16_t*)p;
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}
	return ~(sum & 0xFFFF) & 0xFFFF;
}

void ip_ntoa(const uint32_t src) {
	AU_NET_LOG("%d.%d.%d.",
				 ((src & 0xFF000000) >> 24),
				 ((src & 0xFF0000) >> 16),
				 ((src & 0xFF00) >> 8),
				 ((src & 0xFF)));
	AU_NET_LOG("%d \r\n", (src & 0xFF));
}

/*
 * IPv4HandlePacket -- handle incoming IPv4 packet
 * @param data -- Payload from Phy layer
 * @param nic -- Pointer to NIC card
 */
void IPv4HandlePacket(void* data, AuVFSNode* nic) {
	IPv4Header* pack = (IPv4Header*)data;
	uint8_t protocol;
	AuPacket pkt;
	int local;

	if (!pack || !nic)
		return;

	AuPacketInitIpv4(&pkt, pack, ntohs(pack->totalLength), nic, NULL);
	/* Broadcast/multicast are delivered locally (DHCP before addr set). */
	local = AuAddrIsLocal4(pack->destAddress) ||
		pack->destAddress == 0xFFFFFFFFu ||
		((pack->destAddress & MAKE_IP(240, 0, 0, 0)) == MAKE_IP(224, 0, 0, 0));
	{
		AuNetworkDevice* ndev = (AuNetworkDevice*)nic->device;
		if (ndev && ndev->ipv4addr && pack->destAddress == ndev->ipv4addr)
			local = 1;
	}

	/* Wire RX: PREROUTING. Loopback reinject skips L2 PREROUTING. */
	if (AuPacketGetOrigin() != AU_PKT_ORIGIN_LOCAL) {
		if (AuNetfilterHook(NF_PRE_ROUTING, &pkt) == NF_DROP)
			return;
	}

	if (local) {
		pkt.in_dev = nic;
		if (AuNetfilterHook(NF_LOCAL_IN, &pkt) == NF_DROP)
			return;
	} else {
		/* Host stack: FORWARD exists but default policy DROPs. */
		if (AuNetfilterHook(NF_FORWARD, &pkt) == NF_DROP)
			return;
		return;
	}

	memcpy(&protocol, &pack->protocol, 1);
	switch (protocol) {
	case 1: {
		AuICMPHandle(pack, nic);
		break;
	}
	case IPV4_PROTOCOL_UDP: {
		UDPHandlePacket(pack);
		break;
	}
	case IPV4_PROTOCOL_TCP: {
		TCPHandlePacket(pack, nic);
		break;
	}
	}
}

/*
 * CreateIPv4Socket -- create a new ipv4 socket
 * @param type -- type of the socket its Datagram or
 * stream socket
 * @param protocol -- protocol number
 */
int CreateIPv4Socket(int type, int protocol) {
	switch (type) {
	case SOCK_DGRAM:
		if (protocol == 0 || protocol == IPPROTOCOL_UDP) {
			NET_SOCKET_DEBUG("[aurora]: ipv4 udp protocol created \r\n");
			return CreateUDPSocket();
		}
		if (protocol == IPPROTOCOL_ICMP) {
			NET_SOCKET_DEBUG("[aurora]: ipv4 icmp protocol created \r\n");
			return CreateICMPSocket();
		}
		return -1;
	case SOCK_STREAM:
		if (protocol == 0 || protocol == IPPROTOCOL_TCP) {
			NET_SOCKET_DEBUG("[aurora]: tcp protocol created \r\n");
			return CreateTCPSocket();
		}
		return -1;
	default:
		return -1;
	}
}

/**
 * @brief IPV4SendPacket -- sends a packet to next stage
 * @param packet -- IPv4 packet to send
 * @param nic -- Pointer to NIC device
 */
void IPV4SendPacket(IPv4Header* packet, AuVFSNode* nic) {
	AuNetworkDevice* ndev;
	AuVFSNode* deliver;
	uint32_t ip_dest;
	AuRouteResult rr;
	AuARPCache* cache;

	if (!packet || !nic)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (!ndev)
		return;

	ip_dest = packet->destAddress;

	{
		AuPacket pkt;
		AuVFSNode* out = nic;

		if (ndev->type == NETDEV_TYPE_LOOPBACK || AuAddrIsLocal4(ip_dest)) {
			out = AuGetNetworkAdapter("lo");
			if (!out)
				out = nic;
		}
		AuPacketInitIpv4(&pkt, packet, ntohs(packet->totalLength), NULL, out);
		if (AuNetfilterHook(NF_LOCAL_OUT, &pkt) == NF_DROP)
			return;
		if (AuNetfilterHook(NF_POST_ROUTING, &pkt) == NF_DROP)
			return;
	}

	/* Loopback / local delivery: reinject at IP (never AuEthernetSend). */
	if (ndev->type == NETDEV_TYPE_LOOPBACK || AuAddrIsLocal4(ip_dest)) {
		deliver = AuGetNetworkAdapter("lo");
		if (!deliver)
			deliver = nic;
		if (!AuPacketLocalEnter())
			return;
		IPv4HandlePacket(packet, deliver);
		AuPacketLocalLeave();
		return;
	}

	/* FIB next-hop (RFC 1812 §5.2.4): gateway routes ARP the gateway. */
	if (AuRouteLookup4(ip_dest, &rr) == 0 && (rr.flags & RTF_GATEWAY) && rr.nexthop)
		ip_dest = rr.nexthop;

	/* 802.11 station presents Ethernet frames; the driver translates them. */
	if (ndev->type == NETDEV_TYPE_ETHERNET || ndev->type == NETDEV_TYPE_802_11) {
		cache = AuARPResolve(nic, ip_dest);
		if (!cache)
			return;
		AuEthernetSend(nic,
					   packet,
					   ntohs(packet->totalLength),
					   ETHERNET_TYPE_IPV4,
					   cache->hw_address);
	}
}
