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

#ifndef __TCP_H__
#define __TCP_H__

#include <list.h>
#include <Net/socket.h>
#include <Net/ipv4.h>
#include <Net/ipv6.h>
#include <Fs/vfs.h>

#define TCP_FLAGS_FIN (1<<0)
#define TCP_FLAGS_SYN (1<<1)
#define TCP_FLAGS_RST (1<<2)
#define TCP_FLAGS_PSH (1<<3)
#define TCP_FLAGS_ACK (1<<4)
#define TCP_FLAGS_URG (1<<5)
#define TCP_FLAGS_ECE (1<<6)
#define TCP_FLAGS_CWR (1<<7)
#define TCP_FLAGS_NS (1<<8)
#define TCP_DATA_OFFSET_5 (0x5 << 12)

#define TCP_DEFAULT_WIN_SZ 65535
#define TCP_MSS 1460
/* One TLS record is at most 16384 bytes plus a 5-byte header (RFC 8446).
 * The buffer has to hold that while the reader is still pulling the
 * previous record. The advertised window itself stays 16 bits (RFC 9293). */
#define TCP_RX_BUF_SZ 65536

#define TCP_STATE_CLOSED       0
#define TCP_STATE_LISTEN       1
#define TCP_STATE_SYN_SENT     2
#define TCP_STATE_SYN_RECEIVED 3
#define TCP_STATE_ESTABLISHED  4
#define TCP_STATE_FIN_WAIT_1   5
#define TCP_STATE_FIN_WAIT_2   6
#define TCP_STATE_CLOSE_WAIT   7
#define TCP_STATE_CLOSING      8
#define TCP_STATE_LAST_ACK     9
#define TCP_STATE_TIME_WAIT    10

#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(push,1)
#endif
typedef struct _tcphead_ {
	unsigned short srcPort;
	unsigned short destPort;
	unsigned sequenceNum;
	unsigned ackNum;
	unsigned short dataOffsetFlags;
	unsigned short window;
	unsigned short checksum;
	unsigned short urgentPointer;
}TCPHeader;
#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(pop)
#endif

typedef struct _tcp_pcb_ {
	uint8_t state;
	uint8_t fin_recvd;
	uint8_t is_ipv6;
	uint32_t iss;
	uint32_t irs;
	uint32_t snd_una;
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint32_t remote_ip;
	ip6_addr remote_ip6;
	uint16_t remote_port;
	uint16_t snd_wnd;
	uint16_t rcv_wnd;
	uint16_t mss;
	int backlog;
	AuVFSNode* nic;
	struct _socket_* parent;
	void* rxbuf;
	uint8_t* rxmem;
	list_t* acceptq;
	/* Deadline for the next SYN retry while a non-blocking connect waits. */
	uint64_t syn_sec;
	uint64_t syn_sub;
	uint8_t syn_tries;
} TCPControlBlock;

/*
* CreateTCPSocket -- creates a new TCP Socket
*/
extern int CreateTCPSocket();

/*
 * TCPGetSocketList -- return the current socket
 * list of TCP
 */
extern list_t* TCPGetSocketList();

/*
 * AuTCPAcknowledge -- send ack packets
 * @param nic -- Pointer to NIC device
 * @param sock -- Pointer to session socket
 * @param ippack -- Pointer to IPv4 packet
 * @param payloadLen -- length of the payload
*/
extern int AuTCPAcknowledge(AuVFSNode* nic, AuSocket* sock, IPv4Header* ippack, size_t payloadLen);

/*
 * TCPHandlePacket -- dispatch an incoming TCP segment
 */
extern void TCPHandlePacket(IPv4Header* pack, AuVFSNode* nic);

/*
 * TCPHandlePacket6 -- dispatch an incoming TCP segment over IPv6
 */
extern void TCPHandlePacket6(IPv6Header* pack, AuVFSNode* nic);

/*
 * TCPProtocolInstall -- initialize the TCP protocol
 */
extern void TCPProtocolInstall();

#endif