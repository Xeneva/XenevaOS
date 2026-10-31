/**
* @file udp.c
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
#include <Mm/kmalloc.h>
#include <Net/aunet.h>
#include <string.h>
#include <Net/ipv4.h>
#include <Net/ipv6.h>
#include <Net/udp.h>
#include <Net/ethernet.h>
#include <Drivers/uart.h>

list_t* udp_socket_list;
static int _udp_port = 12345; //12345

int UDPGetPort(AuSocket* sock) {
	int out = _udp_port++;
	sock->sessionPort = _udp_port;
	list_add(udp_socket_list, sock);
	return out;
}

/**
 * @brief AuUDPHandlePacket -- handle incoming packet
 * @param packet -- pointer to ipv4 packet
 */
void UDPHandlePacket(char* packet) {
	IPv4Header* ipv4 = (IPv4Header*)packet;
	UDPHeader* udp = (UDPHeader*)&ipv4->payload;
	uint16_t dest_port;
	memcpy(&dest_port, &udp->destPort, 2);
	uint16_t src_port;
	memcpy(&src_port, &udp->srcPort, 2);

	uint16_t totalLen = 0;
	memcpy(&totalLen, &ipv4->totalLength, 2);

#ifdef DEBUG_SERIAL
	UARTDebugOut(
		"UDP Packet received with dest_port : %d , src_port : %d \r\n", ntohs(dest_port), src_port);
	UARTDebugOut("Data bytes (%d bytes): \r\n", ntohs(totalLen));
	for (int i = 0; i < 15; i++) {
		UARTDebugOut("%c", udp->payload[i]);
	}
#endif

	/** add it to its destination socket **/
	for (int i = 0; i < udp_socket_list->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(udp_socket_list, i);
		if (sock->sessionPort == ntohs(dest_port)) {
			AuSocketAdd(sock, packet, ntohs(totalLen));
#ifdef DEBUG_SERIAL
			UARTDebugOut("UDP Packet added \r\n");
#endif
			break;
		}
	}
}

void UDPHandlePacket6(IPv6Header* ipv6) {
	UDPHeader* udp;
	uint16_t dest_port;
	uint16_t payloadLen;
	size_t totalLen;
	int i;

	if (!ipv6)
		return;
	udp = (UDPHeader*)&ipv6->payload;
	memcpy(&dest_port, &udp->destPort, 2);
	payloadLen = ntohs(ipv6->payloadLen);
	totalLen = sizeof(IPv6Header) + payloadLen;

	for (i = 0; i < udp_socket_list->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(udp_socket_list, i);
		if (sock->sessionPort == ntohs(dest_port)) {
			AuSocketAdd(sock, ipv6, totalLen);
			break;
		}
	}
}

/**
* @brief AuUDPReceive -- UDP protocol receive interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
int AuUDPReceive(AuSocket* sock, msghdr* msg, int flags) {
	char* packet;
	size_t stored;
	uint8_t version;

	(void)flags;
	if (sock->sessionPort == 0)
		return -1;

	if (msg->msg_iovlen > 1) {
		UARTDebugOut("[aurora]: UDP-net: Multiple iov is not supported \r\n");
		return -1;
	}

	if (msg->msg_iovlen == 0)
		return 0;

	AuNetRxPoll();
	packet = (char*)AuSocketGet(sock);
	if (!packet)
		return 0;

	stored = *(size_t*)packet;
	version = ((uint8_t*)(packet + sizeof(size_t)))[0] >> 4;

	if (version == 6) {
		IPv6Header* ipv6 = (IPv6Header*)(packet + sizeof(size_t));
		UDPHeader* udp = (UDPHeader*)&ipv6->payload;
		long len = (long)ntohs(ipv6->payloadLen) - (long)sizeof(UDPHeader);
		if (len < 0)
			len = 0;
		if ((size_t)len > msg->msg_iov[0].iov_len)
			len = (long)msg->msg_iov[0].iov_len;
		memcpy(msg->msg_iov[0].iov_base, udp->payload, (size_t)len);
		if (msg->msg_name && msg->msg_namelen >= sizeof(sockaddr_in6)) {
			sockaddr_in6* name = (sockaddr_in6*)msg->msg_name;
			name->sin6_family = AF_INET6;
			name->sin6_port = udp->srcPort;
			name->sin6_flowinfo = 0;
			memcpy(name->sin6_addr.s6_addr, ipv6->srcIP.s6_addr, 16);
			name->sin6_scope_id = 0;
			msg->msg_namelen = sizeof(sockaddr_in6);
		}
		kfree(packet);
		(void)stored;
		return (int)len;
	} else {
		IPv4Header* ipv4 = (IPv4Header*)(packet + sizeof(size_t));
		UDPHeader* udp = (UDPHeader*)&ipv4->payload;

		UARTDebugOut("[aurora]: UDP: Got Response %d \r\n", ntohs(ipv4->totalLength));
		memcpy(msg->msg_iov[0].iov_base,
			   udp->payload,
			   ntohs(ipv4->totalLength) - sizeof(IPv4Header) - sizeof(UDPHeader));

		if (msg->msg_namelen == sizeof(sockaddr_in)) {
			if (msg->msg_name) {
				((sockaddr_in*)msg->msg_name)->sin_family = AF_INET;
				((sockaddr_in*)msg->msg_name)->sin_port = udp->srcPort;
				((sockaddr_in*)msg->msg_name)->sin_addr.s_addr = ipv4->srcAddress;
			}
		}

		long len = ntohs(ipv4->totalLength) - sizeof(IPv4Header) - sizeof(UDPHeader);
		kfree(packet);
		return (int)len;
	}
}

/**
* @brief AuUDPSend -- UDP protocol send interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
int AuUDPSend(AuSocket* sock, msghdr* msg, int flags) {
	UARTDebugOut("[aurora]: UDP: Send -> %d \r\n", sizeof(UDPHeader));
	(void)flags;
	if (msg->msg_iovlen > 1) {
		UARTDebugOut("UDP: Multiple IOV is not supported \r\n");
		return 1;
	}
	if (msg->msg_iovlen == 0)
		return 0;

	if (sock->sessionPort == 0) {
		UDPGetPort(sock);
		UARTDebugOut("[aurora]:UDP: assigning port %d to socket \r\n", sock->sessionPort);
	}

	if (msg->msg_namelen == sizeof(sockaddr_in6)) {
		sockaddr_in6* sockin6 = (sockaddr_in6*)msg->msg_name;
		AuVFSNode* nic = AuNetworkRoute6((const ip6_addr*)&sockin6->sin6_addr);
		AuNetworkDevice* netdev;
		size_t total_len;
		uint16_t udpLen;
		IPv6Header* ipv6;
		UDPHeader* udp;

		if (!nic)
			return -114; /* ENETUNREACH */
		netdev = (AuNetworkDevice*)nic->device;
		if (!netdev)
			return -114;

		udpLen = (uint16_t)(sizeof(UDPHeader) + msg->msg_iov[0].iov_len);
		total_len = sizeof(IPv6Header) + udpLen;
		ipv6 = (IPv6Header*)kmalloc(total_len);
		if (!ipv6)
			return 0;
		memset(ipv6, 0, total_len);
		IPv6SetVerTcFl(ipv6, 6, 0, sockin6->sin6_flowinfo & 0xFFFFF);
		ipv6->payloadLen = htons(udpLen);
		ipv6->nextHeader = IPV6_NEXT_UDP;
		ipv6->hopLimit = 64;
		ip6_addr_copy(&ipv6->srcIP, &netdev->ipv6addr);
		memcpy(ipv6->destIP.s6_addr, sockin6->sin6_addr.s6_addr, 16);

		udp = (UDPHeader*)&ipv6->payload;
		udp->srcPort = htons(sock->sessionPort);
		udp->destPort = sockin6->sin6_port;
		udp->length = htons(udpLen);
		udp->checksum = 0;
		memcpy(&udp->payload, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len);
		udp->checksum = htons(IPv6PseudoChecksum(&ipv6->srcIP, &ipv6->destIP,
			udpLen, IPV6_NEXT_UDP, udp, udpLen));
		if (udp->checksum == 0)
			udp->checksum = 0xFFFF;

		IPV6SendPacket(ipv6, nic);
		kfree(ipv6);
		return (int)msg->msg_iov[0].iov_len;
	}

	if (msg->msg_namelen != sizeof(sockaddr_in)) {
		UARTDebugOut("UDP: invalid destination address size \r\n");
		return 0;
	}

	{
		sockaddr_in* sockin = (sockaddr_in*)msg->msg_name;
		AuVFSNode* nic = AuNetworkRoute(sockin->sin_addr.s_addr);
		AuNetworkDevice* netdev;
		size_t total_len;
		IPv4Header* ipv4;
		UDPHeader* udp;

		if (!nic) {
			UARTDebugOut("[aurora]:UDP: ENETUNREACH (no FIB route)\r\n");
			return -114; /* ENETUNREACH */
		}
		netdev = (AuNetworkDevice*)nic->device;
		if (!netdev) {
			UARTDebugOut("[aurora]: UDP: No network device found \r\n");
			return 0;
		}

		total_len = sizeof(IPv4Header) + msg->msg_iov[0].iov_len + sizeof(UDPHeader);
		ipv4 = (IPv4Header*)kmalloc(total_len);
		memset(ipv4, 0, total_len);
		ipv4->totalLength = htons((uint16_t)total_len);
		ipv4->destAddress = sockin->sin_addr.s_addr;
		ipv4->srcAddress = netdev->ipv4addr;
		ipv4->timeToLive = 64;
		ipv4->protocol = IPV4_PROTOCOL_UDP;
		ipv4->identification = 0;
		ipv4->flagsFragOffset = htons(0x4000);
		ipv4->versionHeaderLen = 0x45;
		ipv4->typeOfService = 0;
		ipv4->headerChecksum = 0;
		ipv4->headerChecksum = htons(IPv4CalculateChecksum(ipv4));

		udp = (UDPHeader*)&ipv4->payload;
		udp->srcPort = htons(sock->sessionPort);
		udp->destPort = sockin->sin_port;
		udp->length = htons((uint16_t)(sizeof(UDPHeader) + msg->msg_iov[0].iov_len));
		udp->checksum = 0;
		memcpy(&udp->payload, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len);

		IPV4SendPacket(ipv4, nic);
		kfree(ipv4);
		return (int)msg->msg_iov[0].iov_len;
	}
}

/**
* @brief AuUDPClose -- UDP protocol close call
* @param sock -- Pointer to socket
*/
void AuUDPClose(AuSocket* sock) {
	return;
}

/**
 * @brief AuUDPBind -- bind a local address to the current
 * socket, it also verifies if there is already a socket
 * with this local address, if not found, then assign this
 * socket with the given local address and assign it to NIC
 * for listening to incoming communication
 */
int AuUDPBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen) {
	int port = 0;
	int i;

	if (sock->sessionPort != 0)
		return -1;

	UARTDebugOut("[aurora]: binding udp  \r\n");
	if (addrlen >= sizeof(sockaddr_in6) && addr && addr->sa_family == AF_INET6) {
		sockaddr_in6* addr6 = (sockaddr_in6*)addr;
		port = ntohs(addr6->sin6_port);
	} else if (addrlen >= sizeof(sockaddr_in) && addr) {
		sockaddr_in* addr_in = (sockaddr_in*)addr;
		port = ntohs(addr_in->sin_port);
	} else {
		return -1;
	}

	UARTDebugOut("[aurora]: port -> %d \r\n", port);
	for (i = 0; i < udp_socket_list->pointer; i++) {
		AuSocket* existing = (AuSocket*)list_get_at(udp_socket_list, i);
		if (existing->sessionPort == port)
			return -1;
	}
	sock->sessionPort = (uint16_t)port;
	list_add(udp_socket_list, sock);
	UARTDebugOut("UDP Socket added \r\n");
	return 0;
}

uint64_t AuUDPRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	return 0;
}

uint64_t AuUDPWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	return 0;
}

int AuUDPFileClose(AuVFSNode* fsys, AuVFSNode* file) {
	/* here both fs and file points to socket file , better
	 * would be using file pointer */
	AuSocket* sock = (AuSocket*)file->device;

	/* Remove it from raw socket list */
	for (int i = 0; i < udp_socket_list->pointer; i++) {
		AuSocket* socket = (AuSocket*)list_get_at(udp_socket_list, i);
		if (socket == sock) {
			list_remove(udp_socket_list, i);
			break;
		}
	}
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
	UARTDebugOut("[aurora]: UDP: Socket Closed \r\n");
	return 0;
}
/*
 * CreateUDPSocket -- create a new UDP
 * socket
 */
int CreateUDPSocket() {
	int fd = -1;
	AA64Thread* thread = AuGetCurrentThread();
	if (!thread)
		return -1;
	AuProcess* proc = AuProcessFindThread(thread);
	if (!proc)
		proc = AuProcessFindSubThread(thread);
	if (!proc)
		return -1;
	AuSocket* sock = AuNetCreateSocket();
	fd = AuProcessGetFileDesc(proc);
	sock->bind = AuUDPBind;
	sock->close = AuUDPClose;
	sock->connect = 0;
	sock->receive = AuUDPReceive;
	sock->send = AuUDPSend;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "udp");
	node->flags |= FS_FLAG_SOCKET;
	node->device = sock;
	node->close = AuUDPFileClose;
	node->iocontrol = SocketIOControl;
	proc->fds[fd] = node;
	UARTDebugOut("[aurora]: UDP Socket created \r\n");
	return fd;
}

/**
 * @brief UDPProtocolInstall -- initialize the UDP socket
 */
void UDPProtocolInstall() {
	udp_socket_list = initialize_list();
}

/**
 * @brief UDPProtocolGetSockList -- returns the socket
 * list
 */
list_t* UDPProtocolGetSockList() {
	return udp_socket_list;
}