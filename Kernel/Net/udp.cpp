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

#include <Net/socket.h>
#include <process.h>
#include <Mm/kmalloc.h>
#include <Net/aunet.h>
#include <string.h>
#include <Net/ipv4.h>
#include <Net/udp.h>
#include <Net/ethernet.h>
#include <Hal/serial.h>

list_t* udp_socket_list;
static int _udp_port = 12345; //12345

int UDPGetPort(AuSocket* sock) {
	int out = _udp_port++;
	sock->sessionPort = _udp_port;
	list_add(udp_socket_list, sock);
	return out;
}
/*
* AuUDPReceive -- UDP protocol receive interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
int AuUDPReceive(AuSocket* sock, msghdr *msg, int flags){
	if (sock->sessionPort == 0)
		return -1;

	if (msg->msg_iovlen > 1) {
		SeTextOut("UDP: Multiple iov is not supported \r\n");
		return -1;
	}

	if (msg->msg_iovlen == 0)
		return 0;
	
	char* packet = (char*)AuSocketGet(sock);
	if (!packet) return -1;
	IPv4Header* ipv4 = (IPv4Header*)(packet + sizeof(size_t));
	UDPHeader* udp = (UDPHeader*)&ipv4->payload;

	SeTextOut("UDP: Got Response %d \r\n", ntohs(ipv4->totalLength));
	memcpy(msg->msg_iov[0].iov_base, udp->payload, ntohs(ipv4->totalLength) - sizeof(IPv4Header) -
		sizeof(UDPHeader));

	if (msg->msg_namelen == sizeof(sockaddr_in)) {
		if (msg->msg_name) {
			((sockaddr_in*)msg->msg_name)->sin_family = AF_INET;
			((sockaddr_in*)msg->msg_name)->sin_port = udp->srcPort;
			((sockaddr_in*)msg->msg_name)->sin_addr.s_addr = ipv4->srcAddress;
		}
	}

	long len = ntohs(ipv4->totalLength) - sizeof(IPv4Header) - sizeof(UDPHeader);
	kfree(packet);
	return len;
	return 0;
}


/*
* AuUDPSend -- UDP protocol send interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
int AuUDPSend(AuSocket* sock, msghdr* msg, int flags){
	SeTextOut("UDP: Send -> %d \r\n", sizeof(UDPHeader));
	if (msg->msg_iovlen > 1) {
		SeTextOut("UDP: Multiple IOV is not supported \r\n");
		return 1;
	}
	if (msg->msg_iovlen == 0)
		return 0;
	if (msg->msg_namelen != sizeof(sockaddr_in)) {
		SeTextOut("UDP: invalid destination address size \r\n");
		return 0;
	}

	if (sock->sessionPort == 0) {
		UDPGetPort(sock);
		SeTextOut("UDP: assigning port %d to socket \r\n", sock->sessionPort);
	}

	sockaddr_in* sockin = (sockaddr_in*)msg->msg_name;
    
	AuVFSNode* nic = AuNetworkRoute(sockin->sin_addr.s_addr);
	if (!nic) {
		SeTextOut("UDP: Failed to route address \r\n");
		return 0;
	}
	AuNetworkDevice* netdev = (AuNetworkDevice*)nic->device;
	if (!netdev) {
		SeTextOut("UDP: No network device found \r\n");
		return 0;
	}

	size_t total_len = sizeof(IPv4Header) + msg->msg_iov[0].iov_len + sizeof(UDPHeader);

	IPv4Header* ipv4 = (IPv4Header*)kmalloc(total_len);
	memset(ipv4, 0, total_len);
	ipv4->totalLength = htons(total_len);
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

	UDPHeader* udp = (UDPHeader*)&ipv4->payload;
	udp->srcPort = htons(sock->sessionPort);
	udp->destPort = sockin->sin_port;
	udp->length = htons(sizeof(UDPHeader) + msg->msg_iov[0].iov_len);
	udp->checksum = 0; 

	memcpy(&udp->payload, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len);

	IPV4SendPacket(ipv4, nic);
	kfree(ipv4);
	return msg->msg_iov[0].iov_len;
}

/*
* AuUDPClose -- UDP protocol close call
* @param sock -- Pointer to socket
*/
void AuUDPClose(AuSocket* sock) {
	return;
}


/*
 * AuUDPBind -- bind a local address to the current
 * socket, it also verifies if there is already a socket
 * with this local address, if not found, then assign this
 * socket with the given local address and assign it to NIC
 * for listening to incoming communication
 */
int AuUDPBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen){
	if (sock->sessionPort != 0)
		return -1;

	SeTextOut("BINDING UDP \r\n");
	sockaddr_in* addr_in = (sockaddr_in*)addr;
	int port = ntohs(addr_in->sin_port);
	SeTextOut("PORT -> %d \r\n", port);
	SeTextOut("UDP Protocol List -> %x \r\n", udp_socket_list);

	for (int i = 0; i < udp_socket_list->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(udp_socket_list, i);
		if (sock->sessionPort == port)
			return -1;
	}
	sock->sessionPort = port;
	list_add(udp_socket_list, sock);
	SeTextOut("UDP Socket added \r\n");
	return 0;
}


uint64_t AuUDPRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len){
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
	SeTextOut("UDP: Socket Closed \r\n");
	return 0;
}
/*
 * CreateUDPSocket -- create a new UDP
 * socket
 */
int CreateUDPSocket() {
	int fd = -1;
	AuThread *thread = AuGetCurrentThread();
	if (!thread)
		return -1;
	AuProcess *proc = AuProcessFindThread(thread);
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
	SeTextOut("UDP Socket created \r\n");
	return fd;
}

/*
 * UDPProtocolInstall -- initialize the UDP socket
 */
void UDPProtocolInstall() {
	udp_socket_list = initialize_list();
}

/*
 * UDPProtocolGetSockList -- returns the socket
 * list
 */
list_t* UDPProtocolGetSockList() {
	return udp_socket_list;
}