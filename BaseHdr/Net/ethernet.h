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

#ifndef __ETHERNET_H__
#define __ETHERNET_H__

#include <stdint.h>
#include <aurora.h>
#include <Fs/vfs.h>


#define ETHERNET_TYPE_IPV4  0x0800
#define ETHERNET_TYPE_ARP   0x0806
#define ETHERNET_TYPE_WAKE_ON_LAN  0x0842
#define ETHERNET_TYPE_AVTP  0x22F0
#define ETHERNET_TYPE_IETF_TRILL_PROTOCOL 0x22F3
#define ETHERNET_TYPE_STREAM_RESV_PROTOCOL 0x22EA
#define ETHERNET_TYPE_IPV6 0x86DD


/*
* AuEthernetSend -- sends a packet to ethernet layer
* @param data -- data to send
* @param len -- length of the data
* @param type -- type
* @param dest -- destination mac address
*/
extern void AuEthernetSend(AuVFSNode* nic,void* data, size_t len, uint16_t type, uint8_t* dest);

AU_EXTERN AU_EXPORT void AuEthernetHandle(void *frame, int size, AuVFSNode* nic);


#endif