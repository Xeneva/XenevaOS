/**
* @file icmp.c
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

#include <Net/socket.h>
#include <process.h>
#include <Fs/vfs.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <Net/ipv4.h>
#include <Net/aunet.h>
#include <_null.h>
#include <Drivers/uart.h>
#include <Net/icmp.h>
#include <Sync/poll.h>

AuSocket* current_icmp_sock;

uint16_t AuICMPChecksum(IPv4Header* packet) {
	uint32_t sum = 0;
	uint16_t* s = (uint16_t*)packet->payload;
	for (int i = 0; i < (ntohs(packet->totalLength) - 20) / 2; ++i)
		sum += ntohs(s[i]);

	if (sum > 0xFFFF)
		sum = (sum >> 16) + (sum & 0xFFFF);

	return ~(sum & UINT16_MAX) & UINT16_MAX;
}

/*
 * AuICMPHandle -- ICMP handler
 */
void AuICMPHandle(IPv4Header* ipv4, AuVFSNode* nic) {
	ICMPHeader* header = (ICMPHeader*)&ipv4->payload;

	AuNetworkDevice* netdev = (AuNetworkDevice*)nic->device;
	if (!netdev) {
		AU_NET_LOG("[AuNet]: ICMP Handle no network device found \r\n");
		return;
	}

	/* PING */
	if (header->type == 8 && header->code == 0) {
		if (ntohs(ipv4->totalLength) & 1)
			ipv4->totalLength = htons(ntohs(ipv4->totalLength) + 1);

		IPv4Header* resp = (IPv4Header*)kmalloc(ntohs(ipv4->totalLength));
		memcpy(resp, ipv4, ntohs(ipv4->totalLength));
		resp->totalLength = ipv4->totalLength;
		resp->destAddress = ipv4->srcAddress;
		/* ipv4addr is already MAKE_IP/wire form — do not htonl again. */
		resp->srcAddress = netdev->ipv4addr;
		resp->timeToLive = 64;
		resp->protocol = 1;
		resp->identification = ipv4->identification;
		resp->flagsFragOffset = htons(0x4000);
		resp->versionHeaderLen = 0x45;
		resp->typeOfService = 0;
		resp->headerChecksum = 0;
		resp->headerChecksum = htons(IPv4CalculateChecksum(resp));

		ICMPHeader* reply = (ICMPHeader*)&resp->payload;
		reply->checksum = 0;
		reply->type = 0;
		reply->code = 0;
		reply->checksum = htons(AuICMPChecksum(resp));

		IPV4SendPacket(resp, nic);
		kfree(resp);
	} else if (header->type == 0 && header->code == 0) {
		if (current_icmp_sock) {
			AuSocketAdd(current_icmp_sock, ipv4, ntohs(ipv4->totalLength));
			AuSocketWakeRx(current_icmp_sock);
		}
	}
}

void AuICMPSendDestUnreachable(IPv4Header* orig, AuVFSNode* nic, uint8_t code) {
	AuNetworkDevice* netdev;
	size_t icmpLen;
	size_t totalLen;
	size_t copyLen;
	IPv4Header* resp;
	ICMPHeader* icmp;
	uint8_t* payload;

	if (!orig || !nic || !nic->device)
		return;
	if (orig->protocol == 1)
		return;
	netdev = (AuNetworkDevice*)nic->device;

	copyLen = 20 + 8;
	if (ntohs(orig->totalLength) < copyLen)
		copyLen = ntohs(orig->totalLength);
	icmpLen = 8 + copyLen; /* type/code/csum/unused + orig */
	totalLen = sizeof(IPv4Header) + icmpLen;

	resp = (IPv4Header*)kmalloc(totalLen);
	if (!resp)
		return;
	memset(resp, 0, totalLen);
	resp->versionHeaderLen = 0x45;
	resp->typeOfService = 0;
	resp->totalLength = htons((uint16_t)totalLen);
	resp->identification = 0;
	resp->flagsFragOffset = htons(0x4000);
	resp->timeToLive = 64;
	resp->protocol = 1;
	resp->srcAddress = netdev->ipv4addr;
	resp->destAddress = orig->srcAddress;
	resp->headerChecksum = 0;
	resp->headerChecksum = htons(IPv4CalculateChecksum(resp));

	icmp = (ICMPHeader*)&resp->payload;
	icmp->type = 3; /* Destination Unreachable */
	icmp->code = code;
	icmp->identifier = 0;
	icmp->sequenceNum = 0;
	payload = (uint8_t*)icmp + 8;
	memcpy(payload, orig, copyLen);
	icmp->checksum = 0;
	icmp->checksum = htons(AuICMPChecksum(resp));

	IPV4SendPacket(resp, nic);
	kfree(resp);
}
/*
* AuICMPReceive -- ICMP protocol receive interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
static size_t ICMPIovCopyIn(const msghdr* msg, uint8_t* dst, size_t max) {
	size_t done = 0;
	size_t i;

	for (i = 0; i < msg->msg_iovlen && done < max; i++) {
		size_t chunk = msg->msg_iov[i].iov_len;
		if (chunk > max - done)
			chunk = max - done;
		if (chunk && msg->msg_iov[i].iov_base) {
			memcpy(dst + done, msg->msg_iov[i].iov_base, chunk);
			done += chunk;
		}
	}
	return done;
}

static size_t ICMPIovCopyOut(const msghdr* msg, const uint8_t* src, size_t len) {
	size_t done = 0;
	size_t i;

	for (i = 0; i < msg->msg_iovlen && done < len; i++) {
		size_t chunk = msg->msg_iov[i].iov_len;
		if (chunk > len - done)
			chunk = len - done;
		if (chunk && msg->msg_iov[i].iov_base) {
			memcpy(msg->msg_iov[i].iov_base, src + done, chunk);
			done += chunk;
		}
	}
	return done;
}

int AuICMPReceive(AuSocket* sock, msghdr* msg, int flags) {
	uint64_t deadline;

	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (!msg || msg->msg_iovlen == 0)
		return 0;

	deadline = AuGetCurrentUS() + (uint64_t)30000 * 1000ULL;
	for (;;) {
		char* packet;

		AuNetRxPoll();
		packet = (char*)AuSocketGet(sock);
		if (packet) {
			size_t stored = *(size_t*)packet;
			IPv4Header* src = (IPv4Header*)(packet + sizeof(size_t));
			size_t hdr = (size_t)(src->versionHeaderLen & 0x0F) * 4;
			size_t plen = 0;
			size_t copied;

			if (stored > hdr && hdr >= sizeof(IPv4Header))
				plen = stored - hdr;
			copied = ICMPIovCopyOut(msg, src->payload, plen);
			if (copied < plen)
				msg->msg_flags |= MSG_TRUNC;

			if (msg->msg_name && msg->msg_namelen >= sizeof(sockaddr_in)) {
				sockaddr_in* name = (sockaddr_in*)msg->msg_name;
				name->sin_family = AF_INET;
				name->sin_port = 0;
				/* Network/wire form -- inet_ntoa expects this. */
				name->sin_addr.s_addr = src->srcAddress;
				memset(name->sin_zero, 0, sizeof(name->sin_zero));
				name->sin_zero[0] = src->timeToLive;
				msg->msg_namelen = sizeof(sockaddr_in);
			}
			kfree(packet);
			return (int)copied;
		}

		if (sock->nonblock || (flags & MSG_DONTWAIT))
			return -AU_ERR_EAGAIN;
		if (AuGetCurrentUS() >= deadline)
			return -AU_ERR_ETIMEDOUT;
		AuSocketWaitSlice(&sock->rx_wait, AU_SOCKET_WAIT_SLICE_MS);
	}
}

/*
* AuICMPSend -- ICMP protocol send interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
int AuICMPSend(AuSocket* sock, msghdr* msg, int flags) {
	sockaddr_in* name;
	AuVFSNode* nic;
	AuNetworkDevice* netdev;
	size_t payload;
	size_t totalLen;
	IPv4Header* resp;

	(void)sock;
	(void)flags;
	if (!msg || msg->msg_iovlen == 0)
		return 0;
	if (!msg->msg_name || msg->msg_namelen < sizeof(sockaddr_in))
		return -AU_ERR_EDESTADDRREQ;

	name = (sockaddr_in*)msg->msg_name;
	if (name->sin_family != AF_INET)
		return -AU_ERR_EAFNOSUPPORT;

	payload = 0;
	for (size_t i = 0; i < msg->msg_iovlen; i++)
		payload += msg->msg_iov[i].iov_len;
	if (payload > 65507)
		return -AU_ERR_EMSGSIZE;

	nic = AuNetworkRoute(name->sin_addr.s_addr);
	if (!nic)
		return -AU_ERR_ENETUNREACH;
	netdev = (AuNetworkDevice*)nic->device;
	if (!netdev)
		return -AU_ERR_ENETUNREACH;

	totalLen = sizeof(IPv4Header) + payload;
	resp = (IPv4Header*)kmalloc(totalLen);
	if (!resp)
		return -AU_ERR_EMFILE;
	memset(resp, 0, totalLen);
	resp->totalLength = htons((uint16_t)totalLen);
	/* sockaddr_in holds network/wire order already -- do not htonl again. */
	resp->destAddress = name->sin_addr.s_addr;
	resp->srcAddress = netdev->ipv4addr;
	resp->timeToLive = 64;
	resp->protocol = 1;
	resp->identification = 0;
	resp->flagsFragOffset = htons(0x4000);
	resp->versionHeaderLen = 0x45;
	resp->typeOfService = 0;
	resp->headerChecksum = 0;
	resp->headerChecksum = htons(IPv4CalculateChecksum(resp));

	ICMPIovCopyIn(msg, resp->payload, payload);
	IPV4SendPacket(resp, nic);
	kfree(resp);
	return (int)payload;
}

static int AuICMPPoll(AuSocket* sock, int events) {
	int mask = POLLOUT;

	(void)events;
	if (!sock)
		return POLLNVAL;
	if (sock->rxstack && sock->rxstack->itemCount)
		mask |= POLLIN;
	if (sock->so_error)
		mask |= POLLERR;
	return mask;
}

/*
* AuICMPClose -- ICMP protocol close call
* @param sock -- Pointer to socket
*/
void AuICMPClose(AuSocket* sock) {
	return;
}

int AuICMPBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	return 0;
}

uint64_t AuICMPRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	return 0;
}

uint64_t AuICMPWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	return 0;
}

int AuICMPFileClose(AuVFSNode* fsys, AuVFSNode* file) {
	/* here both fs and file points to socket file , better
	 * would be using file pointer */
	AuSocket* sock = (AuSocket*)file->device;

	if (sock) {
		if (sock->rxstack) {
			while (sock->rxstack->itemCount) {
				void* data = AuStackPop(sock->rxstack);
				kfree(data);
			}
			kfree(sock->rxstack);
		}

		kfree(sock);
	}

	kfree(file);
	current_icmp_sock = NULL;
	AU_NET_LOG("ICMP: Socket Closed \r\n");
	return 0;
}

/*
 * ICMPInitialise -- initialise ICMP
 */
void ICMPInitialise() {
	current_icmp_sock = 0;
}

/*
 * CreateICMPSocket -- create a new Internet
 * Control Message Protocol (ICMP) protocol
 */
int CreateICMPSocket() {
	AuSocket* sock;
	int fd;

	if (current_icmp_sock) {
		AU_NET_LOG("ICMP: Already there is a socket listening to ICMP, close that \r\n");
		return -AU_ERR_EADDRINUSE;
	}
	sock = AuNetCreateSocket();
	if (!sock)
		return -AU_ERR_EMFILE;
	sock->bind = AuICMPBind;
	sock->close = AuICMPClose;
	sock->connect = 0;
	sock->receive = AuICMPReceive;
	sock->send = AuICMPSend;
	sock->poll = AuICMPPoll;
	sock->so_type = SOCK_DGRAM;
	sock->family = AF_INET;

	fd = AuSocketInstallFd(sock, "icmp", AuICMPFileClose);
	if (fd < 0) {
		if (sock->rxstack)
			kfree(sock->rxstack);
		kfree(sock);
		return fd;
	}
	current_icmp_sock = sock;
	AU_NET_LOG("ICMP Socket created \r\n");
	return fd;
}
