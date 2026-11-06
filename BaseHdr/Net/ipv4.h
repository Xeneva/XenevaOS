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

#ifndef __IPV4_H__
#define __IPV4_H__

#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(push,1)
#endif
typedef struct _ipv4head_ {
	unsigned char versionHeaderLen;
	unsigned char typeOfService;
	unsigned short totalLength;
	unsigned short identification;
	unsigned short flagsFragOffset;
	unsigned char timeToLive;
	unsigned char protocol;
	unsigned short headerChecksum;
	unsigned srcAddress;
	unsigned destAddress;
	uint8_t payload[];
}IPv4Header;
#if defined(ARCH_X64) || defined(ARCH_ARM64)
#pragma pack(pop)
#endif

#define IPV4_PROTOCOL_UDP 17
#define IPV4_PROTOCOL_TCP 6



extern uint16_t IPv4CalculateChecksum(IPv4Header* p);

extern void ip_ntoa(const uint32_t src);
/*
* CreateIPv4Socket -- create a new ipv4 socket
* @param type -- type of the socket its Datagram or
* stream socket
* @param protocol -- protocol number
*/
extern int CreateIPv4Socket(int type, int protocol);

/*
 * IPv4HandlePacket -- IPv4 Handle Packet 
 * @param data -- Pointer to IPv4 packet
 * @param nic -- Pointer to Network Card
 */
extern void IPv4HandlePacket(void* data,AuVFSNode* nic);

/*
 * IPV4SendPacket -- sends a packet to next stage
 * @param packet -- IPv4 packet to send
 * @param nic -- Pointer to NIC device
 */
extern void IPV4SendPacket(IPv4Header* packet, AuVFSNode* nic);
#endif