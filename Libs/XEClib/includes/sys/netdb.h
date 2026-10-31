/**
* @file netdb.h
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
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

#ifndef __NETDB_H__
#define __NETDB_H__

#include <stdint.h>
#include <_xeneva.h>
#include <sys/socket.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

#pragma pack(push, 1)
#if defined(_MSC_VER)
	__declspec(align(2))
#else
__attribute__((aligned(2)))
#endif
	typedef struct _dns_ {
		uint16_t qid;
		uint16_t flags;
		uint16_t questions;
		uint16_t answers;
		uint16_t authorities;
		uint16_t additional;
		uint8_t data[];
	} DNSPacket;
#pragma pack(pop)

	struct hostent {
		char* h_name;
		char** h_aliases;
		int h_addrtype;
		int h_length;
		char** h_addr_list;
	};

#ifndef AI_PASSIVE
#define AI_PASSIVE     0x01
#define AI_CANONNAME   0x02
#define AI_NUMERICHOST 0x04
#define AI_NUMERICSERV 0x08
#endif

#ifndef EAI_NONAME
#define EAI_BADFLAGS -1
#define EAI_NONAME   -2
#define EAI_AGAIN    -3
#define EAI_FAIL     -4
#define EAI_FAMILY   -5
#define EAI_MEMORY   -6
#define EAI_SERVICE  -7
#define EAI_OVERFLOW -8
#endif

	XE_LIB hostent* gethostbyname(const char* name);
	XE_LIB int getaddrinfo(const char* node,
						   const char* service,
						   const addrinfo* hints,
						   addrinfo** res);
	XE_LIB void freeaddrinfo(addrinfo* res);
	XE_LIB const char* gai_strerror(int errcode);

	/* Last upstream DNS server used by the stub resolver (0 if local/none). */
	XE_LIB uint32_t xe_dns_last_server(void);

#ifdef __cplusplus
}
#endif

#endif
