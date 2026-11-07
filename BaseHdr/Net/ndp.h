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

#ifndef __NDP_H__
#define __NDP_H__

#include <stdint.h>
#include <Fs/vfs.h>
#include <Net/ipv6.h>

#define NDP_OPT_SOURCE_LINK 1
#define NDP_OPT_TARGET_LINK 2

#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(push,1)
#endif
typedef struct _nd_neighbor_solicit_ {
	uint8_t type;       /* 135 */
	uint8_t code;
	uint16_t checksum;
	uint32_t reserved;
	ip6_addr target;
	uint8_t options[];
} NDNeighborSolicit;

typedef struct _nd_neighbor_advert_ {
	uint8_t type;       /* 136 */
	uint8_t code;
	uint16_t checksum;
	uint32_t flags;     /* R/S/O in high bits */
	ip6_addr target;
	uint8_t options[];
} NDNeighborAdvert;

typedef struct _nd_opt_link_ {
	uint8_t type;
	uint8_t length;     /* units of 8 octets */
	uint8_t mac[6];
} NDOptLinkLayer;
#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(pop)
#endif

typedef struct _nd_cache_ {
	uint8_t hw_address[6];
	uint16_t flags;
	ip6_addr ipAddress;
	AuVFSNode* nic;
} AuNDCache;

extern void NDProtocolInitialise();
extern void NDProtocolAdd(AuVFSNode* nic, const ip6_addr* address, uint8_t* hwaddr);
extern AuNDCache* AuNDGet(const ip6_addr* address);
extern void AuNDRequestMAC(AuVFSNode* nic, const ip6_addr* addr);
extern AuNDCache* AuNDResolve(AuVFSNode* nic, const ip6_addr* addr);
extern void NDHandleNeighborSolicit(IPv6Header* ipv6, AuVFSNode* nic);
extern void NDHandleNeighborAdvert(IPv6Header* ipv6, AuVFSNode* nic);

#endif
