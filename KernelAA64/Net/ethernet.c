/**
* @file ethernet.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include <Net/ethernet.h>
#include <Net/aunet.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <Drivers/uart.h>
#include <Fs/Dev/devfs.h>
#include <aucon.h>
#include <Net/arp.h>
#include <Net/ipv4.h>
#include <Net/ipv6.h>
#include <Net/udp.h>
#include <Net/socket.h>
#include <Hal/AA64/profile.h>

#pragma pack(push, 1)
ALIGNED(2) typedef struct _ethernet_ {
	uint8_t dest[6];
	uint8_t src[6];
	uint16_t typeLen;
	uint8_t payload[];
} Ethernet;
#pragma pack(pop)

/* Raw sockets are observability taps, not a reliable transport. Without a
 * bound, a busy link grows every tap's queue until the heap is exhausted. */
#define AU_RAW_SOCKET_MAX_FRAMES 64

AU_EXTERN AU_EXPORT void AuEthernetHandle(void* data, int size, AuVFSNode* nic) {
	Ethernet* frame = (Ethernet*)data;
	AuNetworkDevice* ndev = (AuNetworkDevice*)nic->device;
	if (!ndev)
		return;
	list_t* raw_sockets = AuRawSocketGetList();
	//AU_NET_LOG("Adding to raw_sockets \r\n");
	for (int i = 0; i < raw_sockets->pointer; i++) {
		AuSocket* sock = (AuSocket*)list_get_at(raw_sockets, i);
		if (sock && sock->rxstack &&
			sock->rxstack->itemCount < AU_RAW_SOCKET_MAX_FRAMES)
			AuSocketAdd(sock, frame, size);
	}

	char broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	if (!memcmp(frame->dest, ndev->mac, 6) || !memcmp(frame->dest, broadcast_mac, 6)) {
		switch (ntohs(frame->typeLen)) {
		case ETHERNET_TYPE_ARP:
			ARPHandlePacket((void*)&frame->payload, nic);
			break;
		case ETHERNET_TYPE_IPV4:
			IPv4HandlePacket((void*)&frame->payload, nic);
			break;
		case ETHERNET_TYPE_IPV6:
			IPv6HandlePacket((void*)&frame->payload, nic);
			break;
		}
	}
}

#ifdef ARCH_X64
#pragma pack(push, 1)
#endif

typedef struct _dns_ {
	uint16_t qid;
	uint16_t flags;
	uint16_t questions;
	uint16_t answers;
	uint16_t authorities;
	uint16_t additional;
	uint8_t data[];
} DNSPacket;
#ifdef ARCH_X64
#pragma pack(pop)
#endif
/**
 * @brief AuEthernetSend -- sends a packet to ethernet layer
 * @param data -- data to send
 * @param len -- length of the data
 * @param type -- type
 * @param dest -- destination mac address
 */
void AuEthernetSend(AuVFSNode* nic, void* data, size_t len, uint16_t type, uint8_t* dest) {
	AuNetworkDevice* ndev = (AuNetworkDevice*)nic->device;
	if (!ndev)
		return;
	/* Acceptance: loopback / local delivery must never hit L2. */
	if (ndev->type == NETDEV_TYPE_LOOPBACK) {
		AU_NET_LOG("[aurora]: AuEthernetSend blocked on lo\r\n");
		return;
	}
	size_t totalSz = sizeof(Ethernet) + len;
	Ethernet* pacl = (Ethernet*)kmalloc(totalSz);
	/* Every header field and the payload are written below; the previous
	 * full-frame memset only added a pass over the data. */
	memcpy(&pacl->payload, data, len);
	memcpy(&pacl->dest, dest, 6);
	uint8_t* src_mac = ndev->mac;
	memcpy(&pacl->src, src_mac, 6);
	pacl->typeLen = htons(type);
	if (nic->write)
		nic->write(nic, nic, (uint64_t*)pacl, totalSz);

	kfree(pacl);
}