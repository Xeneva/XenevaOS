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
#include <Sync/waitq.h>
#include <stack.h>
#include <list.h>

#define AF_UNSPEC 0
#define AF_INET 1
#define AF_RAW 2
#define AF_INET6 3
#define AF_UNIX 4

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

/* Linux-compatible type flags. The low nibble stays the socket type. */
#define SOCK_TYPE_MASK 0x0000000F
#define SOCK_NONBLOCK  0x00000800
#define SOCK_CLOEXEC   0x00080000

#define IPPROTOCOL_ICMP 1
#define IPPROTOCOL_TCP  6
#define IPPROTOCOL_UDP  17
#define IPPROTOCOL_ICMPV6 58

/* Native socket options. These are deliberately not the Linux numbers:
 * SO_NONBLOCK predates the Linux ABI on this tree and already means
 * "connect() returns before the handshake finishes". */
#define SOL_SOCKET       0
#define SO_KEEPALIVE     1
#define SO_REUSEADDR     2
#define SO_BINDTODEVICE  3
#define SO_NONBLOCK      4
#define SO_ERROR         5
#define SO_TYPE          6
#define SO_RCVBUF        7
#define SO_SNDBUF        8
#define SO_ACCEPTCONN    9

#define TCP_NODELAY 10

#define SOMAXCONN 128

/* shutdown(2) how values, matching XEClib. */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

/* Message flags (native values, match XEClib). */
#define MSG_OOB       0x0001
#define MSG_PEEK      0x0002
#define MSG_DONTROUTE 0x0004
#define MSG_TRUNC     0x0020
#define MSG_DONTWAIT  0x0040
#define MSG_EOR       0x0080
#define MSG_WAITALL   0x0100
#define MSG_NOSIGNAL  0x4000
/* Turn off blocking after the first message of recvmmsg(2). */
#define MSG_WAITFORONE 0x10000

/* Blocking socket calls park for this long before re-checking the NIC.
 * An RX interrupt wakes them immediately; this is only the safety net for
 * a missed edge, so keep it short enough not to cap throughput. */
#define AU_SOCKET_WAIT_SLICE_MS 1

/* Native errno values, matching Libs/XEClib/includes/errno.h. */
#define AU_ERR_EBADF        9
#define AU_ERR_EAGAIN       11
#define AU_ERR_EFAULT       14
#define AU_ERR_EINVAL       22
#define AU_ERR_EMFILE       24
#define AU_ERR_ENOSYS       88
#define AU_ERR_EOPNOTSUPP   95
#define AU_ERR_ENOENT       2
#define AU_ERR_ECONNRESET   104
#define AU_ERR_EAFNOSUPPORT 106
#define AU_ERR_ENOTSOCK     108
#define AU_ERR_ENOPROTOOPT  109
#define AU_ERR_ECONNREFUSED 111
#define AU_ERR_EISCONN      127
#define AU_ERR_EADDRINUSE   112
#define AU_ERR_ENETUNREACH  114
#define AU_ERR_ETIMEDOUT    116
#define AU_ERR_EINPROGRESS  119
#define AU_ERR_EDESTADDRREQ 121
#define AU_ERR_EMSGSIZE     122
#define AU_ERR_EPIPE        32
#define AU_ERR_ESHUTDOWN    110
#define AU_ERR_ENOTCONN     128

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

/* One entry of sendmmsg(2)/recvmmsg(2). The kernel and XEClib headers must
 * keep this layout in sync (msghdr followed by the transferred length). */
typedef struct _mmsghdr_ {
	msghdr msg_hdr;
	unsigned int msg_len;
}mmsghdr;


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
	/* getsockname(2)/getpeername(2). Each callback writes the native
	 * sockaddr for its protocol and truncates to the caller's room. */
	int(*getsockname)(struct _socket_* sock, sockaddr* addr, socklen_t* addrlen);
	int(*getpeername)(struct _socket_* sock, sockaddr* addr, socklen_t* addrlen);
	/* shutdown(2): SHUT_RD/SHUT_WR/SHUT_RDWR. */
	int(*shutdown)(struct _socket_* sock, int how);
	/* Readiness for poll(2). Returns a POLL* mask; runs in the calling
	 * thread, so it must never sleep. */
	int(*poll)(struct _socket_* sock, int events);
	void* proto;
	/* 1: AuTCPConnect returns before the handshake finishes. */
	uint8_t nonblock;
	uint8_t cloexec;
	uint8_t family;
	uint8_t so_type;
	uint8_t listening;
	/* Pending asynchronous socket error, consumed by getsockopt(SO_ERROR). */
	int so_error;
	int rcvbuf;
	int sndbuf;
	uint8_t keepalive;
	uint8_t reuseaddr;
	uint8_t nodelay;
	/* Half-close state. shut_wr blocks further sends; shut_rd turns
	 * receive into EOF without touching the peer's send direction. */
	uint8_t shut_rd;
	uint8_t shut_wr;
	/* Default peer for connect()ed datagram sockets. */
	uint8_t peer_addr[128];
	socklen_t peer_len;
	/* Bound local address, stored by bind(2) for getsockname(2). */
	uint8_t local_addr[128];
	socklen_t local_len;
	/* Wake channels used by blocking calls and by poll(2). */
	AuWaitQueue rx_wait;
	AuWaitQueue tx_wait;
	AuWaitQueue accept_wait;
}AuSocket;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

typedef struct _sockaddr_un_ {
	unsigned short sun_family;
	char sun_path[108];
}sockaddr_un;


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
* @param type -- type, optionally ORed with SOCK_NONBLOCK/SOCK_CLOEXEC
* @param protocol -- protocol version
*/
extern int AuCreateSocket(int domain, int type, int protocol);

/* Kernel-side entry points for the extended socket syscalls implemented
 * in Net/netserv.c. The older Net* wrappers are declared in sysserv.h. */
extern int NetShutdown(int sockfd, int how);
extern int NetSocketPair(int domain, int type, int protocol, int* sv);
extern int NetGetSockName(int sockfd, sockaddr* addr, socklen_t* addrlen);
extern int NetGetPeerName(int sockfd, sockaddr* addr, socklen_t* addrlen);
extern int NetSendTo(int sockfd, const void* buf, size_t len, int flags,
	const sockaddr* addr, socklen_t addrlen);
extern int NetRecvFrom(int sockfd, void* buf, size_t len, int flags,
	sockaddr* addr, socklen_t* addrlen);
extern int NetSendMmsg(int sockfd, mmsghdr* msgvec, unsigned int vlen, int flags);
extern int NetRecvMmsg(int sockfd, mmsghdr* msgvec, unsigned int vlen,
	int flags, const void* timeout);
extern int NetIfNameToIndex(const char* ifname);
extern int NetGetIfAddrs(void* buf, int max);

/*
 * AuSocketCopyAddr -- copy a protocol sockaddr out to a user buffer using
 * getsockname(2) truncation rules: copy what fits, always report the full
 * length in *addrlen.
 */
extern int AuSocketCopyAddr(sockaddr* dst, socklen_t* addrlen,
	const void* src, socklen_t srclen);

/*
 * AuSocketCopyMsgName -- apply the same truncation rules to msghdr.msg_name
 * for recvmsg(2)/recvfrom(2). Sets msg_namelen to the full address length.
 */
extern int AuSocketCopyMsgName(msghdr* msg, const void* src, socklen_t srclen);

/*
 * AuSocketFromFd -- resolve a process file descriptor to a live socket.
 * Returns NULL for a bad descriptor, a non-socket node, or a closed node.
 */
extern AuSocket* AuSocketFromFd(int sockfd);

/*
 * AuSocketInstallFd -- allocate a VFS socket node and install it in the
 * current process. Returns the descriptor or a negative AU_ERR_* value.
 */
extern int AuSocketInstallFd(AuSocket* sock, const char* name,
	int (*closefn)(AuVFSNode*, AuVFSNode*));

/*
 * AuSocketSetOpt -- setsockopt(2) for the native socket ABI.
 */
extern int AuSocketSetOpt(int sockfd, int level, int optname, const void* optval, socklen_t optlen);

/*
 * AuSocketGetOpt -- getsockopt(2) for the native socket ABI.
 */
extern int AuSocketGetOpt(int sockfd, int level, int optname, void* optval, socklen_t* optlen);

/*
 * AuSocketProtocolPoll -- VFS poll callback shared by every socket node.
 */
extern int AuSocketProtocolPoll(AuVFSNode* fs, AuVFSNode* file, AuPollTable* table);

/*
 * AuSocketWaitSlice -- park the current thread on a socket wait queue for a
 * bounded slice. Callers re-check their condition around this call; waking
 * early is safe and is what makes blocking recv/accept responsive.
 */
extern void AuSocketWaitSlice(AuWaitQueue* q, uint64_t ms);

/* Wake helpers. Safe to call from packet dispatch. */
extern void AuSocketWakeRx(AuSocket* sock);
extern void AuSocketWakeTx(AuSocket* sock);
extern void AuSocketWakeAccept(AuSocket* sock);

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