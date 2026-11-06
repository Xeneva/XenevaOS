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
#include <_xeneva.h>
#include <stddef.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

struct timespec;

#define AF_UNSPEC 0
#define AF_INET	  1
#define AF_RAW	  2
#define AF_INET6  3
#define AF_UNIX   4

#define SOCK_STREAM 1
#define SOCK_DGRAM	2
#define SOCK_RAW	3

#define SOCK_TYPE_MASK 0x0000000F
#define SOCK_NONBLOCK  0x00000800
#define SOCK_CLOEXEC   0x00080000

#define IPPROTOCOL_ICMP 1
#define IPPROTOCOL_TCP	6
#define IPPROTOCOL_UDP	17

#ifndef IPPROTO_TCP
#define IPPROTO_TCP	IPPROTOCOL_TCP
#define IPPROTO_UDP	IPPROTOCOL_UDP
#endif
#define IPPROTOCOL_ICMPV6 58

#define SOL_SOCKET 0

#define SO_KEEPALIVE	1
#define SO_REUSEADDR	2
#define SO_BINDTODEVICE 3
/* connect() returns -119 until the handshake finishes. */
#define SO_NONBLOCK	4
#define SO_ERROR	5
#define SO_TYPE		6
#define SO_RCVBUF	7
#define SO_SNDBUF	8
#define SO_ACCEPTCONN	9

#define TCP_NODELAY	10

#define SOMAXCONN	128

#define SHUT_RD		0
#define SHUT_WR		1
#define SHUT_RDWR	2

#define MSG_OOB       0x0001
#define MSG_PEEK      0x0002
#define MSG_DONTROUTE 0x0004
#define MSG_TRUNC     0x0020
#define MSG_DONTWAIT  0x0040
#define MSG_EOR       0x0080
#define MSG_WAITALL   0x0100
#define MSG_NOSIGNAL  0x4000
#define MSG_WAITFORONE 0x10000

	typedef size_t socklen_t;

#pragma pack(push, 1)
	typedef struct _dns_entry_ {
		int index;
		uint32_t address;
	} XEDNSEntry;
#pragma pack(pop)

	typedef struct _sock_addr_ {
		unsigned short sa_family; //address family
		char sa_data[14];
	} sockaddr_;

	typedef struct _addrinfo_ {
		int ai_flags;
		int ai_family;
		int ai_socktype;
		int ai_protocol;
		socklen_t ai_addrlen;
		sockaddr_* ai_addr;
		char* ai_canonname;
		struct _addrinfo_* ai_next;
	} addrinfo;

	typedef struct _iovec_ {
		void* iov_base;
		size_t iov_len;
	} iovec;

	typedef struct _msghdr_ {
		void* msg_name;
		socklen_t msg_namelen;
		iovec* msg_iov;
		size_t msg_iovlen;
		void* msg_control;
		size_t msg_controllen;
		int msg_flags;
	} msghdr;

	/* sendmmsg(2)/recvmmsg(2) entry. Keep in sync with the kernel. */
	typedef struct _mmsghdr_ {
		msghdr msg_hdr;
		unsigned int msg_len;
	} mmsghdr;

	/* Route flags (match BaseHdr/Net/route.h) */
#define RTF_UP        0x01
#define RTF_HOST      0x02
#define RTF_GATEWAY   0x04
#define RTF_LOCAL     0x08
#define RTF_CONNECTED 0x10

	/* Simple Route table entry structure */
	typedef struct _route_entry_ {
		char* ifname;
		uint32_t dest;
		uint32_t netmask;
		uint32_t ifaddress;
		uint32_t gateway;
		uint8_t flags;
	} XERouteEntry;

	typedef struct _route_entry_info_ {
		int index;
		void* route_entry;
	} XERouteEntryInfo;

	/* Netfilter (match BaseHdr/Net/netfilter.h) */
#define NF_PRE_ROUTING  0
#define NF_LOCAL_IN     1
#define NF_FORWARD      2
#define NF_LOCAL_OUT    3
#define NF_POST_ROUTING 4
#define NF_ACCEPT       0
#define NF_DROP         1
#define NF_REJECT       2

	typedef struct _xe_nf_rule_ {
		int hook;
		char in_dev[16];
		char out_dev[16];
		uint8_t proto;
		uint32_t src;
		uint32_t src_mask;
		uint32_t dst;
		uint32_t dst_mask;
		uint16_t sport;
		uint16_t dport;
		int target;
	} XENfRule;

	typedef struct _xe_nf_rule_info_ {
		int index;
		XENfRule* rule;
	} XENfRuleInfo;

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
	} sockaddr_in;

	struct in6_addr {
		uint8_t s6_addr[16];
	};

	typedef struct _sockaddr_in6_ {
		short sin6_family;
		unsigned short sin6_port;
		uint32_t sin6_flowinfo;
		struct in6_addr sin6_addr;
		uint32_t sin6_scope_id;
	} sockaddr_in6;

	typedef struct _sockaddr_un_ {
		unsigned short sun_family;
		char sun_path[108];
	} sockaddr_un;

	XE_LIB int socket(int domain, int type, int protocol);
	XE_LIB int connect(int sockfd, sockaddr_* addr, socklen_t addrlen);
	XE_LIB int send(int sockfd, msghdr* msg, int flags);
	XE_LIB int receive(int sockfd, msghdr* msg, int flags);
	XE_LIB int socket_setopt(
		int sockfd, int level, int optname, const void* optval, socklen_t optlen);
	XE_LIB int bind(int sockfd, struct sockaddr* addr, socklen_t addrlen);
	XE_LIB int accept(int sockfd, sockaddr* addr, socklen_t* addrlen);
	XE_LIB int accept4(int sockfd, sockaddr* addr, socklen_t* addrlen, int flags);
	XE_LIB int listen(int sockfd, int backlog);
	XE_LIB int getsockopt(
		int sockfd, int level, int optname, void* optval, socklen_t* optlen);
	XE_LIB int getsockname(int sockfd, sockaddr_* addr, socklen_t* addrlen);
	XE_LIB int getpeername(int sockfd, sockaddr_* addr, socklen_t* addrlen);
	XE_LIB int socketpair(int domain, int type, int protocol, int sv[2]);
	XE_LIB int shutdown(int sockfd, int how);
	XE_LIB ssize_t sendmmsg(int sockfd, mmsghdr* msgvec, unsigned int vlen,
							int flags);
	XE_LIB ssize_t recvmmsg(int sockfd, mmsghdr* msgvec, unsigned int vlen,
							int flags, const struct timespec* timeout);
	XE_LIB int setsockopt(
		int sockfd, int level, int optname, const void* optval, socklen_t optlen);
	XE_LIB ssize_t sendmsg(int sockfd, const msghdr* msg, int flags);
	XE_LIB ssize_t recvmsg(int sockfd, msghdr* msg, int flags);
	XE_LIB ssize_t recvfrom(int sockfd,
							void* buf,
							size_t len,
							int flags,
							struct sockaddr* src_addr,
							socklen_t* addrlen);
	XE_LIB ssize_t sendto(int sockfd,
						  const void* buf,
						  size_t len,
						  int flags,
						  const struct sockaddr* dest_addr,
						  socklen_t addrlen);
#ifdef __cplusplus
}
#endif

#endif