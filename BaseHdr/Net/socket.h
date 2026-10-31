/**
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

#ifndef __SOCKET_H__
#define __SOCKET_H__

#include <stdint.h>
#include <Fs/vfs.h>
#include <stack.h>
#include <list.h>

#define AF_UNSPEC 0
#define AF_INET 1
#define AF_RAW 2
#define AF_INET6 3

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define IPPROTOCOL_ICMP 1
#define IPPROTOCOL_TCP  6
#define IPPROTOCOL_UDP  17
#define IPPROTOCOL_ICMPV6 58

/* IO Control Codes */
//Routing Table codes
#define SOCK_ROUTE_TABLE_ADD 0x120
#define SOCK_ROUTE_TABLE_DELETE 0x121
#define SOCK_ROUTE_TABLE_GETNUMENTRY 0x122
#define SOCK_ROUTE_TABLE_GETENTRY 0x123
#define SOCK_ADD_DNS_SERVER 0x124
#define SOCK_GET_DNS_SERVER 0x125
#define SOCK_NF_APPEND 0x126
#define SOCK_NF_DELETE 0x127
#define SOCK_NF_LIST 0x128
#define SOCK_NF_FLUSH 0x129
#define SOCK_NF_GETNUM 0x12A


typedef size_t socklen_t;


typedef struct _sockaddr_ {
	unsigned short sa_family;  //address family
	char           sa_data[14];
}sockaddr;

typedef struct _addrinfo_ {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	sockaddr *ai_addr;
	char *ai_canonname;
	struct _addinfo_* ai_next;
}addrinfo;

typedef struct _iovec_ {
	void* iov_base;
	size_t iov_len;
}iovec;

typedef struct _msghdr_ {
	void* msg_name;
	socklen_t msg_namelen;
	iovec *msg_iov;
	size_t msg_iovlen;
	void* msg_control;
	size_t msg_controllen;
	int msg_flags;
}msghdr;


#define SOCK_STATE_CONNECTION_RST 0
#define SOCK_STATE_WAITING_FOR_CONNECTION 1
#define SOCK_STATE_CONNECTED 2
#define SOCK_STATE_CLOSED 3

#ifdef ARCH_X64
#pragma pack(push,1)
#endif
typedef struct _socket_ {
	void* binedDev;
	AuStack *rxstack;
	uint16_t sessionPort;
	uint8_t sockState;
	unsigned packID;
	uint16_t ipv4Iden;
	int(*receive)(struct _socket_* sock, msghdr *msg, int flags);
	int(*send)(struct _socket_* sock, msghdr* msg, int flags);
	void(*close)(struct _socket_* sock);
	int(*connect)(struct _socket_* sock, sockaddr* addr, socklen_t addrlen);
	int(*bind)(struct _socket_* sock, sockaddr* addr, socklen_t addrlen);
	int(*listen)(struct _socket_* sock, int backlog);
	int(*accept)(struct _socket_* sock, sockaddr* addr, socklen_t* addrlen);
	void* proto;
	/* 1: AuTCPConnect returns before the handshake finishes. */
	uint8_t nonblock;
}AuSocket;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr {
	in_addr_t s_addr;
};

typedef struct _sockaddr_in_ {
	short sin_family;
	unsigned short sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
}sockaddr_in;

struct in6_addr {
	uint8_t s6_addr[16];
};

typedef struct _sockaddr_in6_ {
	short sin6_family;
	unsigned short sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
}sockaddr_in6;


extern AuSocket* AuNetCreateSocket();
/*
 * AuSocketAdd -- add some data to socket
 * @param sock -- Pointer to the socket
 * @param data -- data to add
 * @param sz -- Size of the data
 */
extern void AuSocketAdd(AuSocket* sock, void* data, size_t sz);

/*
 * AuSocketGet -- retreives previously stacked
 * data from the socket
 * @param sock -- Pointer to the socket
 */
extern void* AuSocketGet(AuSocket* sock);
/*
* AuSocketInstall -- install the socket
* interface
*/
extern void AuSocketInstall();
/*
* AuCreateSocket -- create a new socket for specific
* domain
* @param domain -- domain type -- IPV4_INET, RAW or IPV6_INET
* @param type -- type
* @param protocol -- protocol version
*/
extern int AuCreateSocket(int domain, int type, int protocol);

extern int AuSocketSetOpt(int sockfd, int level, int optname, const void* optval, socklen_t optlen);

/*
* AuRawSocketGetList -- get raw socket list
*/
extern list_t* AuRawSocketGetList();


/*
 * SocketIOControl -- Global socket io control function
 * @param file -- Pointer to the socket file
 * @param code -- IO Control Code
 * @param arg -- extra argument
 */
extern int SocketIOControl(AuVFSNode* file, int code, void* arg);

#endif