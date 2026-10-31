/**
* @file ndp.c
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

#include <Net/ndp.h>
#include <Net/ethernet.h>
#include <Net/aunet.h>
#include <Net/ipv6.h>
#include <Net/icmpv6.h>
#include <string.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>
#include <list.h>
#include <Hal/AA64/sched.h>

list_t* nd_list;

void NDProtocolInitialise() {
	nd_list = initialize_list();
}

void NDProtocolAdd(AuVFSNode* nic, const ip6_addr* address, uint8_t* hwaddr) {
	AuNDCache* cache;

	if (!address || !hwaddr)
		return;
	if (AuNDGet(address))
		return;

	cache = (AuNDCache*)kmalloc(sizeof(AuNDCache));
	if (!cache)
		return;
	memset(cache, 0, sizeof(AuNDCache));
	memcpy(cache->hw_address, hwaddr, 6);
	ip6_addr_copy(&cache->ipAddress, address);
	cache->nic = nic;
	list_add(nd_list, cache);
}

AuNDCache* AuNDGet(const ip6_addr* address) {
	int i;

	if (!address || !nd_list)
		return NULL;
	for (i = 0; i < nd_list->pointer; i++) {
		AuNDCache* nd = (AuNDCache*)list_get_at(nd_list, i);
		if (nd && ip6_addr_equal(&nd->ipAddress, address))
			return nd;
	}
	return NULL;
}

AuNDCache* AuNDResolve(AuVFSNode* nic, const ip6_addr* addr) {
	AuNDCache* cache;
	int n;

	cache = AuNDGet(addr);
	if (cache)
		return cache;
	if (!nic || !addr)
		return NULL;
	AuNDRequestMAC(nic, addr);
	for (n = 0; n < 50; n++) {
		AuNetRxPoll();
		cache = AuNDGet(addr);
		if (cache)
			return cache;
		AuSleepThread(AuGetCurrentThread(), 1);
		AuScheduleNext();
	}
	return NULL;
}

void AuNDRequestMAC(AuVFSNode* nic, const ip6_addr* addr) {
	AuNetworkDevice* ndev;
	size_t totalLen;
	IPv6Header* ipv6;
	NDNeighborSolicit* ns;
	NDOptLinkLayer* opt;
	ip6_addr sn_mcast;
	uint8_t dest_mac[6];
	uint16_t icmpLen;

	if (!nic || !addr)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (!ndev || ip6_addr_is_zero(&ndev->ipv6addr))
		return;

	icmpLen = (uint16_t)(sizeof(NDNeighborSolicit) + sizeof(NDOptLinkLayer));
	totalLen = sizeof(IPv6Header) + icmpLen;
	ipv6 = (IPv6Header*)kmalloc(totalLen);
	if (!ipv6)
		return;
	memset(ipv6, 0, totalLen);

	IPv6SetVerTcFl(ipv6, 6, 0, 0);
	ipv6->payloadLen = htons(icmpLen);
	ipv6->nextHeader = IPV6_NEXT_ICMPV6;
	ipv6->hopLimit = 255;
	ip6_addr_copy(&ipv6->srcIP, &ndev->ipv6addr);
	ip6_solicited_node_addr(addr, &sn_mcast);
	ip6_addr_copy(&ipv6->destIP, &sn_mcast);

	ns = (NDNeighborSolicit*)&ipv6->payload;
	ns->type = ICMPV6_ND_NS;
	ns->code = 0;
	ns->checksum = 0;
	ns->reserved = 0;
	ip6_addr_copy(&ns->target, addr);

	opt = (NDOptLinkLayer*)ns->options;
	opt->type = NDP_OPT_SOURCE_LINK;
	opt->length = 1;
	memcpy(opt->mac, ndev->mac, 6);

	ns->checksum = htons(IPv6PseudoChecksum(&ipv6->srcIP, &ipv6->destIP,
		icmpLen, IPV6_NEXT_ICMPV6, ns, icmpLen));

	ip6_solicited_node_mac(addr, dest_mac);
	AuEthernetSend(nic, ipv6, totalLen, ETHERNET_TYPE_IPV6, dest_mac);
	kfree(ipv6);
}

void NDHandleNeighborSolicit(IPv6Header* ipv6, AuVFSNode* nic) {
	AuNetworkDevice* ndev;
	NDNeighborSolicit* ns;
	size_t totalLen;
	IPv6Header* resp;
	NDNeighborAdvert* na;
	NDOptLinkLayer* opt;
	uint16_t icmpLen;
	uint16_t payloadLen;

	if (!ipv6 || !nic)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (!ndev || ip6_addr_is_zero(&ndev->ipv6addr))
		return;

	payloadLen = ntohs(ipv6->payloadLen);
	if (payloadLen < sizeof(NDNeighborSolicit))
		return;

	ns = (NDNeighborSolicit*)&ipv6->payload;
	if (!ip6_addr_equal(&ns->target, &ndev->ipv6addr))
		return;

	/* Learn requester's MAC from source link-layer option if present */
	if (payloadLen >= sizeof(NDNeighborSolicit) + sizeof(NDOptLinkLayer)) {
		opt = (NDOptLinkLayer*)ns->options;
		if (opt->type == NDP_OPT_SOURCE_LINK && opt->length == 1)
			NDProtocolAdd(nic, &ipv6->srcIP, opt->mac);
	}

	icmpLen = (uint16_t)(sizeof(NDNeighborAdvert) + sizeof(NDOptLinkLayer));
	totalLen = sizeof(IPv6Header) + icmpLen;
	resp = (IPv6Header*)kmalloc(totalLen);
	if (!resp)
		return;
	memset(resp, 0, totalLen);

	IPv6SetVerTcFl(resp, 6, 0, 0);
	resp->payloadLen = htons(icmpLen);
	resp->nextHeader = IPV6_NEXT_ICMPV6;
	resp->hopLimit = 255;
	ip6_addr_copy(&resp->srcIP, &ndev->ipv6addr);
	ip6_addr_copy(&resp->destIP, &ipv6->srcIP);

	na = (NDNeighborAdvert*)&resp->payload;
	na->type = ICMPV6_ND_NA;
	na->code = 0;
	na->checksum = 0;
	/* Solicited + Override */
	na->flags = htonl(0x60000000);
	ip6_addr_copy(&na->target, &ndev->ipv6addr);

	opt = (NDOptLinkLayer*)na->options;
	opt->type = NDP_OPT_TARGET_LINK;
	opt->length = 1;
	memcpy(opt->mac, ndev->mac, 6);

	na->checksum = htons(IPv6PseudoChecksum(&resp->srcIP, &resp->destIP,
		icmpLen, IPV6_NEXT_ICMPV6, na, icmpLen));

	IPV6SendPacket(resp, nic);
	kfree(resp);
}

void NDHandleNeighborAdvert(IPv6Header* ipv6, AuVFSNode* nic) {
	NDNeighborAdvert* na;
	uint8_t* optp;
	uint16_t payloadLen;
	uint16_t remain;

	if (!ipv6 || !nic)
		return;

	payloadLen = ntohs(ipv6->payloadLen);
	if (payloadLen < sizeof(NDNeighborAdvert))
		return;

	na = (NDNeighborAdvert*)&ipv6->payload;
	optp = (uint8_t*)na->options;
	remain = (uint16_t)(payloadLen - sizeof(NDNeighborAdvert));
	while (remain >= 8) {
		NDOptLinkLayer* opt = (NDOptLinkLayer*)optp;
		uint16_t olen;

		if (!opt->length)
			break;
		olen = (uint16_t)(opt->length * 8);
		if (olen > remain)
			break;
		if (opt->type == NDP_OPT_TARGET_LINK && opt->length == 1)
			NDProtocolAdd(nic, &na->target, opt->mac);
		optp += olen;
		remain = (uint16_t)(remain - olen);
	}
}
