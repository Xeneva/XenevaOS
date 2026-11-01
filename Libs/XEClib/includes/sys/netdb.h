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

	struct servent {
		char* s_name;
		char** s_aliases;
		int s_port;
		char* s_proto;
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
#define EAI_SOCKTYPE -9
#endif

/* getnameinfo(3) flags. */
#ifndef NI_NOFQDN
#define NI_NOFQDN      0x01
#define NI_NUMERICHOST 0x02
#define NI_NAMEREQD    0x04
#define NI_NUMERICSERV 0x08
#define NI_DGRAM       0x10
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#endif

/* h_errno values (gethostbyname/gethostbyaddr). */
#ifndef HOST_NOT_FOUND
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4
#define NO_ADDRESS     NO_DATA
#endif

	extern int h_errno;

	XE_LIB hostent* gethostbyname(const char* name);
	XE_LIB hostent* gethostbyaddr(const void* addr, socklen_t len, int type);
	XE_LIB servent* getservbyname(const char* name, const char* proto);
	XE_LIB servent* getservbyport(int port, const char* proto);
	XE_LIB void herror(const char* s);
	XE_LIB const char* hstrerror(int err);
	XE_LIB int getaddrinfo(const char* node,
						   const char* service,
						   const addrinfo* hints,
						   addrinfo** res);
	XE_LIB void freeaddrinfo(addrinfo* res);
	XE_LIB const char* gai_strerror(int errcode);
	XE_LIB int getnameinfo(const struct sockaddr* sa,
						   socklen_t salen,
						   char* host,
						   socklen_t hostlen,
						   char* serv,
						   socklen_t servlen,
						   int flags);

	/* Last upstream DNS server used by the stub resolver (0 if local/none). */
	XE_LIB uint32_t xe_dns_last_server(void);

#ifdef __cplusplus
}
#endif

#endif
