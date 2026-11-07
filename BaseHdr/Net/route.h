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

#ifndef __ROUTE_H__
#define __ROUTE_H__

#include <stdint.h>
#include <Net/ipv6.h>
#include <Fs/vfs.h>

/* Route flags (bitmask). */
#define RTF_UP        0x01
#define RTF_HOST      0x02
#define RTF_GATEWAY   0x04
#define RTF_LOCAL     0x08
#define RTF_CONNECTED 0x10

/* Simple Route table entry structure */
typedef struct _route_entry_ {
	char* ifname;
	uint32_t dest;
	uint32_t netmask;
	uint32_t ifaddress;
	uint32_t gateway;
	uint8_t flags;
}AuRouteEntry;

typedef struct _route_entry6_ {
	char* ifname;
	ip6_addr dest;
	uint8_t prefixLen;
	ip6_addr ifaddress;
	ip6_addr gateway;
	uint8_t flags;
}AuRouteEntry6;

typedef struct _route_entry_info_ {
	int index;
	void* route_entry;
}AuRouteEntryInfo;

/* Output of an IPv4 FIB lookup (RFC 1122 / 1812 next-hop). */
typedef struct _au_route_result_ {
	AuRouteEntry* entry;
	AuVFSNode* nic;
	uint32_t nexthop;
	uint8_t flags;
} AuRouteResult;

typedef struct _au_route_result6_ {
	AuRouteEntry6* entry;
	AuVFSNode* nic;
	ip6_addr nexthop;
	uint8_t flags;
} AuRouteResult6;

/*
 * AuRouteTableInitialise -- initialise the kernel route
 * table
 */
extern void AuRouteTableInitialise();

/*
 * AuRouteTableCreateEntry -- create a new route table
 * entry and return
 */
extern AuRouteEntry* AuRouteTableCreateEntry();

/*
 * AuRouteTableAdd -- add an entry to route
 * table
 * @param entry -- Entry to add
 */
extern void AuRouteTableAdd(AuRouteEntry* entry);

/*
 * AuRouteTableDelete -- delete an entry from
 * route table
 * @param entry -- entry to delete
 */
extern void AuRouteTableDelete(AuRouteEntry* entry);

/*
 * AuRouteSetFlag4 -- set or clear RTF_UP on one IPv4 route.
 * Two default routes share a prefix; the first RTF_UP entry wins.
 */
AU_EXTERN AU_EXPORT int AuRouteSetFlag4(const char* ifname, uint32_t dest, uint32_t netmask, int up);

/*
 * AuRouteTableGetNumEntry -- returns the number
 * route entry present in the system
 */
extern int AuRouteTableGetNumEntry();

/*
 * AuRouteTablePopulate -- populates a given memory pointer with
 * route table entry indexed by entryIndex number
 * @param whereToPopulate -- memory pointer where to populate
 * with an entry
 * @param entryIndex -- entry index number
 */
extern void AuRouteTablePopulate(AuRouteEntry* whereToPopulate, int entryIndex);

/*
 * AuRouteTableDoRouteLookup -- takes the decision on taking
 * the best route
 * @param address -- address to take for routing
 */
extern AuRouteEntry* AuRouteTableDoRouteLookup(uint32_t address);

/*
 * AuRouteLookup4 -- fill AuRouteResult for dest (NULL entry on miss).
 */
extern int AuRouteLookup4(uint32_t address, AuRouteResult* out);

/*
 * AuRouteTable6Initialise -- initialise IPv6 route table
 */
extern void AuRouteTable6Initialise();

extern AuRouteEntry6* AuRouteTable6CreateEntry();
extern void AuRouteTable6Add(AuRouteEntry6* entry);
extern void AuRouteTable6Delete(AuRouteEntry6* entry);
extern AuRouteEntry6* AuRouteTableDoRouteLookup6(const ip6_addr* address);
extern int AuRouteLookup6(const ip6_addr* address, AuRouteResult6* out);
#endif
