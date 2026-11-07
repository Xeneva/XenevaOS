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

#include <Net/aunet.h>
#include <Net/arp.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <_null.h>
#include <hashmap.h>
#include <Fs/dev/devfs.h>
#include <Net/socket.h>
#include <Net/route.h>
#include <Net/udp.h>
#include <Net/icmp.h>
#include <Net/tcp.h>

hashmap_t* netadapters;

/*
 * AuInitialiseNet -- initialise network data structures
 */
void AuInitialiseNet() {
	netadapters = AuHashmapCreate(10);
	AuVFSNode* fs = AuVFSFind("/dev");
	AuDevFSCreateFile(fs, "/dev/net", FS_FLAG_DIRECTORY);
	AuSocketInstall();
	AuRouteTableInitialise();
	/* ARP Protocol for Ethernet devices */
	ARPProtocolInitialise();
	UDPProtocolInstall();
	ICMPInitialise();
	TCPProtocolInstall();
}


/*
 * AuAddNetAdapter -- add a net adapter to the adapter
 * list
 */
void AuAddNetAdapter(AuVFSNode* netfs, char* name) {
	AuHashmapSet(netadapters, name, netfs);

	AuVFSNode* fs = AuVFSFind("/dev");
	AuDevFSAddFile(fs, "/dev/net", netfs);
}

/*
 * AuGetNetworkAdapter -- get a network adapter 
 * @param name -- adapter name
 */
AuVFSNode* AuGetNetworkAdapter(char* name) {
	AuVFSNode* node = (AuVFSNode*)AuHashmapGet(netadapters, name);
	return node;
}

int AuAddrIsLocal4(uint32_t address) {
	if ((address & MAKE_IP(255, 0, 0, 0)) == MAKE_IP(127, 0, 0, 0))
		return 1;
	if ((address & 0xFF000000u) == 0x7F000000u)
		return 1;
	return 0;
}

int AuAddrIsLocal6(const ip6_addr* address) {
	int i;
	if (!address)
		return 0;
	for (i = 0; i < 15; i++) {
		if (address->s6_addr[i] != 0)
			return 0;
	}
	return address->s6_addr[15] == 1;
}

void AuNetAddConnectedRoute4(AuVFSNode* nic, const char* ifname) {
	(void)nic;
	(void)ifname;
}

void AuNetAddConnectedRoute6(AuVFSNode* nic, const char* ifname) {
	(void)nic;
	(void)ifname;
}

void AuNetAddDefaultRoute4(AuVFSNode* nic, const char* ifname) {
	(void)nic;
	(void)ifname;
}

void AuNetAddDefaultRoute6(AuVFSNode* nic, const char* ifname) {
	(void)nic;
	(void)ifname;
}

AuVFSNode* AuNetworkRoute(uint32_t address) {
	AuRouteResult rr;

	if (AuAddrIsLocal4(address))
		return AuGetNetworkAdapter("lo");
	if (AuRouteLookup4(address, &rr) == 0 && rr.nic)
		return rr.nic;
	/* x86 stub: keep e1000 fallback so old images still boot. */
	return AuGetNetworkAdapter("e1000");
}

AuVFSNode* AuNetworkRoute6(const ip6_addr* address) {
	(void)address;
	return AuGetNetworkAdapter("e1000");
}

