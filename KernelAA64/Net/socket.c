/**
* @file socket.c
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

#include <Net/socket.h>
#include <Net/ipv4.h>
#include <Net/ipv6.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <_null.h>
#include <Net/aunet.h>
#include <list.h>
#include <Drivers/uart.h>
#include <aucon.h>
#include <Net/route.h>
#include <Net/netfilter.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/profile.h>
#include <process.h>

list_t* raw_socket_list;

typedef struct _dns_entry_ {
	int index;
	uint32_t address;
} AuDNSEntry;

/**
 * @brief AuSocketAdd -- add some data to socket
 * @param sock -- Pointer to the socket
 * @param data -- data to add
 * @param sz -- Size of the data
 */
void AuSocketAdd(AuSocket* sock, void* data, size_t sz) {
	char* data_ = (char*)kmalloc(sizeof(size_t) + sz);
	memset(data_, 0, sz + sizeof(size_t));
	*(size_t*)data_ = sz;
	char* dest = data_ + sizeof(size_t);
	const char* src = (const char*)data;
	for (size_t i = 0; i < sz; i++) {
		dest[i] = src[i];
	}
	AuStackPush(sock->rxstack, data_);
}

/**
 * @brief AuSocketGet -- retreives previously stacked
 * data from the socket
 * @param sock -- Pointer to the socket
 */
void* AuSocketGet(AuSocket* sock) {
	void* data = AuStackPop(sock->rxstack);
	return data;
}

/**
 * @brief AuRawSocketReceive -- checks if there is any
 * received data in this raw socket
 * @param sock -- Pointer to the socket
 * @param msg -- Message header
 * @param flags -- flags to be cared about
 */
int AuRawSocketReceive(AuSocket* sock, msghdr* msg, int flags) {
	if (!sock->binedDev) {
		//UARTDebugOut("No binded device \r\n");
		return -1;
	}
	if (msg->msg_iovlen == 0) {
		UARTDebugOut("MSG_IOVLen : 0 \r\n");
		return 0;
	}
	AuNetRxPoll();
	char* data = (char*)AuSocketGet(sock);
	/* Empty RX queue is not an error — return 0 so DHCP poll loops can wait. */
	if (!data)
		return 0;
	size_t pack_sz = *(size_t*)data;
	if (msg->msg_iov[0].iov_len < pack_sz)
		return -1;
	memcpy(msg->msg_iov[0].iov_base, data + sizeof(size_t), pack_sz);
	kfree(data);
	return pack_sz;
}

int AuRawSocketSend(AuSocket* sock, msghdr* msg, int flags) {
	UARTDebugOut("Socket sending : %x \r\n", sock->binedDev);
	if (!sock->binedDev)
		return -1;
	if (msg->msg_iovlen == 0)
		return 0;
	AuVFSNode* device = (AuVFSNode*)sock->binedDev;
	UARTDebugOut("****/////******socket device name : %s \r\n", device->filename);
	/* virtio-net's adapter VFS node never populates a write callback (TX
	 * goes through a different path than the generic VFS write op), so
	 * this was a null function-pointer call -- crashed the whole boot
	 * (FAR_EL1/ELR_EL1 both 0) the moment netmngr tried a raw socket send
	 * against it. Guard until raw-socket TX is actually wired up for
	 * virtio-net. --axiss */
	if (!device->write)
		return -1;
	device->write(device, device, (uint64_t*)msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len);
	return 0;
}

#define SOL_SOCKET 0

#define SO_KEEPALIVE	1
#define SO_REUSEADDR	2
#define SO_BINDTODEVICE 3
#define SO_NONBLOCK	4

int AuSocketSOSocket(AuSocket* sock, int optname, const void* optval, socklen_t optlen) {
	switch (optname) {
	case SO_BINDTODEVICE: {
		UARTDebugOut("Device binding ->%s \r\n", ((char*)optval));
		//if (optlen < 1 || optlen > 32 || ((const char*)optval)[optlen - 1] != 0) return -1;
		AuVFSNode* nic = AuGetNetworkAdapter((char*)optval);
		UARTDebugOut("NIC : %x \r\n", nic);
		if (!nic)
			return -1;
		sock->binedDev = nic;
		UARTDebugOut("Device binded \r\n");
		return 0;
	}
	case SO_NONBLOCK: {
		int on;
		if (!optval || optlen < (socklen_t)sizeof(int))
			return -1;
		on = *(const int*)optval;
		sock->nonblock = on ? 1 : 0;
		return 0;
	}
	}
	return 1;
}

int AuSocketSetOpt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
	AA64Thread* curr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(curr);
	if (!proc) {
		proc = AuProcessFindSubThread(curr);
		if (!proc)
			return -1;
	}
	AuVFSNode* node = proc->fds[sockfd];
	AuSocket* sock = (AuSocket*)node->device;
	switch (level) {
	case SOL_SOCKET:
		return AuSocketSOSocket(sock, optname, optval, optlen);
	default:
		return -1;
	}
	return -1;
}
AuSocket* AuNetCreateSocket() {
	AuSocket* sock = (AuSocket*)kmalloc(sizeof(AuSocket));
	memset(sock, 0, sizeof(AuSocket));
	sock->rxstack = AuStackCreate();
	return sock;
}

int AuRawSocketClose(AuVFSNode* fs, AuVFSNode* file) {
	/* here both fs and file points to socket file , better
	 * would be using file pointer */
	AuSocket* sock = (AuSocket*)file->device;

	/* Remove it from raw socket list */
	for (int i = 0; i < raw_socket_list->pointer; i++) {
		AuSocket* socket = (AuSocket*)list_get_at(raw_socket_list, i);
		if (socket == sock) {
			list_remove(raw_socket_list, i);
			break;
		}
	}
	if (sock) {
		if (sock->rxstack) {
			while (sock->rxstack->itemCount) {
				void* data = AuStackPop(sock->rxstack);
				kfree(data);
			}
			kfree(sock->rxstack);
		}
		kfree(sock);
	}
	kfree(file);
	UARTDebugOut("Raw Socket Closed \r\n");
	return 0;
}

/**
 * @brief SocketIOControl -- Global socket io control function
 * @param file -- Pointer to the socket file
 * @param code -- IO Control Code
 * @param arg -- extra argument
 */
int SocketIOControl(AuVFSNode* file, int code, void* arg) {
	switch (code) {
		/* Route Table controls */
	case SOCK_ROUTE_TABLE_ADD: {
		if (!arg)
			return 1;
		AuRouteEntry* data = (AuRouteEntry*)arg;
		AuRouteEntry* entry = AuRouteTableCreateEntry();
		entry->ifname = (char*)kmalloc(strlen(data->ifname) + 1);
		strcpy(entry->ifname, data->ifname);
		entry->flags = data->flags;
		entry->ifaddress = data->ifaddress;
		entry->netmask = data->netmask;
		entry->dest = data->dest;
		entry->gateway = data->gateway;
		AuRouteTableAdd(entry);
		UARTDebugOut("[aurora] net route entry added \r\n");
		return 0;
	}
	case SOCK_ROUTE_TABLE_DELETE: {
		if (!arg)
			return 1;
		AuRouteEntry* data = (AuRouteEntry*)arg;
		AuRouteTableDelete(data);
		return 0;
	}
	case SOCK_ROUTE_TABLE_GETNUMENTRY: {
		return AuRouteTableGetNumEntry();
	}
	case SOCK_ROUTE_TABLE_GETENTRY: {
		UARTDebugOut("Populating route table \r\n");
		if (!arg)
			return 1;
		AuRouteEntryInfo* info = (AuRouteEntryInfo*)arg;
		if (info->index == -1)
			return 1;
		if (!info->route_entry)
			return 1;
		AuRouteEntry* entry = (AuRouteEntry*)info->route_entry;
		AuRouteTablePopulate(entry, info->index);
		UARTDebugOut("[aurora]: route table populated \r\n");
		return 0;
	}
	case SOCK_ADD_DNS_SERVER: {
		AuDNSEntry* dnsentry = (AuDNSEntry*)arg;
		AuSocket* sock;
		AuVFSNode* nic;
		AuNetworkDevice* netdev;
		if (!dnsentry)
			return 1;
		/* Always update the global table so resolvers need not bind a NIC. */
		if (dnsentry->index >= 1 && dnsentry->index <= AU_DNS_MAX4)
			AuDnsSetServer4(dnsentry->index, dnsentry->address);
		else
			AuDnsAddServer4(dnsentry->address);

		sock = (AuSocket*)file->device;
		nic = sock ? (AuVFSNode*)sock->binedDev : NULL;
		netdev = (nic && nic->device) ? (AuNetworkDevice*)nic->device : NULL;
		if (netdev) {
			switch (dnsentry->index) {
			case 1:
				netdev->dns_ipv4_1 = dnsentry->address;
				break;
			case 2:
				netdev->dns_ipv4_2 = dnsentry->address;
				break;
			case 3:
				netdev->dns_ipv4_3 = dnsentry->address;
				break;
			default:
				break;
			}
		}
		UARTDebugOut("[aurora]: DNS server idx %d -> %u (global)\r\n",
					 dnsentry->index,
					 dnsentry->address);
		return 0;
	}
	case SOCK_GET_DNS_SERVER: {
		AuDNSEntry* dnsentry = (AuDNSEntry*)arg;
		AuSocket* sock;
		AuVFSNode* nic;
		AuNetworkDevice* netdev;
		uint32_t addr = 0;
		if (!dnsentry)
			return 1;
		UARTDebugOut("[aurora]: Getting dns entry of index -> %d \r\n", dnsentry->index);
		if (AuDnsGetServer4(dnsentry->index, &addr) == 0 && addr != 0) {
			dnsentry->address = addr;
			UARTDebugOut("[aurora]: dns entry address -> %d \r\n", dnsentry->address);
			return 0;
		}
		/* Fall back to bound NIC slots if global empty at this index. */
		sock = (AuSocket*)file->device;
		nic = sock ? (AuVFSNode*)sock->binedDev : NULL;
		netdev = (nic && nic->device) ? (AuNetworkDevice*)nic->device : NULL;
		if (!netdev) {
			nic = AuGetNetworkAdapter("virtio-net");
			if (!nic)
				nic = AuGetNetworkAdapter("e1000");
			netdev = nic ? (AuNetworkDevice*)nic->device : NULL;
		}
		if (netdev) {
			switch (dnsentry->index) {
			case 1:
				addr = netdev->dns_ipv4_1;
				break;
			case 2:
				addr = netdev->dns_ipv4_2;
				break;
			case 3:
				addr = netdev->dns_ipv4_3;
				break;
			default:
				break;
			}
		}
		/*
		 * No fabricated default here: the stub resolver owns the fallback
		 * (1.1.1.1) so dig reports the server it actually uses.
		 */
		dnsentry->address = addr;
		UARTDebugOut("[aurora]: dns entry address -> %d \r\n", dnsentry->address);
		return 0;
	}
	case SOCK_NF_APPEND: {
		AuNfRule rule;
		int rc;
		if (!arg)
			return 1;
		memcpy(&rule, arg, sizeof(rule));
		rule.in_dev[AU_NF_IFNAMSIZ - 1] = '\0';
		rule.out_dev[AU_NF_IFNAMSIZ - 1] = '\0';
		rc = AuNetfilterAppend(&rule);
		UARTDebugOut("[aurora]: nf append hook=%d proto=%d dport=%d target=%d in=%s rc=%d count=%d\r\n",
					 rule.hook, rule.proto, rule.dport, rule.target, rule.in_dev, rc,
					 AuNetfilterGetNum());
		return rc == 0 ? 0 : 1;
	}
	case SOCK_NF_DELETE: {
		int index;
		if (!arg)
			return 1;
		memcpy(&index, arg, sizeof(index));
		return AuNetfilterDelete(index) == 0 ? 0 : 1;
	}
	case SOCK_NF_FLUSH:
		return AuNetfilterFlush();
	case SOCK_NF_GETNUM:
		return AuNetfilterGetNum();
	case SOCK_NF_LIST: {
		AuNfRuleInfo uinfo;
		AuNfRule krule;
		if (!arg)
			return 1;
		memcpy(&uinfo, arg, sizeof(uinfo));
		if (!uinfo.rule)
			return 1;
		if (AuNetfilterGetEntry(uinfo.index, &krule) != 0)
			return 1;
		memcpy(uinfo.rule, &krule, sizeof(krule));
		return 0;
	}
	}
	return 1;
}
/**
 * @brief AuCreateRawSocket -- creates a very basic raw socket
 * interface
 * @param type -- type of the socket
 * @param protocol -- unused
 */
int AuCreateRawSocket(int type, int protocol) {
	if (type != SOCK_RAW)
		return -1;
	AA64Thread* curr_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(curr_thr);
	if (!proc) {
		proc = AuProcessFindSubThread(curr_thr);
		if (!proc)
			return 1;
	}
	AuSocket* sock = AuNetCreateSocket();
	sock->receive = AuRawSocketReceive;
	sock->send = AuRawSocketSend;
	UARTDebugOut("*****///Raw Socket created %x\r\n", sock);
	int fd = AuProcessGetFileDesc(proc);
	list_add(raw_socket_list, sock);
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "XERawSocket");
	node->flags = FS_FLAG_SOCKET;
	node->device = sock;
	node->close = AuRawSocketClose;
	node->iocontrol = SocketIOControl;
	proc->fds[fd] = node;
	return fd;
}

/**
 * @brief AuCreateSocket -- create a new socket for specific
 * domain
 * @param domain -- domain type -- IPV4_INET, RAW or IPV6_INET
 * @param type -- type
 * @param protocol -- protocol version
 */
int AuCreateSocket(int domain, int type, int protocol) {
	switch (domain) {
	case AF_INET:
		return CreateIPv4Socket(type, protocol);
	case AF_INET6:
		return CreateIPv6Socket(type, protocol);
	case AF_RAW:
		return AuCreateRawSocket(type, protocol);
	default:
		return -1;
	}
}

/**
 * @brief AuSocketInstall -- install the socket
 * interface
 */
void AuSocketInstall() {
	raw_socket_list = initialize_list();
}

/**
 * @brief AuRawSocketGetList -- get raw socket list
 */
list_t* AuRawSocketGetList() {
	return raw_socket_list;
}
