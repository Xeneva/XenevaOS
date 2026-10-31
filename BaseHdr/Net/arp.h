/**
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

#ifndef __ARP_H__
#define __ARP_H__

#include <stdint.h>
#include <Fs/vfs.h>

#define ARP_OPERATION_REQUEST 0x0100
#define ARP_OPERATION_RESPONSE 0x0200


#pragma pack(push,1)
typedef struct _arp_ {
	uint16_t hwAddressType;
	uint16_t hwProtocolType;
	uint8_t hwAddressSize;
	uint8_t protocolSize;
	uint16_t operation;
	union {
		struct {
			uint8_t arp_sha[6];
			uint32_t arp_spa;
			uint8_t arp_tha[6];
			uint32_t arp_tpa;
		}arp_eth_ipv4;
	}arp_data;
}NetARP;
#pragma pack(pop)

typedef struct _arp_cache_ {
	uint8_t hw_address[6];
	uint16_t flags;
	uint32_t ipAddress;
	AuVFSNode* nic;
}AuARPCache;


/*
 * ARPProtocolInitialise -- initialise arp protocol
 */
extern void ARPProtocolInitialise();

/*
 * ARPProtocolAdd -- add a new information to ARP cache list
 * @param nic -- Pointer to NIC device
 * @param address -- IP Address
 * @param hwaddr -- Hardware address to add
 */
extern void ARPProtocolAdd(AuVFSNode* nic, uint32_t address, uint8_t* hwaddr);

/*
 * AuARPGet -- Returns an ARP by its ip address
 * @param address -- IP Address to look
 */
extern AuARPCache* AuARPGet(uint32_t address);

/*
 * AuARPResolve -- ARP for addr and wait for a reply (poll RX).
 * Returns NULL if no reply. Does not send IP to the Ethernet broadcast.
 */
extern AuARPCache* AuARPResolve(AuVFSNode* nic, uint32_t addr);

/*
 * AuARPRequestMAC -- request a mac address from
 * server
 */
extern void AuARPRequestMAC(AuVFSNode* nic, uint32_t addr);

/*
 * ARPHandlePacket -- handle incoming ARP packet
 * @param data -- Pointer to ARP packet
 * @param nic -- Pointer to Network Device file
 */
extern void ARPHandlePacket(void* data, AuVFSNode* nic);

#endif