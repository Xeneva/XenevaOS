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

#ifndef __AUNET_H__
#define __AUNET_H__

#include <stdint.h>
#include <aurora.h>
#include <Fs/vfs.h>
#include <Net/ipv6.h>

/* Network logging is synchronous UART output and sits on the packet path.
 * It is compiled out until the deferred logger is in place; build with
 * -DAU_NET_LOG_ENABLE=1 to restore the old messages. */
#ifndef AU_NET_LOG_ENABLE
#define AU_NET_LOG_ENABLE 0
#endif
#if AU_NET_LOG_ENABLE
#define AU_NET_LOG(...) UARTDebugOut(__VA_ARGS__)
#else
#define AU_NET_LOG(...) do { } while (0)
#endif

#define NETDEV_TYPE_ETHERNET 1
#define NETDEV_TYPE_802_11 2
#define NETDEV_TYPE_BLUETOOTH 3
#define NETDEV_TYPE_LOOPBACK 4

/* Wire/MAKE_IP form used by NIC ipv4addr and on-wire headers (LE host). */
#ifndef MAKE_IP
#define MAKE_IP(a, b, c, d) \
	((uint32_t)(d) << 24 | (uint32_t)(c) << 16 | (uint32_t)(b) << 8 | (uint32_t)(a))
#endif

#ifdef ARCH_X64
#pragma pack(push,1)
#endif
typedef struct _netdev_{
	uint8_t mac[6];
	int linkStatus;
	uint8_t type;
	uint32_t ipv4addr;
	uint32_t ipv4subnet;
	uint32_t ipv4gateway;
	uint32_t dns_ipv4_1;
	uint32_t dns_ipv4_2;
	uint32_t dns_ipv4_3;
	ip6_addr ipv6addr;
	ip6_addr ipv6gateway;
	uint8_t ipv6prefixLen;
	ip6_addr dns_ipv6_1;
}AuNetworkDevice;
#ifdef ARCH_X64
#pragma pack(pop)
#endif


#define htonl(l)  ((((l) & 0xFF) << 24) | (((l) & 0xFF00) << 8) | (((l) & 0xFF0000) >> 8) | (((l) & 0xFF000000) >> 24))
#define htons(s)  ((((s) & 0xFF) << 8) | (((s) & 0xFF00) >> 8))
#define ntohl(l)  htonl((l))
#define ntohs(s)  htons((s))

/*I/O Codes used for network interfaces */
#define AUNET_GET_HARDWARE_ADDRESS 0x100
#define AUNET_SET_IPV4_ADDRESS 0x101
#define AUNET_GET_IPV4_ADDRESS 0x102
#define AUNET_GET_GATEWAY_ADDRESS 0x103
#define AUNET_SET_GATEWAY_ADDRESS 0x104
#define AUNET_GET_SUBNET_MASK 0x105
#define AUNET_SET_SUBNET_MASK 0x106
#define AUNET_GET_LINK_STATUS 0x107
#define AUNET_GET_IPV6_ADDRESS 0x108
#define AUNET_SET_IPV6_ADDRESS 0x109
#define AUNET_GET_IPV6_GATEWAY 0x10A
#define AUNET_SET_IPV6_GATEWAY 0x10B
#define AUNET_GET_IPV6_PREFIX 0x10C
#define AUNET_SET_IPV6_PREFIX 0x10D

/* 802.11 station ioctls (IEEE 802.11-2020). Arg is Wifi* from Net/wifi.h. */
#define WIFI_SCAN        0x140
#define WIFI_GET_SCAN    0x141
#define WIFI_CONNECT     0x142
#define WIFI_DISCONNECT  0x143
#define WIFI_GET_STATUS  0x144
#define WIFI_UPLINK      0x145

/*
* AuInitialiseNet -- initialise network data structures
*/
extern void AuInitialiseNet();

/*
* AuAddNetAdapter -- add a net adapter to the adapter
* list
*/
AU_EXTERN AU_EXPORT AuVFSNode* AuAddNetAdapter(AuVFSNode* netfs,char* name);

/*
* AuRemoveNetAdapter -- the driver behind an adapter is going away; the node
* stays but goes dead (see aunet.c)
*/
AU_EXTERN AU_EXPORT int AuRemoveNetAdapter(char* name);
/*
* AuGetNetworkAdapter -- get a network adapter 
* @param name -- adapter name
*/
AU_EXTERN AU_EXPORT AuVFSNode* AuGetNetworkAdapter(char* name);

/* Interface snapshot shared with XEClib's getifaddrs(2)/if_nametoindex(2).
 * Keep the layout in sync with Libs/XEClib/includes/ifaddrs.h. */
#define AU_IFNAME_MAX 16
#define AU_IFADDR_MAX 32

#define AU_IFF_UP         0x1
#define AU_IFF_BROADCAST  0x2
#define AU_IFF_LOOPBACK   0x8
#define AU_IFF_RUNNING    0x40
#define AU_IFF_MULTICAST  0x1000

typedef struct _au_ifaddrinfo_ {
	char ifa_name[AU_IFNAME_MAX];
	uint32_t ifa_index;
	uint16_t ifa_family;
	uint16_t ifa_flags;
	uint32_t ifa_addr4;
	uint32_t ifa_netmask4;
	uint32_t ifa_broadcast4;
	uint8_t ifa_mac[6];
	uint8_t ifa_link;
	uint8_t ifa_prefix6;
	uint8_t ifa_ipv6[16];
	uint8_t _ifa_pad[2];
} AuIfAddrInfo;

/*
 * AuNetSnapshotIfAddrs -- fill up to max adapter records. Returns the number
 * written, or a negative AU_ERR_* value. Index values are 1-based and stable
 * for the lifetime of the adapter table.
 */
AU_EXTERN AU_EXPORT int AuNetSnapshotIfAddrs(AuIfAddrInfo* out, int max);

/*
 * AuNetIfNameToIndex -- map an interface name to its 1-based index, or 0
 * when the adapter does not exist.
 */
AU_EXTERN AU_EXPORT int AuNetIfNameToIndex(const char* name);

/*
 * AuNetworkRoute -- select NIC via FIB (RFC 1122 §3.3.1).
 * Local/127/8 → lo. FIB hit → that NIC. Miss → NULL (ENETUNREACH).
 */
extern AuVFSNode* AuNetworkRoute(uint32_t address);

/*
 * AuNetworkRoute6 -- select NIC for an IPv6 destination.
 * Miss → NULL (no silent virtio-net fallback).
 */
extern AuVFSNode* AuNetworkRoute6(const ip6_addr* address);

/* Local-address checks (127/8, ::1, or any configured NIC address). */
extern int AuAddrIsLocal4(uint32_t address);
extern int AuAddrIsLocal6(const ip6_addr* address);

/* Install connected / default routes after address/gateway ioctl. */
AU_EXTERN AU_EXPORT void AuNetAddConnectedRoute4(AuVFSNode* nic, const char* ifname);
AU_EXTERN AU_EXPORT void AuNetAddConnectedRoute6(AuVFSNode* nic, const char* ifname);
AU_EXTERN AU_EXPORT void AuNetAddDefaultRoute4(AuVFSNode* nic, const char* ifname);
AU_EXTERN AU_EXPORT void AuNetAddDefaultRoute6(AuVFSNode* nic, const char* ifname);

/* Global nameserver table (not tied to a bound NIC). Index is 1-based. */
#define AU_DNS_MAX4 4
#define AU_DNS_MAX6 2
AU_EXTERN AU_EXPORT void AuDnsAddServer4(uint32_t addr);
AU_EXTERN AU_EXPORT void AuDnsSetServer4(int index, uint32_t addr);
extern int AuDnsGetServer4(int index, uint32_t* out);
AU_EXTERN AU_EXPORT void AuDnsAddServer6(const ip6_addr* addr);
AU_EXTERN AU_EXPORT void AuDnsSetServer6(int index, const ip6_addr* addr);
extern int AuDnsGetServer6(int index, ip6_addr* out);

/* Driver registers so socket recv can drain RX while userspace waits. */
AU_EXTERN AU_EXPORT void AuNetRegisterRxPoll(void (*fn)(void));
AU_EXTERN AU_EXPORT void AuNetRxPoll(void);

#endif