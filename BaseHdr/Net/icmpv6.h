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

#ifndef __ICMPV6_H__
#define __ICMPV6_H__

#include <Net/ipv6.h>
#include <Fs/vfs.h>

#define ICMPV6_ECHO_REQUEST 128
#define ICMPV6_ECHO_REPLY   129
#define ICMPV6_ND_RS        133
#define ICMPV6_ND_RA        134
#define ICMPV6_ND_NS        135
#define ICMPV6_ND_NA        136

#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(push,1)
#endif
typedef struct _icmpv6_head_ {
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint16_t identifier;
	uint16_t sequenceNum;
	uint8_t payload[];
} ICMPv6Header;
#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(pop)
#endif

extern void ICMPv6Initialise();
extern void AuICMPv6Handle(IPv6Header* ipv6, AuVFSNode* nic);
extern int CreateICMPv6Socket();

#endif
