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
#include <Net/aunet.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <Hal/x86_64_sched.h>
#include <process.h>
#include <stack.h>
#include <Hal/serial.h>
#include <Hal/x86_64_hal.h>
#include <Net/tcp.h>
#include <Net/ipv4.h>
#include <stdio.h>
#include <aucon.h>


list_t* tcpSocketList;

typedef struct _tcpcheckheader_{
	uint32_t source;
	uint32_t destination;
	uint8_t zeros;
	uint8_t protocol;
	uint16_t tcpLen;
	uint8_t tcpHeader[];
}TCPCheckHeader;

/**  Implementation needed **/


/*
 * CalculateTCPChecksum -- calculate tcp checksum 
 * @param p -- Pointer to TCP Checksum header
 * @param h -- Pointer to TCP header
 * @param d -- Payload
 * @param payloadsz -- size of the payload
 */
uint16_t CalculateTCPChecksum(TCPCheckHeader* p, TCPHeader* h, void* d, size_t payloadsz) {
	uint32_t sum = 0;
	uint16_t *s = (uint16_t*)p;

	for (int i = 0; i < 6; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF);
	}

	s = (uint16_t*)h;
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) 
			sum = (sum >> 16) + (sum & 0xFFFF);
	}

	uint16_t dwords = payloadsz / 2;
	s = (uint16_t*)d;
	for (unsigned int i = 0; i < dwords; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) 
			sum = (sum >> 16) + (sum & 0xFFFF);
	}

	if (dwords * static_cast<uint64_t>(2) != payloadsz){
		uint8_t* t = (uint8_t*)d;
		uint8_t tmp[2];
		tmp[0] = t[dwords * sizeof(uint16_t)];
		tmp[1] = 0;

		uint16_t* f = (uint16_t*)tmp;

		sum += ntohs(f[0]);
		if (sum > 0xFFFF)
			sum = (sum >> 16) + (sum & 0xFFFF); 
	}

	return ~(sum & 0xFFFF) & 0xFFFF;
}
/*
 * AuTCPReceive -- TCP protocol receive interface
 * @param sock -- Pointer to socket
 * @param msghdr -- Message header containing every information
 * @param flags -- extra flags
 */
int AuTCPReceive(AuSocket* sock, msghdr *msg, int flags){
	return 0;
}

/*
* AuTCPSend -- TCP protocol send interface
* @param sock -- Pointer to socket
* @param msghdr -- Message header containing every information
* @param flags -- extra flags
*/
int AuTCPSend(AuSocket* sock, msghdr* msg, int flags){
	return 0;
}

/*
* AuTCPClose -- TCP protocol close call
* @param sock -- Pointer to socket
*/
void AuTCPClose(AuSocket* sock) {
	return;
}

static int _tcp_port_ = 49152;
/*
 * AuTCPObtainPort -- obtains a new port for
 * a session, port number 0 - 1024 are reserved
 * for services and protocols like HTTP(port 80)
 * FTP(port 21), SSH(port 22)..etc
 * Rage from 1024 to 49151 are assigned by Internet
 * Assigned Numbers Authority for specific services
 * upon application by a requesting entity.
 * Dynamic/Private ports ranges from 49152 - 65535
 * this ports are not assigned and can be used 
 * dynamically by applications and services
 * @param sock -- Pointer to socket session
 */
void AuTCPObtainPort(AuSocket* sock) {
	int port = _tcp_port_++;
	sock->sessionPort = port;
	list_add(tcpSocketList, sock);
}

/*
 * AuTCPAcknowledge -- send ack packets
 * @param nic -- Pointer to NIC device
 * @param sock -- Pointer to session socket
 * @param ippack -- Pointer to IPv4 packet
 * @param payloadLen -- length of the payload
*/
int AuTCPAcknowledge(AuVFSNode* nic, AuSocket* sock, IPv4Header* ippack, size_t payloadLen) {
	AuNetworkDevice* nicdev = (AuNetworkDevice*)nic->device;
	if (!nicdev)
		return -1;


	/*  need to implement three way ack to handle
	 * packet loss or errors
	 */
	TCPHeader* tcp = (TCPHeader*)&ippack->payload;
	int windowsz = TCP_DEFAULT_WIN_SZ;
	
	unsigned seq_num = sock->packID + 1;
	unsigned ack_num = (tcp->sequenceNum + payloadLen);
	size_t totalLen = sizeof(IPv4Header) + sizeof(TCPHeader);
	IPv4Header* ipv4 = (IPv4Header*)kmalloc(totalLen);
	ipv4->totalLength = htons(totalLen);
	ipv4->destAddress = ippack->srcAddress;
	ipv4->srcAddress = nicdev->ipv4addr;
	ipv4->timeToLive = 64;
	ipv4->protocol = IPV4_PROTOCOL_TCP;
	sock->ipv4Iden++;
	ipv4->identification = htons(sock->ipv4Iden);
	ipv4->flagsFragOffset = htons(0x0);
	ipv4->versionHeaderLen = 0x45;
	ipv4->typeOfService = 0;
	ipv4->headerChecksum = htons(IPv4CalculateChecksum(ipv4));
	
	/* handle fin packets */
	int flags = TCP_FLAGS_ACK;

	TCPHeader* tcppack = (TCPHeader*)&ipv4->payload;
	tcppack->srcPort = htons(sock->sessionPort);
	tcppack->destPort = tcp->srcPort;
	tcppack->sequenceNum = htonl(seq_num);
	tcppack->ackNum = htonl(ack_num);
	tcppack->dataOffsetFlags = htons(flags | 0x5000);
	tcppack->window = htons(windowsz);
	tcppack->checksum = 0;
	tcppack->urgentPointer = 0;

	TCPCheckHeader checkhdr;
	checkhdr.source = ipv4->srcAddress;
	checkhdr.destination = ipv4->destAddress;
	checkhdr.zeros = 0;
	checkhdr.protocol = IPV4_PROTOCOL_TCP;
	checkhdr.tcpLen = htons(sizeof(TCPHeader));

	tcppack->checksum = htons(CalculateTCPChecksum(&checkhdr, tcppack, NULL, 0));
	IPV4SendPacket(ipv4, nic);
	SeTextOut("TCP-ACK Sent !! successfully \r\n");
	kfree(ipv4);
	return 0;
}
/*
* AuTCPConnect -- TCP protocol connect interface
* @param sock -- Pointer to socket
* @param addr -- socket address information
* @param addrlen -- address length
*/
int AuTCPConnect(AuSocket* sock, sockaddr* addr, socklen_t addrlen){
	x64_cli();
	sockaddr_in* sockdata = (sockaddr_in*)addr;

	AuTCPObtainPort(sock);
	int sourcePort = sock->sessionPort;
	sock->sockState = SOCK_STATE_WAITING_FOR_CONNECTION;
	sock->ipv4Iden = rand();
	AuVFSNode* nic = AuNetworkRoute(sockdata->sin_addr.s_addr);
	if (!nic) {
		SeTextOut("[Aurora-net]: TCP failed to connect, no NIC\r\n");
		return 1;
	}

	AuNetworkDevice* ndev = (AuNetworkDevice*)nic->device;
	if (!ndev) {
		SeTextOut("[Aurora-net]: TCP No Netword device data \r\n");
		return 1;
	}

	size_t totalLen = sizeof(IPv4Header) + sizeof(TCPHeader);

	/* IPV4 Header */
	IPv4Header* ipv4 = (IPv4Header*)kmalloc(totalLen);
	ipv4->totalLength = htons(totalLen);
	ipv4->destAddress = sockdata->sin_addr.s_addr;
	ipv4->srcAddress = ndev->ipv4addr;
	ipv4->timeToLive = 64;
	ipv4->protocol = IPV4_PROTOCOL_TCP;
	ipv4->identification = htons(sock->ipv4Iden);
	ipv4->flagsFragOffset = htons(0x0);
	ipv4->versionHeaderLen = 0x45;
	ipv4->typeOfService = 0;
	ipv4->headerChecksum = htons(IPv4CalculateChecksum(ipv4));

	TCPHeader* tcp = (TCPHeader*)&ipv4->payload;
	tcp->srcPort = htons(sock->sessionPort);
	tcp->destPort = sockdata->sin_port;
	tcp->sequenceNum = 0;
	tcp->ackNum = 0;
	tcp->dataOffsetFlags = htons((1 << 1) | 0x5000);
	tcp->window = htons(TCP_DEFAULT_WIN_SZ);
	tcp->checksum = 0;
	tcp->urgentPointer = 0;
	sock->packID = tcp->sequenceNum;

	TCPHeader* tcpch = (TCPHeader*)ipv4->payload;
	SeTextOut("TCP Check SRC -> %d \r\n", tcpch->srcPort);

	TCPCheckHeader checkhdr;
	checkhdr.source = ipv4->srcAddress;
	checkhdr.destination = ipv4->destAddress;
	checkhdr.zeros = 0;
	checkhdr.protocol = IPV4_PROTOCOL_TCP;
	checkhdr.tcpLen = htons(sizeof(TCPHeader));

	tcp->checksum = htons(CalculateTCPChecksum(&checkhdr, tcp, NULL, 0));
	
	IPV4SendPacket(ipv4, nic);

	uint64_t s, ss;
	uint64_t ns, nss;
	x86_64_calculate_ticks(1, 0, &s, &ss);
	int attempts = 0;
	while (!sock->rxstack->itemCount) {
		AuSleepThread(AuGetCurrentThread(), 100000);
		AuForceScheduler();
		x86_64_calculate_ticks(0, 0, &ns, &nss);
		//connection refused
		if (sock->sockState == SOCK_STATE_CONNECTION_RST) {
			SeTextOut("Sock state reset \r\n");
			kfree(ipv4);
			return -1;
		}
		if ((ns > s || (ns == s && nss > ss))) {
			if (attempts == 3) {
				SeTextOut("Attempts == 3 \r\n");
				kfree(ipv4);
				return -1; //timeout
			}
			SeTextOut("[aurora]: TCP retrying connection \r\n");
			IPV4SendPacket(ipv4, nic);
			x86_64_calculate_ticks(1, 0, &s, &ss);
			attempts++;
		}
	}
	kfree(ipv4);
	SeTextOut("[aurora]: TCP connection succeeded \r\n");
	return 0;
}


int AuTCPBind(AuSocket* sock, sockaddr* addr, socklen_t addrlen){
	return 0;
}


uint64_t AuTCPRead(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len){
	return 0;
}

uint64_t AuTCPWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	return 0;
}

int AuTCPFileClose(AuVFSNode* fsys, AuVFSNode* file) {
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
	SeTextOut("TCP/IP Socket Closed \r\n");
	return 0;
}
/*
 * CreateTCPSocket -- creates a new TCP Socket
 */
int CreateTCPSocket() {
	int fd = -1;
	AuThread *thread = AuGetCurrentThread();
	if (!thread)
		return -1;
	AuProcess *proc = AuProcessFindThread(thread);
	if (!proc) 
		proc = AuProcessFindSubThread(thread);
		if (!proc)
			return -1;
	AuSocket *sock = (AuSocket*)kmalloc(sizeof(AuSocket));
	memset(sock, 0, sizeof(AuSocket));
	sock->send = AuTCPSend;
	sock->receive = AuTCPReceive;
	sock->connect = AuTCPConnect;
	sock->bind = AuTCPBind;
	sock->close = AuTCPClose;
	sock->rxstack = AuStackCreate();
	fd = AuProcessGetFileDesc(proc);
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	node->flags |= FS_FLAG_SOCKET;
	node->device = sock;
	node->close = AuTCPFileClose;
	node->iocontrol = SocketIOControl;
	proc->fds[fd] = node;
	SeTextOut("TCP Socket created \r\n");
	return fd;
}

/*
 * TCPGetSocketList -- return the current socket
 * list of TCP
 */
list_t* TCPGetSocketList() {
	return tcpSocketList;
}

/*
 * TCPProtocolInstall -- initialize the TCP protocol
 */
void TCPProtocolInstall() {
	tcpSocketList = initialize_list();
}