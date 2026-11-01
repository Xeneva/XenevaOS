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

#include <ifaddrs.h>
#include <sys/_kenet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static sockaddr_in* ifa_alloc_in(uint32_t address, unsigned short port) {
	sockaddr_in* in = (sockaddr_in*)malloc(sizeof(sockaddr_in));

	if (!in)
		return NULL;
	memset(in, 0, sizeof(*in));
	in->sin_family = AF_INET;
	in->sin_port = port;
	in->sin_addr.s_addr = address;
	return in;
}

static int ifa_ipv6_nonzero(const uint8_t addr[16]) {
	int i;

	for (i = 0; i < 16; i++) {
		if (addr[i])
			return 1;
	}
	return 0;
}

static sockaddr_in6* ifa_alloc_in6(const uint8_t address[16]) {
	sockaddr_in6* in6 = (sockaddr_in6*)malloc(sizeof(sockaddr_in6));

	if (!in6)
		return NULL;
	memset(in6, 0, sizeof(*in6));
	in6->sin6_family = AF_INET6;
	memcpy(in6->sin6_addr.s6_addr, (void*)address, 16);
	return in6;
}

static sockaddr_in6* ifa_prefix_mask6(uint8_t prefix) {
	sockaddr_in6* in6 = (sockaddr_in6*)malloc(sizeof(sockaddr_in6));
	uint8_t full;
	uint8_t rem;
	uint8_t i;

	if (!in6)
		return NULL;
	memset(in6, 0, sizeof(*in6));
	in6->sin6_family = AF_INET6;
	if (prefix > 128)
		prefix = 128;
	full = (uint8_t)(prefix / 8);
	rem = (uint8_t)(prefix % 8);
	for (i = 0; i < full; i++)
		in6->sin6_addr.s6_addr[i] = 0xFF;
	if (rem && full < 16)
		in6->sin6_addr.s6_addr[full] = (uint8_t)(0xFF << (8 - rem));
	return in6;
}

static struct ifaddrs* ifa_make(const XeIfAddrInfo* rec, int family) {
	struct ifaddrs* ifa = (struct ifaddrs*)malloc(sizeof(struct ifaddrs));

	if (!ifa)
		return NULL;
	memset(ifa, 0, sizeof(*ifa));
	ifa->ifa_name = strdup(rec->ifa_name);
	if (!ifa->ifa_name) {
		free(ifa);
		return NULL;
	}
	ifa->ifa_flags = rec->ifa_flags;
	if (family == AF_INET) {
		ifa->ifa_addr = (sockaddr_*)ifa_alloc_in(rec->ifa_addr4, 0);
		ifa->ifa_netmask = (sockaddr_*)ifa_alloc_in(rec->ifa_netmask4, 0);
		ifa->ifa_ifu.ifu_broadaddr =
			(sockaddr_*)ifa_alloc_in(rec->ifa_broadcast4, 0);
		if (!ifa->ifa_addr || !ifa->ifa_netmask ||
			!ifa->ifa_ifu.ifu_broadaddr) {
			freeifaddrs(ifa);
			return NULL;
		}
	} else if (family == AF_INET6) {
		ifa->ifa_addr = (sockaddr_*)ifa_alloc_in6(rec->ifa_ipv6);
		ifa->ifa_netmask =
			(sockaddr_*)ifa_prefix_mask6(rec->ifa_prefix6);
		if (!ifa->ifa_addr || !ifa->ifa_netmask) {
			freeifaddrs(ifa);
			return NULL;
		}
	}
	return ifa;
}

unsigned int if_nametoindex(const char* ifname) {
	int rc;

	if (!ifname) {
		errno = EINVAL;
		return 0;
	}
	rc = _KeIfNameToIndex(ifname);
	if (rc <= 0) {
		errno = rc < 0 ? -rc : ENXIO;
		return 0;
	}
	return (unsigned int)rc;
}

int getifaddrs(struct ifaddrs** ifap) {
	XeIfAddrInfo records[XE_IFADDR_MAX];
	struct ifaddrs* head = NULL;
	struct ifaddrs* tail = NULL;
	int n;
	int i;

	if (!ifap) {
		errno = EFAULT;
		return -1;
	}
	*ifap = NULL;
	n = _KeGetIfAddrs(records, XE_IFADDR_MAX);
	if (n < 0) {
		errno = -n;
		return -1;
	}
	for (i = 0; i < n; i++) {
		XeIfAddrInfo* rec = &records[i];
		int has4 = rec->ifa_addr4 != 0;
		int has6 = ifa_ipv6_nonzero(rec->ifa_ipv6);
		int families[3];
		int count = 0;
		int j;

		if (has4)
			families[count++] = AF_INET;
		if (has6)
			families[count++] = AF_INET6;
		if (count == 0)
			families[count++] = AF_UNSPEC;
		for (j = 0; j < count; j++) {
			struct ifaddrs* ifa = ifa_make(rec, families[j]);

			if (!ifa) {
				freeifaddrs(head);
				errno = ENOMEM;
				return -1;
			}
			if (!head)
				head = ifa;
			else
				tail->ifa_next = ifa;
			tail = ifa;
		}
	}
	*ifap = head;
	return 0;
}

void freeifaddrs(struct ifaddrs* ifa) {
	while (ifa) {
		struct ifaddrs* next = ifa->ifa_next;

		if (ifa->ifa_name)
			free(ifa->ifa_name);
		if (ifa->ifa_addr)
			free(ifa->ifa_addr);
		if (ifa->ifa_netmask)
			free(ifa->ifa_netmask);
		if (ifa->ifa_ifu.ifu_broadaddr)
			free(ifa->ifa_ifu.ifu_broadaddr);
		free(ifa);
		ifa = next;
	}
}
