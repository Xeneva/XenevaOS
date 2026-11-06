/**
* @file route.c
*
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

#include <Net/route.h>
#include <Net/aunet.h>
#include <list.h>
#include <string.h>
#include <Mm/kmalloc.h>
#include <Hal/serial.h>
#include <_null.h>

list_t* _kernelRouteList;
list_t* _kernelRouteList6;

static int AuRoutePref(uint8_t flags, uint32_t netmask) {
	int pref;

	if (flags & RTF_LOCAL)
		pref = 300;
	else if (flags & RTF_CONNECTED)
		pref = 200;
	else if (netmask == 0)
		pref = 0; /* default */
	else
		pref = 100;
	return pref;
}

static int AuRoutePref6(uint8_t flags, uint8_t prefixLen) {
	int pref;

	if (flags & RTF_LOCAL)
		pref = 300;
	else if (flags & RTF_CONNECTED)
		pref = 200;
	else if (prefixLen == 0)
		pref = 0;
	else
		pref = 100;
	return pref;
}

/**
  * @brief AuRouteTableInitialise -- initialise the kernel route
  * table
  */
void AuRouteTableInitialise() {
	_kernelRouteList = initialize_list();
}

/**
 * @brief AuRouteTableCreateEntry -- create a new route table
 * entry and return
 */
AuRouteEntry* AuRouteTableCreateEntry() {
	AuRouteEntry* entry = (AuRouteEntry*)kmalloc(sizeof(AuRouteEntry));
	memset(entry, 0, sizeof(AuRouteEntry));
	return entry;
}

extern void ip_ntoa(const uint32_t src);

/**
 * @brief AuRouteTableAdd -- add an entry to route table.
 * Allows default route (dest==0 && netmask==0). Replaces an
 * existing entry with the same dest/netmask/ifname.
 */
void AuRouteTableAdd(AuRouteEntry* entry) {
	int i;

	if (!entry)
		return;
	/* Reject empty dest unless this is the default route. */
	if (!entry->dest && entry->netmask)
		return;
	if (!(entry->flags & RTF_UP))
		entry->flags |= RTF_UP;

	for (i = 0; i < _kernelRouteList->pointer; i++) {
		AuRouteEntry* old = (AuRouteEntry*)list_get_at(_kernelRouteList, i);
		if (!old)
			continue;
		if (old->dest == entry->dest && old->netmask == entry->netmask) {
			if (old->ifname && entry->ifname && strcmp(old->ifname, entry->ifname) == 0) {
				old->ifaddress = entry->ifaddress;
				old->gateway = entry->gateway;
				old->flags = entry->flags;
				if (entry->ifname)
					kfree(entry->ifname);
				kfree(entry);
				return;
			}
		}
	}
	list_add(_kernelRouteList, entry);
}

/**
 * @brief AuRouteTableDelete -- delete an entry from
 * route table (including default 0/0).
 */
void AuRouteTableDelete(AuRouteEntry* entry) {
	int index = -1;
	int i;

	if (!entry)
		return;
	for (i = 0; i < _kernelRouteList->pointer; i++) {
		AuRouteEntry* _entry = (AuRouteEntry*)list_get_at(_kernelRouteList, i);
		if (_entry->dest == entry->dest && _entry->netmask == entry->netmask) {
			index = i;
			break;
		}
	}
	if (index != -1) {
		AuRouteEntry* _removable = (AuRouteEntry*)list_remove(_kernelRouteList, index);
		kfree(_removable->ifname);
		kfree(_removable);
	}
}

int AuRouteSetFlag4(const char* ifname, uint32_t dest, uint32_t netmask, int up) {
	int i;

	if (!ifname || !_kernelRouteList)
		return -1;
	for (i = 0; i < _kernelRouteList->pointer; i++) {
		AuRouteEntry* entry = (AuRouteEntry*)list_get_at(_kernelRouteList, i);
		if (!entry || !entry->ifname)
			continue;
		if (entry->dest != dest || entry->netmask != netmask)
			continue;
		if (strcmp(entry->ifname, ifname) != 0)
			continue;
		if (up)
			entry->flags = (uint8_t)(entry->flags | RTF_UP);
		else
			entry->flags = (uint8_t)(entry->flags & (uint8_t)~RTF_UP);
		return 0;
	}
	return -1;
}

/**
 * @brief AuRouteTableGetNumEntry -- returns the number
 * route entry present in the system
 */
int AuRouteTableGetNumEntry() {
	return _kernelRouteList->pointer;
}

/**
 * @brief AuRouteTablePopulate -- populates a given memory pointer with
 * route table entry indexed by entryIndex number
 */
void AuRouteTablePopulate(AuRouteEntry* whereToPopulate, int entryIndex) {
	AuRouteEntry* entry;

	if (!whereToPopulate)
		return;
	if (entryIndex == -1)
		return;
	if (entryIndex >= _kernelRouteList->pointer)
		return;

	entry = (AuRouteEntry*)list_get_at(_kernelRouteList, entryIndex);
	if (!entry)
		return;
	if (whereToPopulate->ifname && entry->ifname)
		strcpy(whereToPopulate->ifname, entry->ifname);
	whereToPopulate->dest = entry->dest;
	whereToPopulate->flags = entry->flags;
	whereToPopulate->gateway = entry->gateway;
	whereToPopulate->ifaddress = entry->ifaddress;
	whereToPopulate->netmask = entry->netmask;
}

/**
 * @brief AuRouteTableDoRouteLookup -- longest-prefix match; skip !RTF_UP.
 * Prefer LOCAL over CONNECTED over default when prefixes tie.
 */
AuRouteEntry* AuRouteTableDoRouteLookup(uint32_t address) {
	AuRouteEntry* bestRoute = NULL;
	int bestPref = -1;
	int i;

	if (!_kernelRouteList)
		return NULL;
	for (i = 0; i < _kernelRouteList->pointer; i++) {
		AuRouteEntry* _entry = (AuRouteEntry*)list_get_at(_kernelRouteList, i);
		int pref;

		if (!_entry)
			continue;
		if (!(_entry->flags & RTF_UP))
			continue;
		if ((address & _entry->netmask) != (_entry->dest & _entry->netmask))
			continue;
		pref = AuRoutePref(_entry->flags, _entry->netmask);
		if (!bestRoute ||
			_entry->netmask > bestRoute->netmask ||
			(_entry->netmask == bestRoute->netmask && pref > bestPref)) {
			bestRoute = _entry;
			bestPref = pref;
		}
	}
	return bestRoute;
}

int AuRouteLookup4(uint32_t address, AuRouteResult* out) {
	AuRouteEntry* rt;

	if (!out)
		return -1;
	memset(out, 0, sizeof(AuRouteResult));
	rt = AuRouteTableDoRouteLookup(address);
	if (!rt)
		return -1;
	out->entry = rt;
	out->flags = rt->flags;
	out->nic = rt->ifname ? AuGetNetworkAdapter(rt->ifname) : NULL;
	if (rt->flags & RTF_GATEWAY)
		out->nexthop = rt->gateway;
	else
		out->nexthop = address;
	return 0;
}

void AuRouteTable6Initialise() {
	_kernelRouteList6 = initialize_list();
}

AuRouteEntry6* AuRouteTable6CreateEntry() {
	AuRouteEntry6* entry = (AuRouteEntry6*)kmalloc(sizeof(AuRouteEntry6));
	if (!entry)
		return NULL;
	memset(entry, 0, sizeof(AuRouteEntry6));
	return entry;
}

void AuRouteTable6Add(AuRouteEntry6* entry) {
	int i;

	if (!entry)
		return;
	/* Allow ::/0 default; reject other empty dest with non-zero prefix. */
	if (ip6_addr_is_zero(&entry->dest) && entry->prefixLen != 0)
		return;
	if (!(entry->flags & RTF_UP))
		entry->flags |= RTF_UP;

	for (i = 0; i < _kernelRouteList6->pointer; i++) {
		AuRouteEntry6* old = (AuRouteEntry6*)list_get_at(_kernelRouteList6, i);
		if (!old)
			continue;
		if (old->prefixLen == entry->prefixLen &&
			ip6_addr_equal(&old->dest, &entry->dest) &&
			old->ifname && entry->ifname &&
			strcmp(old->ifname, entry->ifname) == 0) {
			ip6_addr_copy(&old->ifaddress, &entry->ifaddress);
			ip6_addr_copy(&old->gateway, &entry->gateway);
			old->flags = entry->flags;
			kfree(entry->ifname);
			kfree(entry);
			return;
		}
	}
	list_add(_kernelRouteList6, entry);
}

void AuRouteTable6Delete(AuRouteEntry6* entry) {
	int index = -1;
	int i;

	if (!entry || !_kernelRouteList6)
		return;
	for (i = 0; i < _kernelRouteList6->pointer; i++) {
		AuRouteEntry6* e = (AuRouteEntry6*)list_get_at(_kernelRouteList6, i);
		if (!e)
			continue;
		if (e->prefixLen == entry->prefixLen &&
			ip6_addr_equal(&e->dest, &entry->dest)) {
			index = i;
			break;
		}
	}
	if (index != -1) {
		AuRouteEntry6* rem = (AuRouteEntry6*)list_remove(_kernelRouteList6, index);
		if (rem->ifname)
			kfree(rem->ifname);
		kfree(rem);
	}
}

AuRouteEntry6* AuRouteTableDoRouteLookup6(const ip6_addr* address) {
	AuRouteEntry6* bestRoute = NULL;
	int bestPref = -1;
	int i;

	if (!address || !_kernelRouteList6)
		return NULL;
	for (i = 0; i < _kernelRouteList6->pointer; i++) {
		AuRouteEntry6* _entry = (AuRouteEntry6*)list_get_at(_kernelRouteList6, i);
		int pref;

		if (!_entry)
			continue;
		if (!(_entry->flags & RTF_UP))
			continue;
		if (!ip6_prefix_equal(address, &_entry->dest, _entry->prefixLen))
			continue;
		pref = AuRoutePref6(_entry->flags, _entry->prefixLen);
		if (!bestRoute ||
			_entry->prefixLen > bestRoute->prefixLen ||
			(_entry->prefixLen == bestRoute->prefixLen && pref > bestPref)) {
			bestRoute = _entry;
			bestPref = pref;
		}
	}
	return bestRoute;
}

int AuRouteLookup6(const ip6_addr* address, AuRouteResult6* out) {
	AuRouteEntry6* rt;

	if (!out || !address)
		return -1;
	memset(out, 0, sizeof(AuRouteResult6));
	rt = AuRouteTableDoRouteLookup6(address);
	if (!rt)
		return -1;
	out->entry = rt;
	out->flags = rt->flags;
	out->nic = rt->ifname ? AuGetNetworkAdapter(rt->ifname) : NULL;
	if (rt->flags & RTF_GATEWAY)
		ip6_addr_copy(&out->nexthop, &rt->gateway);
	else
		ip6_addr_copy(&out->nexthop, address);
	return 0;
}
