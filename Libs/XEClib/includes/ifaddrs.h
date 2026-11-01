/**
* BSD 2-Clause License
*
* Copyright (c) 2026, Manas Kamal Choudhury
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

#ifndef __XE_IFADDRS_H__
#define __XE_IFADDRS_H__

#include <stdint.h>
#include <_xeneva.h>
#include <sys/socket.h>
#include <net/if.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

struct ifaddrs {
	struct ifaddrs* ifa_next;
	char* ifa_name;
	unsigned int ifa_flags;
	sockaddr_* ifa_addr;
	sockaddr_* ifa_netmask;
	union {
		sockaddr_* ifu_broadaddr;
		sockaddr_* ifu_dstaddr;
	} ifa_ifu;
	void* ifa_data;
};

#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr ifa_ifu.ifu_dstaddr

/* Native snapshot record. Keep in sync with BaseHdr/Net/aunet.h. */
#define XE_IFNAME_MAX 16
#define XE_IFADDR_MAX 32

typedef struct _xe_ifaddrinfo_ {
	char ifa_name[XE_IFNAME_MAX];
	uint32_t ifa_index;
	uint16_t ifa_family;
	uint16_t ifa_flags;
	uint32_t ifa_addr4;
	uint32_t ifa_netmask4;
	uint32_t ifa_broadcast4;
	uint8_t ifa_mac[6];
	uint8_t ifa_link;
	uint8_t ifa_prefix6;
	uint8_t ifa_ipv6[16];
	uint8_t _ifa_pad[2];
} XeIfAddrInfo;

XE_LIB int getifaddrs(struct ifaddrs** ifap);
XE_LIB void freeifaddrs(struct ifaddrs* ifa);

#ifdef __cplusplus
}
#endif

#endif
