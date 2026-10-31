/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2024, Manas Kamal Choudhury
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

#ifndef __INET_H__
#define __INET_H__

#include <stdint.h>
#include <sys/socket.h>
#include <_xeneva.h>

#ifndef INADDR_ANY
#define INADDR_ANY       ((in_addr_t)0x00000000)
#define INADDR_BROADCAST ((in_addr_t)0xFFFFFFFF)
#define INADDR_NONE      ((in_addr_t)0xFFFFFFFF)
#define INADDR_LOOPBACK  ((in_addr_t)0x7F000001)
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46
#endif

#ifdef __cplusplus
XE_EXTERN {
#endif

	XE_LIB uint32_t htonl(uint32_t hostlong);
	XE_LIB uint16_t htons(uint16_t hostshort);
	XE_LIB uint32_t ntohl(uint32_t netlong);
	XE_LIB uint16_t ntohs(uint16_t netshort);
	XE_LIB in_addr_t inet_addr(const char* in);
	XE_LIB char* inet_ntoa(struct in_addr in);
	XE_LIB int inet_pton(int af, const char* src, void* dst);
	XE_LIB const char* inet_ntop(int af, const void* src, char* dst, size_t size);

#ifdef __cplusplus
}
#endif

#endif