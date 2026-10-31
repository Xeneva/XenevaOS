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

#ifndef __ICMP_H__
#define __ICMP_H__

#include <Net/ipv4.h>
#include <Fs/vfs.h>

#ifdef ARCH_X64
#pragma pack(push,1)
#endif
typedef struct _icmp_head_ {
	unsigned char type;
	unsigned char code;
	unsigned short checksum;
	unsigned short identifier;
	unsigned short sequenceNum;
	unsigned char payload[];
}ICMPHeader;
#ifdef ARCH_X64
#pragma pack(pop)
#endif

 /*
 * ICMPInitialise -- initialise ICMP
 */
extern void ICMPInitialise();
/*
 * AuICMPHandle -- ICMP handler
 */
extern void AuICMPHandle(IPv4Header* ipv4, AuVFSNode* nic);

/*
 * AuICMPSendDestUnreachable -- RFC 792 type 3 (code as given).
 * Includes original IP header + 8 bytes of payload.
 */
extern void AuICMPSendDestUnreachable(IPv4Header* orig, AuVFSNode* nic, uint8_t code);
/*
* CreateICMPSocket -- create a new Internet
* Control Message Protocol (ICMP) protocol
*/
extern int CreateICMPSocket();

#endif