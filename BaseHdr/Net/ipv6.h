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
**/

#ifndef __IPV6_H__
#define __IPV6_H__

#include <stdint.h>
#include <stddef.h>
#include <Fs/vfs.h>

#define IPV6_NEXT_HOPOPT  0
#define IPV6_NEXT_TCP     6
#define IPV6_NEXT_UDP     17
#define IPV6_NEXT_ICMPV6  58

#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(push,1)
#endif
typedef struct _ip6_addr_ {
	uint8_t s6_addr[16];
} ip6_addr;

typedef struct _ipv6_head_ {
	uint32_t ver_tc_fl;   /* version:4 | tc:8 | flow:20 (network byte order) */
	uint16_t payloadLen;
	uint8_t  nextHeader;
	uint8_t  hopLimit;
	ip6_addr srcIP;
	ip6_addr destIP;
	uint8_t  payload[];
} IPv6Header;
#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(pop)
#endif

static inline void ip6_addr_copy(ip6_addr* dst, const ip6_addr* src) {
	int i;
	for (i = 0; i < 16; i++)
		dst->s6_addr[i] = src->s6_addr[i];
}

static inline int ip6_addr_equal(const ip6_addr* a, const ip6_addr* b) {
	int i;
	for (i = 0; i < 16; i++) {
		if (a->s6_addr[i] != b->s6_addr[i])
			return 0;
	}
	return 1;
}

static inline int ip6_addr_is_zero(const ip6_addr* a) {
	int i;
	for (i = 0; i < 16; i++) {
		if (a->s6_addr[i] != 0)
			return 0;
	}
	return 1;
}

static inline int ip6_is_multicast(const ip6_addr* a) {
	return a->s6_addr[0] == 0xFF;
}

static inline int ip6_is_linklocal(const ip6_addr* a) {
	return a->s6_addr[0] == 0xFE && (a->s6_addr[1] & 0xC0) == 0x80;
}

/* Solicited-node multicast MAC: 33:33:ff:XX:XX:XX from last 3 bytes of IPv6 */
static inline void ip6_solicited_node_mac(const ip6_addr* addr, uint8_t* mac) {
	mac[0] = 0x33;
	mac[1] = 0x33;
	mac[2] = 0xFF;
	mac[3] = addr->s6_addr[13];
	mac[4] = addr->s6_addr[14];
	mac[5] = addr->s6_addr[15];
}

/* Build solicited-node multicast address ff02::1:ffXX:XXXX */
static inline void ip6_solicited_node_addr(const ip6_addr* unicast, ip6_addr* out) {
	int i;
	for (i = 0; i < 16; i++)
		out->s6_addr[i] = 0;
	out->s6_addr[0] = 0xFF;
	out->s6_addr[1] = 0x02;
	out->s6_addr[11] = 0x01;
	out->s6_addr[12] = 0xFF;
	out->s6_addr[13] = unicast->s6_addr[13];
	out->s6_addr[14] = unicast->s6_addr[14];
	out->s6_addr[15] = unicast->s6_addr[15];
}

/*
 * Prefix match: compare first prefixLen bits of a and b.
 */
static inline int ip6_prefix_equal(const ip6_addr* a, const ip6_addr* b, uint8_t prefixLen) {
	int full;
	int rem;
	uint8_t mask;
	int i;

	if (prefixLen > 128)
		prefixLen = 128;
	full = prefixLen / 8;
	rem = prefixLen % 8;
	for (i = 0; i < full; i++) {
		if (a->s6_addr[i] != b->s6_addr[i])
			return 0;
	}
	if (rem) {
		mask = (uint8_t)(0xFF << (8 - rem));
		if ((a->s6_addr[full] & mask) != (b->s6_addr[full] & mask))
			return 0;
	}
	return 1;
}

/* Local endian helpers — avoid depending on aunet.h from this header */
static inline uint32_t ipv6_bswap32(uint32_t l) {
	return ((l & 0xFFu) << 24) | ((l & 0xFF00u) << 8) |
		((l & 0xFF0000u) >> 8) | ((l & 0xFF000000u) >> 24);
}

static inline uint8_t IPv6GetVersion(const IPv6Header* h) {
	return (uint8_t)((ipv6_bswap32(h->ver_tc_fl) >> 28) & 0xF);
}

static inline uint8_t IPv6GetTrafficClass(const IPv6Header* h) {
	return (uint8_t)((ipv6_bswap32(h->ver_tc_fl) >> 20) & 0xFF);
}

static inline uint32_t IPv6GetFlowLabel(const IPv6Header* h) {
	return ipv6_bswap32(h->ver_tc_fl) & 0xFFFFF;
}

static inline void IPv6SetVerTcFl(IPv6Header* h, uint8_t version, uint8_t tc, uint32_t fl) {
	uint32_t v = ((uint32_t)(version & 0xF) << 28) |
		((uint32_t)tc << 20) |
		(fl & 0xFFFFF);
	h->ver_tc_fl = ipv6_bswap32(v);
}

/*
 * IPv6 pseudo-header checksum for upper layers (UDP/TCP/ICMPv6).
 * @param src -- source IPv6
 * @param dst -- dest IPv6
 * @param length -- upper-layer length (host order)
 * @param nextHeader -- next header value
 * @param data -- upper-layer header + payload
 * @param dataLen -- length of data in bytes
 */
extern uint16_t IPv6PseudoChecksum(const ip6_addr* src, const ip6_addr* dst,
	uint32_t length, uint8_t nextHeader, const void* data, size_t dataLen);

/*
 * CreateIPv6Socket -- create a new ipv6 socket
 */
extern int CreateIPv6Socket(int type, int protocol);

/*
 * IPv6HandlePacket -- receive and decode ipv6 packet
 */
extern void IPv6HandlePacket(void* data, AuVFSNode* nic);

/*
 * IPV6SendPacket -- sends a packet to next stage
 */
extern void IPV6SendPacket(IPv6Header* packet, AuVFSNode* nic);

#endif
