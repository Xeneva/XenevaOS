/**
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
* Note: Full IPv6 (NDP/ICMPv6/UDP6/TCP6) lives in KernelAA64/Net/.
* This x86 twin keeps EtherType dispatch and shared helpers compiling.
**/

#include <Net/socket.h>
#include <Net/ipv6.h>
#include <Net/aunet.h>
#include <Net/ethernet.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <Hal/serial.h>
#include <string.h>
#include <Hal/x86_64_cpu.h>

uint16_t IPv6PseudoChecksum(const ip6_addr* src, const ip6_addr* dst,
	uint32_t length, uint8_t nextHeader, const void* data, size_t dataLen) {
	uint32_t sum = 0;
	const uint16_t* s;
	uint32_t len_be;
	size_t i;
	size_t words;

	s = (const uint16_t*)src->s6_addr;
	for (i = 0; i < 8; i++) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
	}
	s = (const uint16_t*)dst->s6_addr;
	for (i = 0; i < 8; i++) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
	}

	len_be = htonl(length);
	s = (const uint16_t*)&len_be;
	sum += ntohs(s[0]);
	if (sum > 0xFFFF)
		sum = (sum >> 16) + (sum & 0xFFFF);
	sum += ntohs(s[1]);
	if (sum > 0xFFFF)
		sum = (sum >> 16) + (sum & 0xFFFF);

	{
		uint32_t nh_word = htonl((uint32_t)nextHeader);
		s = (const uint16_t*)&nh_word;
		sum += ntohs(s[0]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
		sum += ntohs(s[1]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
	}

	words = dataLen / 2;
	s = (const uint16_t*)data;
	for (i = 0; i < words; i++) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
	}
	if (dataLen & 1) {
		uint8_t tmp[2];
		const uint8_t* t = (const uint8_t*)data;
		tmp[0] = t[dataLen - 1];
		tmp[1] = 0;
		sum += ntohs(*(uint16_t*)tmp);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
	}

	return (uint16_t)(~(sum & 0xFFFF) & 0xFFFF);
}

void IPv6HandlePacket(void* data, AuVFSNode* nic) {
	IPv6Header* ipv6 = (IPv6Header*)data;
	(void)nic;
	if (!ipv6 || IPv6GetVersion(ipv6) != 6)
		return;
	/* Full demux is implemented on KernelAA64; keep RX path safe on x86. */
	SeTextOut("IPv6 packet next=%d\r\n", ipv6->nextHeader);
}

int CreateIPv6Socket(int type, int protocol) {
	(void)type;
	(void)protocol;
	return -1;
}

void IPV6SendPacket(IPv6Header* packet, AuVFSNode* nic) {
	AuNetworkDevice* ndev;
	uint8_t broadcast_addr[6];
	size_t totalLen;

	if (!packet || !nic)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (!ndev || ndev->type != NETDEV_TYPE_ETHERNET)
		return;
	memset(broadcast_addr, 0xFF, 6);
	totalLen = sizeof(IPv6Header) + ntohs(packet->payloadLen);
	AuEthernetSend(nic, packet, totalLen, ETHERNET_TYPE_IPV6, broadcast_addr);
}
