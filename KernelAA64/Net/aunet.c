/**
* @file aunet.c
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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
#include <Net/ndp.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <_null.h>
#include <hashmap.h>
#include <Fs/Dev/devfs.h>
#include <Net/socket.h>
#include <Net/route.h>
#include <Net/netfilter.h>
#include <Net/udp.h>
#include <Net/icmp.h>
#include <Net/icmpv6.h>
#include <Net/tcp.h>
#include <aucon.h>
#include <Drivers/uart.h>

static hashmap_t* netadapters;
static void (*_net_rx_poll)(void);
static int _net_rx_depth;

void AuNetRegisterRxPoll(void (*fn)(void)) {
	_net_rx_poll = fn;
}

void AuNetRxPoll(void) {
	if (_net_rx_poll) {
		_net_rx_depth++;
		_net_rx_poll();
		/* Only the outermost drain flushes: nested calls would send the
		 * ACK before the ring has been emptied. */
		_net_rx_depth--;
	}
	if (_net_rx_depth == 0)
		AuTCPFlushDeferredAcks();
}

/* Global DNS nameserver table (independent of SO_BINDTODEVICE). */
static uint32_t g_dns4[AU_DNS_MAX4];
static ip6_addr g_dns6[AU_DNS_MAX6];

void AuDnsAddServer4(uint32_t addr) {
	int i;

	if (!addr)
		return;
	for (i = 0; i < AU_DNS_MAX4; i++) {
		if (g_dns4[i] == addr)
			return;
	}
	for (i = 0; i < AU_DNS_MAX4; i++) {
		if (g_dns4[i] == 0) {
			g_dns4[i] = addr;
			return;
		}
	}
}

void AuDnsSetServer4(int index, uint32_t addr) {
	if (index < 1 || index > AU_DNS_MAX4)
		return;
	g_dns4[index - 1] = addr;
}

int AuDnsGetServer4(int index, uint32_t* out) {
	if (!out || index < 1 || index > AU_DNS_MAX4)
		return 1;
	*out = g_dns4[index - 1];
	return 0;
}

void AuDnsAddServer6(const ip6_addr* addr) {
	int i;

	if (!addr || ip6_addr_is_zero(addr))
		return;
	for (i = 0; i < AU_DNS_MAX6; i++) {
		if (ip6_addr_equal(&g_dns6[i], addr))
			return;
	}
	for (i = 0; i < AU_DNS_MAX6; i++) {
		if (ip6_addr_is_zero(&g_dns6[i])) {
			ip6_addr_copy(&g_dns6[i], addr);
			return;
		}
	}
}

void AuDnsSetServer6(int index, const ip6_addr* addr) {
	if (!addr || index < 1 || index > AU_DNS_MAX6)
		return;
	ip6_addr_copy(&g_dns6[index - 1], addr);
}

int AuDnsGetServer6(int index, ip6_addr* out) {
	if (!out || index < 1 || index > AU_DNS_MAX6)
		return 1;
	ip6_addr_copy(out, &g_dns6[index - 1]);
	return 0;
}

static size_t AuLoopbackWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	(void)node;
	(void)file;
	(void)buffer;
	(void)length;
	/* IP local delivery must never reach L2 write on lo. */
	AU_NET_LOG("[aurora]: lo write ignored (local path should skip L2)\r\n");
	return 0;
}

static int AuLoopbackIOCtl(AuVFSNode* file, int code, void* arg) {
	AuNetworkDevice* ndev;

	if (!file || !file->device || !arg)
		return 1;
	ndev = (AuNetworkDevice*)file->device;
	switch (code) {
	case AUNET_GET_HARDWARE_ADDRESS:
		memcpy(arg, ndev->mac, 6);
		return 0;
	case AUNET_GET_IPV4_ADDRESS:
		memcpy(arg, &ndev->ipv4addr, sizeof(ndev->ipv4addr));
		return 0;
	case AUNET_SET_IPV4_ADDRESS:
		memcpy(&ndev->ipv4addr, arg, sizeof(ndev->ipv4addr));
		return 0;
	case AUNET_GET_GATEWAY_ADDRESS:
		memcpy(arg, &ndev->ipv4gateway, sizeof(ndev->ipv4gateway));
		return 0;
	case AUNET_SET_GATEWAY_ADDRESS:
		memcpy(&ndev->ipv4gateway, arg, sizeof(ndev->ipv4gateway));
		return 0;
	case AUNET_GET_SUBNET_MASK:
		memcpy(arg, &ndev->ipv4subnet, sizeof(ndev->ipv4subnet));
		return 0;
	case AUNET_SET_SUBNET_MASK:
		memcpy(&ndev->ipv4subnet, arg, sizeof(ndev->ipv4subnet));
		return 0;
	case AUNET_GET_LINK_STATUS:
		memcpy(arg, &ndev->linkStatus, sizeof(ndev->linkStatus));
		return 0;
	case AUNET_GET_IPV6_ADDRESS:
		memcpy(arg, &ndev->ipv6addr, sizeof(ndev->ipv6addr));
		return 0;
	case AUNET_SET_IPV6_ADDRESS:
		memcpy(&ndev->ipv6addr, arg, sizeof(ndev->ipv6addr));
		return 0;
	case AUNET_GET_IPV6_GATEWAY:
		memcpy(arg, &ndev->ipv6gateway, sizeof(ndev->ipv6gateway));
		return 0;
	case AUNET_SET_IPV6_GATEWAY:
		memcpy(&ndev->ipv6gateway, arg, sizeof(ndev->ipv6gateway));
		return 0;
	case AUNET_GET_IPV6_PREFIX:
		memcpy(arg, &ndev->ipv6prefixLen, sizeof(ndev->ipv6prefixLen));
		return 0;
	case AUNET_SET_IPV6_PREFIX:
		memcpy(&ndev->ipv6prefixLen, arg, sizeof(ndev->ipv6prefixLen));
		return 0;
	default:
		return 1;
	}
}

static void AuNetSeedLoopbackRoutes(void) {
	AuRouteEntry* r4;
	AuRouteEntry6* r6;
	char* name4;
	char* name6;

	r4 = AuRouteTableCreateEntry();
	if (r4) {
		name4 = (char*)kmalloc(3);
		if (name4) {
			strcpy(name4, "lo");
			r4->ifname = name4;
			r4->dest = MAKE_IP(127, 0, 0, 0);
			r4->netmask = MAKE_IP(255, 0, 0, 0);
			r4->ifaddress = MAKE_IP(127, 0, 0, 1);
			r4->gateway = 0;
			r4->flags = RTF_UP | RTF_LOCAL;
			AuRouteTableAdd(r4);
		} else {
			kfree(r4);
		}
	}

	r6 = AuRouteTable6CreateEntry();
	if (r6) {
		name6 = (char*)kmalloc(3);
		if (name6) {
			strcpy(name6, "lo");
			r6->ifname = name6;
			memset(&r6->dest, 0, sizeof(ip6_addr));
			r6->dest.s6_addr[15] = 1; /* ::1 */
			r6->prefixLen = 128;
			memset(&r6->ifaddress, 0, sizeof(ip6_addr));
			r6->ifaddress.s6_addr[15] = 1;
			memset(&r6->gateway, 0, sizeof(ip6_addr));
			r6->flags = RTF_UP | RTF_LOCAL | RTF_HOST;
			AuRouteTable6Add(r6);
		} else {
			kfree(r6);
		}
	}
}

static void AuNetCreateLoopback(void) {
	AuNetworkDevice* ndev;
	AuVFSNode* lo;

	ndev = (AuNetworkDevice*)kmalloc(sizeof(AuNetworkDevice));
	if (!ndev)
		return;
	memset(ndev, 0, sizeof(AuNetworkDevice));
	ndev->type = NETDEV_TYPE_LOOPBACK;
	ndev->linkStatus = 1;
	ndev->ipv4addr = MAKE_IP(127, 0, 0, 1);
	ndev->ipv4subnet = MAKE_IP(255, 0, 0, 0);
	ndev->ipv4gateway = 0;
	memset(&ndev->ipv6addr, 0, sizeof(ip6_addr));
	ndev->ipv6addr.s6_addr[15] = 1; /* ::1 */
	ndev->ipv6prefixLen = 128;

	lo = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!lo) {
		kfree(ndev);
		return;
	}
	memset(lo, 0, sizeof(AuVFSNode));
	strcpy(lo->filename, "lo");
	lo->flags = FS_FLAG_DEVICE;
	lo->write = AuLoopbackWrite;
	lo->iocontrol = AuLoopbackIOCtl;
	lo->device = ndev;
	AuAddNetAdapter(lo, "lo");
	AU_NET_LOG("[aurora]: loopback lo 127.0.0.1/8 ::1/128\r\n");
}

/**
 * @brief AuInitialiseNet -- initialise network data structures
 */
void AuInitialiseNet() {
	netadapters = AuHashmapCreate(10);
	AuVFSNode* fs = AuVFSFind("/dev");
	AuDevFSCreateFile(fs, "/dev/net", FS_FLAG_DIRECTORY);
	AuSocketInstall();
	AuRouteTableInitialise();
	AuRouteTable6Initialise();
	AuNetfilterInit();
	AuNetfilterInstallXrPolicy();
	/* ARP / NDP for Ethernet devices */
	ARPProtocolInitialise();
	NDProtocolInitialise();
	UDPProtocolInstall();
	ICMPInitialise();
	ICMPv6Initialise();
	TCPProtocolInstall();
	AuNetCreateLoopback();
	AuNetSeedLoopbackRoutes();
	AU_NET_LOG("[aurora]: net system initialised \r\n");
}

/**
 * @brief AuAddNetAdapter -- add a net adapter to the adapter
 * list
 */
AuVFSNode* AuAddNetAdapter(AuVFSNode* netfs, char* name) {
	AuVFSNode* old = (AuVFSNode*)AuHashmapGet(netadapters, name);
	if (old) {
		/* Adapter nodes are persistent: processes hold them open and the route
		 * table is keyed by name, so a new driver for the same name takes the
		 * existing node over instead of replacing it. The caller's node is
		 * left allocated (it may still be referenced) and the canonical one is
		 * returned. */
		old->flags = netfs->flags;
		old->write = netfs->write;
		old->iocontrol = netfs->iocontrol;
		old->device = netfs->device;
		AU_NET_LOG("NetAdapter taking over : %x \r\n", name);
		return old;
	}
	AuHashmapSet(netadapters, name, netfs);
	AU_NET_LOG("NetAdapter adding : %x \r\n", name);
	AuVFSNode* fs = AuVFSFind("/dev");
	AuDevFSAddFile(fs, "/dev/net", netfs);
	return netfs;
}

static AuNetworkDevice g_dead_ndev;
static size_t AuDeadAdapterWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	(void)node; (void)file; (void)buffer;
	return length;                       /* link down: frames go nowhere */
}
static int AuDeadAdapterIoctl(AuVFSNode* file, int code, void* arg) {
	(void)file; (void)code; (void)arg;
	return 1;
}

/**
 * @brief AuRemoveNetAdapter -- the driver behind an adapter is going away. The
 * node itself stays (open handles and the stack may still point at it) but
 * becomes a dead interface: writes are dropped, the link is down, and nothing
 * points into the departing driver any more. A later AuAddNetAdapter with the
 * same name revives it.
 * @return 0 if it existed, -1 otherwise
 */
int AuRemoveNetAdapter(char* name) {
	AuVFSNode* node = (AuVFSNode*)AuHashmapGet(netadapters, name);
	if (!node)
		return -1;
	node->write = AuDeadAdapterWrite;
	node->iocontrol = AuDeadAdapterIoctl;
	node->device = &g_dead_ndev;
	return 0;
}

/**
 * @brief AuGetNetworkAdapter -- get a network adapter
 * @param name -- adapter name
 */
AuVFSNode* AuGetNetworkAdapter(char* name) {
	AuVFSNode* node = (AuVFSNode*)AuHashmapGet(netadapters, name);
	return node;
}

static uint16_t AuNetAdapterFlags(AuNetworkDevice* ndev) {
	uint16_t flags = 0;

	if (!ndev)
		return 0;
	if (ndev->linkStatus)
		flags |= AU_IFF_UP | AU_IFF_RUNNING;
	if (ndev->type == NETDEV_TYPE_LOOPBACK)
		flags |= AU_IFF_LOOPBACK;
	if (ndev->type == NETDEV_TYPE_ETHERNET ||
		ndev->type == NETDEV_TYPE_802_11)
		flags |= AU_IFF_BROADCAST | AU_IFF_MULTICAST;
	return flags;
}

/**
 * @brief AuNetSnapshotIfAddrs -- copy the adapter table into fixed records
 * @param out -- destination array, may be NULL when only the count is needed
 * @param max -- number of records the caller can hold
 * @return number of records written, or a negative AU_ERR_* value
 */
int AuNetSnapshotIfAddrs(AuIfAddrInfo* out, int max) {
	unsigned int bucket;
	int index = 0;
	int written = 0;

	if (max < 0)
		return -AU_ERR_EINVAL;
	if (!netadapters)
		return 0;
	for (bucket = 0; bucket < netadapters->size; bucket++) {
		hashmap_entry_t* entry;
		for (entry = netadapters->entries[bucket]; entry; entry = entry->next) {
			AuVFSNode* node = (AuVFSNode*)entry->value;
			AuNetworkDevice* ndev;
			AuIfAddrInfo info;

			index++;
			if (written >= max)
				continue;
			ndev = node ? (AuNetworkDevice*)node->device : NULL;
			memset(&info, 0, sizeof(info));
			strncpy(info.ifa_name, entry->key, sizeof(info.ifa_name) - 1);
			info.ifa_index = (uint32_t)index;
			info.ifa_flags = AuNetAdapterFlags(ndev);
			if (ndev) {
				info.ifa_addr4 = ndev->ipv4addr;
				info.ifa_netmask4 = ndev->ipv4subnet;
				info.ifa_broadcast4 = ndev->ipv4subnet ?
					(ndev->ipv4addr | ~ndev->ipv4subnet) : 0;
				memcpy(info.ifa_mac, ndev->mac, sizeof(info.ifa_mac));
				info.ifa_link = ndev->linkStatus ? 1 : 0;
				info.ifa_prefix6 = ndev->ipv6prefixLen;
				memcpy(info.ifa_ipv6, ndev->ipv6addr.s6_addr,
					sizeof(info.ifa_ipv6));
				if (ndev->ipv4addr)
					info.ifa_family = AF_INET;
				else if (ndev->ipv6prefixLen)
					info.ifa_family = AF_INET6;
			}
			if (out)
				out[written] = info;
			written++;
		}
	}
	return written;
}

/**
 * @brief AuNetIfNameToIndex -- map an interface name to its 1-based index
 */
int AuNetIfNameToIndex(const char* name) {
	unsigned int bucket;
	int index = 0;

	if (!netadapters || !name || !name[0])
		return 0;
	for (bucket = 0; bucket < netadapters->size; bucket++) {
		hashmap_entry_t* entry;
		for (entry = netadapters->entries[bucket]; entry; entry = entry->next) {
			index++;
			if (!strcmp(entry->key, name))
				return index;
		}
	}
	return 0;
}

static int AuNicAddrEquals4(uint32_t address, uint32_t nicaddr) {
	if (!nicaddr)
		return 0;
	/* Wire/MAKE_IP form or host-order sockaddr (ping does htonl(inet_addr)). */
	if (address == nicaddr)
		return 1;
	if (address == htonl(nicaddr))
		return 1;
	return 0;
}

static int AuCheckNicLocal4(const char* name, uint32_t address) {
	AuVFSNode* n = AuGetNetworkAdapter((char*)name);
	AuNetworkDevice* ndev;

	if (!n || !n->device)
		return 0;
	ndev = (AuNetworkDevice*)n->device;
	return AuNicAddrEquals4(address, ndev->ipv4addr);
}

int AuAddrIsLocal4(uint32_t address) {
	/* 127/8 in MAKE_IP/wire form (low octet 127) */
	if ((address & MAKE_IP(255, 0, 0, 0)) == MAKE_IP(127, 0, 0, 0))
		return 1;
	/* 127/8 in host-order form (high octet 127), e.g. ping sockaddr */
	if ((address & 0xFF000000u) == 0x7F000000u)
		return 1;
	if (AuCheckNicLocal4("lo", address))
		return 1;
	if (AuCheckNicLocal4("virtio-net", address))
		return 1;
	if (AuCheckNicLocal4("e1000", address))
		return 1;
	if (AuCheckNicLocal4("wlan0", address))
		return 1;
	return 0;
}

static int AuIsIpv6Loopback(const ip6_addr* a) {
	int i;

	if (!a)
		return 0;
	for (i = 0; i < 15; i++) {
		if (a->s6_addr[i] != 0)
			return 0;
	}
	return a->s6_addr[15] == 1;
}

static int AuCheckNicLocal6(const char* name, const ip6_addr* address) {
	AuVFSNode* n = AuGetNetworkAdapter((char*)name);
	AuNetworkDevice* ndev;

	if (!n || !n->device || !address)
		return 0;
	ndev = (AuNetworkDevice*)n->device;
	if (ip6_addr_is_zero(&ndev->ipv6addr))
		return 0;
	return ip6_addr_equal(address, &ndev->ipv6addr);
}

int AuAddrIsLocal6(const ip6_addr* address) {
	if (!address)
		return 0;
	if (AuIsIpv6Loopback(address))
		return 1;
	if (AuCheckNicLocal6("lo", address))
		return 1;
	if (AuCheckNicLocal6("virtio-net", address))
		return 1;
	if (AuCheckNicLocal6("e1000", address))
		return 1;
	return 0;
}

void AuNetAddConnectedRoute4(AuVFSNode* nic, const char* ifname) {
	AuNetworkDevice* ndev;
	AuRouteEntry* entry;
	char* name;
	size_t nlen;

	if (!nic || !nic->device || !ifname)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (!ndev->ipv4addr || !ndev->ipv4subnet)
		return;

	entry = AuRouteTableCreateEntry();
	if (!entry)
		return;
	nlen = strlen(ifname) + 1;
	name = (char*)kmalloc(nlen);
	if (!name) {
		kfree(entry);
		return;
	}
	strcpy(name, ifname);
	entry->ifname = name;
	entry->dest = ndev->ipv4addr & ndev->ipv4subnet;
	entry->netmask = ndev->ipv4subnet;
	entry->ifaddress = ndev->ipv4addr;
	entry->gateway = 0;
	entry->flags = RTF_UP | RTF_CONNECTED;
	AuRouteTableAdd(entry);
}

void AuNetAddDefaultRoute4(AuVFSNode* nic, const char* ifname) {
	AuNetworkDevice* ndev;
	AuRouteEntry* entry;
	char* name;
	size_t nlen;

	if (!nic || !nic->device || !ifname)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (!ndev->ipv4gateway)
		return;

	entry = AuRouteTableCreateEntry();
	if (!entry)
		return;
	nlen = strlen(ifname) + 1;
	name = (char*)kmalloc(nlen);
	if (!name) {
		kfree(entry);
		return;
	}
	strcpy(name, ifname);
	entry->ifname = name;
	entry->dest = 0;
	entry->netmask = 0;
	entry->ifaddress = ndev->ipv4addr;
	entry->gateway = ndev->ipv4gateway;
	entry->flags = RTF_UP | RTF_GATEWAY;
	AuRouteTableAdd(entry);
}

void AuNetAddConnectedRoute6(AuVFSNode* nic, const char* ifname) {
	AuNetworkDevice* ndev;
	AuRouteEntry6* entry;
	char* name;
	size_t nlen;
	int i;
	uint8_t b;
	uint8_t bits;

	if (!nic || !nic->device || !ifname)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (ip6_addr_is_zero(&ndev->ipv6addr) || ndev->ipv6prefixLen == 0)
		return;

	entry = AuRouteTable6CreateEntry();
	if (!entry)
		return;
	nlen = strlen(ifname) + 1;
	name = (char*)kmalloc(nlen);
	if (!name) {
		kfree(entry);
		return;
	}
	strcpy(name, ifname);
	entry->ifname = name;
	ip6_addr_copy(&entry->ifaddress, &ndev->ipv6addr);
	memset(&entry->dest, 0, sizeof(ip6_addr));
	bits = ndev->ipv6prefixLen;
	for (i = 0; i < 16 && bits > 0; i++) {
		if (bits >= 8) {
			entry->dest.s6_addr[i] = ndev->ipv6addr.s6_addr[i];
			bits -= 8;
		} else {
			b = (uint8_t)(0xFFu << (8 - bits));
			entry->dest.s6_addr[i] = (uint8_t)(ndev->ipv6addr.s6_addr[i] & b);
			bits = 0;
		}
	}
	entry->prefixLen = ndev->ipv6prefixLen;
	memset(&entry->gateway, 0, sizeof(ip6_addr));
	entry->flags = RTF_UP | RTF_CONNECTED;
	AuRouteTable6Add(entry);
}

void AuNetAddDefaultRoute6(AuVFSNode* nic, const char* ifname) {
	AuNetworkDevice* ndev;
	AuRouteEntry6* entry;
	char* name;
	size_t nlen;

	if (!nic || !nic->device || !ifname)
		return;
	ndev = (AuNetworkDevice*)nic->device;
	if (ip6_addr_is_zero(&ndev->ipv6gateway))
		return;

	entry = AuRouteTable6CreateEntry();
	if (!entry)
		return;
	nlen = strlen(ifname) + 1;
	name = (char*)kmalloc(nlen);
	if (!name) {
		kfree(entry);
		return;
	}
	strcpy(name, ifname);
	entry->ifname = name;
	memset(&entry->dest, 0, sizeof(ip6_addr));
	entry->prefixLen = 0;
	ip6_addr_copy(&entry->ifaddress, &ndev->ipv6addr);
	ip6_addr_copy(&entry->gateway, &ndev->ipv6gateway);
	entry->flags = RTF_UP | RTF_GATEWAY;
	AuRouteTable6Add(entry);
}

static AuVFSNode* AuNetRadioNic(void) {
	AuVFSNode* n = AuGetNetworkAdapter("virtio-net");
	if (n)
		return n;
	return AuGetNetworkAdapter("e1000");
}

/**
 * @brief AuNetworkRoute -- select NIC for an IPv4 destination
 * @param address -- Address to consider (MAKE_IP/wire or host-order sockaddr)
 */
AuVFSNode* AuNetworkRoute(uint32_t address) {
	AuRouteResult rr;
	AuVFSNode* nic;

	if (AuAddrIsLocal4(address))
		return AuGetNetworkAdapter("lo");

	if (AuRouteLookup4(address, &rr) == 0 && rr.nic)
		return rr.nic;

	/* Single radio: seed connected/default from the NIC and retry. */
	nic = AuNetRadioNic();
	if (!nic)
		return NULL;
	AuNetAddConnectedRoute4(nic, nic->filename);
	AuNetAddDefaultRoute4(nic, nic->filename);
	if (AuRouteLookup4(address, &rr) == 0 && rr.nic)
		return rr.nic;
	AU_NET_LOG("[aurora]: IPv4 FIB miss, using radio %s\r\n", nic->filename);
	return nic;
}

AuVFSNode* AuNetworkRoute6(const ip6_addr* address) {
	AuRouteResult6 rr;
	AuVFSNode* nic;

	if (!address)
		return NULL;

	if (AuAddrIsLocal6(address))
		return AuGetNetworkAdapter("lo");

	if (AuRouteLookup6(address, &rr) == 0 && rr.nic)
		return rr.nic;

	nic = AuNetRadioNic();
	if (!nic)
		return NULL;
	AuNetAddConnectedRoute6(nic, nic->filename);
	AuNetAddDefaultRoute6(nic, nic->filename);
	if (AuRouteLookup6(address, &rr) == 0 && rr.nic)
		return rr.nic;
	AU_NET_LOG("[aurora]: IPv6 FIB miss, using radio %s\r\n", nic->filename);
	return nic;
}
