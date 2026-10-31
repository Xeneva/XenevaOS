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

#ifndef __UDP_H__
#define __UDP_H__

#include <list.h>
#include <Net/ipv6.h>

#ifdef ARCH_X64
#pragma pack(push,1)
#endif
typedef struct _udpheader_ {
	unsigned short srcPort;
	unsigned short destPort;
	unsigned short length;
	unsigned short checksum;
	unsigned char payload[];
}UDPHeader;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

/*
* CreateUDPSocket -- create a new UDP
* socket
*/
extern int CreateUDPSocket();

/*
 * UDPSocketInstall -- initialize the UDP socket
 */
extern void UDPProtocolInstall();

/*
 * UDPProtocolGetSockList -- returns the socket
 * list
 */
extern list_t* UDPProtocolGetSockList();

extern void UDPHandlePacket(char* packet);
extern void UDPHandlePacket6(IPv6Header* ipv6);

#endif