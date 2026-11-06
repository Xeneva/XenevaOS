/**
* @file arp.c
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

#include <Net/arp.h>
#include <Net/ethernet.h>
#include <Net/aunet.h>
#include <string.h>
#include <_null.h>
#include <aucon.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>
#include <Net/ipv4.h>
#include <Hal/AA64/sched.h>

list_t* arp_list;

/**
 * @brief ARPProtocolInitialise -- initialise arp protocol
 */
void ARPProtocolInitialise() {
	arp_list = initialize_list();
}

/**
 * @brief ARPProtocolAdd -- add a new information to ARP cache list
 * @param nic -- Pointer to NIC device
 * @param address -- IP Address
 * @param hwaddr -- Hardware address to add
 */
void ARPProtocolAdd(AuVFSNode* nic, uint32_t address, uint8_t* hwaddr) {
	AuARPCache* arpcache = (AuARPCache*)kmalloc(sizeof(AuARPCache));
	memset(arpcache, 0, sizeof(AuARPCache));
	memcpy(arpcache->hw_address, hwaddr, 6);
	arpcache->ipAddress = address;
	arpcache->nic = nic;
	list_add(arp_list, arpcache);
}

/**
 * @brief AuARPGet -- Returns an ARP by its ip address
 * @param address -- IP Address to look
 */
AuARPCache* AuARPGet(uint32_t address) {
	int i;
	for (i = 0; i < arp_list->pointer; i++) {
		AuARPCache* arp = (AuARPCache*)list_get_at(arp_list, i);
		if (arp->ipAddress == address)
			return arp;
	}
	return NULL;
}

AuARPCache* AuARPResolve(AuVFSNode* nic, uint32_t addr) {
	AuARPCache* cache;
	int n;

	cache = AuARPGet(addr);
	if (cache)
		return cache;
	if (!nic)
		return NULL;
	AuARPRequestMAC(nic, addr);
	for (n = 0; n < 50; n++) {
		AuNetRxPoll();
		cache = AuARPGet(addr);
		if (cache)
			return cache;
		AuSleepThread(AuGetCurrentThread(), 1);
		AuScheduleNext();
	}
	return NULL;
}
/**
 * @brief AuARPRequestMAC -- request a mac address from
 * server
 */
void AuARPRequestMAC(AuVFSNode* nic, uint32_t addr) {
	AuNetworkDevice* ndev = (AuNetworkDevice*)nic->device;
	if (!ndev)
		return;
	NetARP* arp = (NetARP*)kmalloc(sizeof(NetARP));
	memset(arp, 0, sizeof(NetARP));
	arp->hwAddressType = htons(1); //0x0100;
	arp->hwProtocolType = htons(ETHERNET_TYPE_IPV4);
	arp->hwAddressSize = 6;
	arp->protocolSize = 4;
	arp->operation = htons(1);

	arp->arp_data.arp_eth_ipv4.arp_tpa = addr;
	memcpy(arp->arp_data.arp_eth_ipv4.arp_sha, ndev->mac, 6);

	if (ndev->ipv4addr) {
		arp->arp_data.arp_eth_ipv4.arp_spa = ndev->ipv4addr;
	}

	AU_NET_LOG("[aurora]: ARP Requesting MAC \r\n");
	uint8_t broadcast_mac[6];
	memset(broadcast_mac, 0xFF, 6);
	AuEthernetSend(nic, arp, sizeof(NetARP), ETHERNET_TYPE_ARP, broadcast_mac);
	/* here we need to call different interfaces depending
	 * on the nic node passed, ARP can be sent through different
	 * link layer other than Ethernet
	 */
	kfree(arp);
}

void ARPHandlePacket(void* data, AuVFSNode* nic) {
	AuNetworkDevice* eth = (AuNetworkDevice*)nic->device;
	if (!eth)
		return;
	NetARP* packet = (NetARP*)data;
	if (ntohs(packet->hwAddressType) == 1 && ntohs(packet->hwProtocolType) == ETHERNET_TYPE_IPV4) {
		if (packet->arp_data.arp_eth_ipv4.arp_spa) {
			if (!AuARPGet(packet->arp_data.arp_eth_ipv4.arp_spa)) {
				//AU_NET_LOG("[aurora]: net ARP added \r\n");
				ARPProtocolAdd(nic,
							   packet->arp_data.arp_eth_ipv4.arp_spa,
							   packet->arp_data.arp_eth_ipv4.arp_sha);
			}
		}
	}
	if (ntohs(packet->operation) == 1) {
		ip_ntoa(ntohl(packet->arp_data.arp_eth_ipv4.arp_spa));
		uint32_t tpa = 0;
		memcpy(&tpa, &packet->arp_data.arp_eth_ipv4.arp_tpa, 4);

		ip_ntoa(ntohl(tpa));
		if (eth->ipv4addr && tpa == eth->ipv4addr) {
			NetARP arp;
			arp.hwAddressType = htons(1);
			arp.hwProtocolType = htons(ETHERNET_TYPE_IPV4);
			arp.hwAddressSize = 6;
			arp.protocolSize = 4;
			arp.operation = htons(2);
			memcpy(arp.arp_data.arp_eth_ipv4.arp_sha, eth->mac, 6);
			memcpy(arp.arp_data.arp_eth_ipv4.arp_tha, packet->arp_data.arp_eth_ipv4.arp_sha, 6);

			//arp.arp_data.arp_eth_ipv4.arp_spa = eth->ipv4addr;
			memcpy(&arp.arp_data.arp_eth_ipv4.arp_spa, &eth->ipv4addr, 4);
			//arp.arp_data.arp_eth_ipv4.arp_tpa = packet->arp_data.arp_eth_ipv4.arp_spa;
			memcpy(&arp.arp_data.arp_eth_ipv4.arp_tpa, &packet->arp_data.arp_eth_ipv4.arp_spa, 4);

			uint32_t tpa, spa = 0;
			memcpy(&tpa, &arp.arp_data.arp_eth_ipv4.arp_tpa, 4);
			memcpy(&spa, &arp.arp_data.arp_eth_ipv4.arp_spa, 4);

			AuEthernetSend(nic,
						   &arp,
						   sizeof(NetARP),
						   ETHERNET_TYPE_ARP,
						   packet->arp_data.arp_eth_ipv4.arp_sha);
		}
	} else if (ntohs(packet->operation) == 2) {
		char spa[17];
		ip_ntoa(ntohl(packet->arp_data.arp_eth_ipv4.arp_spa));
	}
}
