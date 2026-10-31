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
#include <Net/tcp.h>
#include <Net/icmp.h>
#include <Net/ipv4.h>
#include <Net/udp.h>
#include <Net/aunet.h>
#include <Net/ethernet.h>
#include <Net/arp.h>
#include <Net/udp.h>
#include <_null.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/serial.h>
#include <aucon.h>
#include <Mm/kmalloc.h>

uint16_t IPv4CalculateChecksum(IPv4Header * p){
	uint32_t sum = 0;
	uint16_t *s = (uint16_t*)p;
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF){
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}
	return ~(sum & 0xFFFF) & 0xFFFF;
}

void ip_ntoa(const uint32_t src) {
	SeTextOut("%d.%d.%d.", ((src & 0xFF000000) >> 24),
		((src & 0xFF0000) >> 16),
		((src & 0xFF00) >> 8),
		((src & 0xFF)));
	SeTextOut("%d \r\n", (src & 0xFF));
}

void IPv4HandlePacket(void* data, AuVFSNode* nic) {
	char dest[16];
	char src[16];
	IPv4Header* pack = (IPv4Header*)data;
	ip_ntoa(ntohl(pack->destAddress));
	ip_ntoa(ntohl(pack->srcAddress)); 
	switch (pack->protocol){
	case 1: {
		SeTextOut("ICMP Message \r\n");
		AuICMPHandle(pack, nic);
		break;
	}
	case IPV4_PROTOCOL_UDP:{
							   uint16_t destport = ntohs(((uint16_t*)&pack->payload)[1]);
							   SeTextOut("UDP Packet received dest port -> %d \r\n", destport);
							   list_t* udpsocklist = UDPProtocolGetSockList();
							   for (int i = 0; i < udpsocklist->pointer; i++) {
								   AuSocket* sock = (AuSocket*)list_get_at(udpsocklist, i);
								   if (sock->sessionPort == destport) {
									   SeTextOut("UDP Packet adding to sock -> %d sz -> %d \r\n", sock->sessionPort,
										   ntohs(pack->totalLength));
									   AuSocketAdd(sock, data, ntohs(pack->totalLength));
									   break;
								   }
							   }
							   break;
	}
	case IPV4_PROTOCOL_TCP: {
		SeTextOut("IPv4 : received TCP packet \r\n");
		TCPHeader* tcp = (TCPHeader*)&pack->payload;
		uint16_t destPort = ntohs(tcp->destPort);
		uint16_t srcPort = ntohs(tcp->srcPort);
		list_t* tcplist = TCPGetSocketList();
		for (int i = 0; i < tcplist->pointer; i++) {
			AuSocket* sock = (AuSocket*)list_get_at(tcplist, i);
			if (sock->sessionPort == destPort) {
				SeTextOut("[IPv4]: TCP has an waiting socket \r\n");

				/*connection establishing */
				if (sock->sockState == SOCK_STATE_WAITING_FOR_CONNECTION) {
					/* SYN-ACK */
					if ((ntohs(tcp->dataOffsetFlags) & (TCP_FLAGS_SYN | TCP_FLAGS_ACK))) {
						SeTextOut("[IPv4]: TCP SynAck \r\n");
						/*now we need to send ack, to complete 
						 * three-way handshake
						 */
						if (AuTCPAcknowledge(nic, sock, pack, 1)) {
							SeTextOut("[IPv4]: failed to send TCP synack \r\n");
							break;
						}
						AuSocketAdd(sock, pack, ntohs(pack->totalLength));
					}
				}
				/* TODO: Other events like RST and DATA-ACK */
			}
		}
		break;
	}
	}
}

/*
 * CreateIPv4Socket -- create a new ipv4 socket
 * @param type -- type of the socket its Datagram or
 * stream socket
 * @param protocol -- protocol number 
 */
int CreateIPv4Socket(int type, int protocol) {
	switch (type) {
	case SOCK_DGRAM:
		if (protocol == 0 || protocol == IPPROTOCOL_UDP)
			return CreateUDPSocket();
		if (protocol == IPPROTOCOL_ICMP)
			return CreateICMPSocket();
	case SOCK_STREAM:
		return CreateTCPSocket();
	default:
		return -1;
	}
}

/*
 * IPV4SendPacket -- sends a packet to next stage
 * @param packet -- IPv4 packet to send
 * @param nic -- Pointer to NIC device
 */
void IPV4SendPacket(IPv4Header* packet, AuVFSNode* nic) {
	AuNetworkDevice* ndev = (AuNetworkDevice*)nic->device;
	if (!ndev)
		return;

	uint32_t ip_dest = packet->destAddress;
	
	SeTextOut("IPV4 Sending %x\r\n", nic->device);
	
	/* Decide which data link layer to use for
	   forwarding this packet*/
	if (ndev->type == NETDEV_TYPE_ETHERNET) {
		AuARPCache* cache = NULL;
		if (!ndev->ipv4subnet || ((ip_dest & ndev->ipv4subnet) != (ndev->ipv4addr & ndev->ipv4subnet))) {
			ip_dest = ndev->ipv4gateway;
			ip_ntoa(ip_dest);
			cache = AuARPGet(ip_dest);
			if (!cache) {
				AuARPRequestMAC(nic, ip_dest);
				SeTextOut("Requesting MAC \r\n");

				/* Not implemented yet */
				AuSleepThread(AuGetCurrentThread(), 100);
				AuForceScheduler();

				cache = AuARPGet(ip_dest);
			}
		}
		else {
			cache = AuARPGet(ip_dest);
			if (!cache) {
				AuARPRequestMAC(nic, ip_dest);
				SeTextOut("Requesting MAC \r\n");
				
				/* Not implemented yet */
				AuSleepThread(AuGetCurrentThread(), 10000);
				AuForceScheduler();

				cache = AuARPGet(ip_dest);
			}
		}
		uint8_t broadcast_addr[6];
		memset(broadcast_addr, 0xFF, 6);
		AuEthernetSend(nic, packet, ntohs(packet->totalLength), ETHERNET_TYPE_IPV4, cache ? cache->hw_address : broadcast_addr);
	}
}