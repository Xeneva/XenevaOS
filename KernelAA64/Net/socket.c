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
#include <Net/unix.h>
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
	char* data_;

	if (!sock || !sock->rxstack || !data || !sz)
		return;
	data_ = (char*)kmalloc(sizeof(size_t) + sz);
	if (!data_)
		return;
	*(size_t*)data_ = sz;
	memcpy(data_ + sizeof(size_t), data, sz);
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
		//AU_NET_LOG("No binded device \r\n");
		return -1;
	}
	if (msg->msg_iovlen == 0) {
		AU_NET_LOG("MSG_IOVLen : 0 \r\n");
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
	AU_NET_LOG("Socket sending : %x \r\n", sock->binedDev);
	if (!sock->binedDev)
		return -1;
	if (msg->msg_iovlen == 0)
		return 0;
	AuVFSNode* device = (AuVFSNode*)sock->binedDev;
	AU_NET_LOG("****/////******socket device name : %s \r\n", device->filename);
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

AuSocket* AuSocketFromFd(int sockfd) {
	AA64Thread* thr;
	AuProcess* proc;
	AuVFSNode* node;

	if (sockfd < 0 || sockfd >= FILE_DESC_PER_PROCESS)
		return NULL;
	thr = AuGetCurrentThread();
	if (!thr)
		return NULL;
	proc = AuProcessFindThread(thr);
	if (!proc)
		proc = AuProcessFindSubThread(thr);
	if (!proc)
		return NULL;
	node = proc->fds[sockfd];
	if (!node)
		return NULL;
	if (!(node->flags & FS_FLAG_SOCKET))
		return NULL;
	return (AuSocket*)node->device;
}

/**
 * @brief AuSocketCopyAddr -- copy a native sockaddr with truncation
 * @param dst -- user destination buffer
 * @param addrlen -- in: room, out: full address length
 * @param src -- protocol address
 * @param srclen -- full protocol address length
 */
int AuSocketCopyAddr(sockaddr* dst, socklen_t* addrlen, const void* src,
	socklen_t srclen) {
	socklen_t room;

	if (!dst || !addrlen || !src)
		return -AU_ERR_EFAULT;
	room = *addrlen;
	if (room > srclen)
		room = srclen;
	if (room)
		memcpy(dst, src, room);
	*addrlen = srclen;
	return 0;
}

int AuSocketCopyMsgName(msghdr* msg, const void* src, socklen_t srclen) {
	socklen_t room;

	if (!msg)
		return -AU_ERR_EINVAL;
	if (!msg->msg_name) {
		msg->msg_namelen = 0;
		return 0;
	}
	room = msg->msg_namelen;
	if (room > srclen)
		room = srclen;
	if (room)
		memcpy(msg->msg_name, src, room);
	msg->msg_namelen = srclen;
	return 0;
}

int AuSocketInstallFd(AuSocket* sock, const char* name,
	int (*closefn)(AuVFSNode*, AuVFSNode*)) {	AA64Thread* thr;
	AuProcess* proc;
	AuVFSNode* node;
	int fd;

	if (!sock)
		return -AU_ERR_EINVAL;
	thr = AuGetCurrentThread();
	if (!thr)
		return -AU_ERR_EBADF;
	proc = AuProcessFindThread(thr);
	if (!proc)
		proc = AuProcessFindSubThread(thr);
	if (!proc)
		return -AU_ERR_EBADF;

	fd = AuProcessGetFileDesc(proc);
	if (fd < 0)
		return -AU_ERR_EMFILE;
	node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	if (!node)
		return -AU_ERR_EMFILE;
	memset(node, 0, sizeof(AuVFSNode));
	strncpy(node->filename, name ? name : "socket", sizeof(node->filename) - 1);
	node->flags = FS_FLAG_SOCKET;
	node->device = sock;
	node->close = closefn;
	node->iocontrol = SocketIOControl;
	node->poll = AuSocketProtocolPoll;
	proc->fds[fd] = node;
	return fd;
}

int AuSocketSOSocket(AuSocket* sock, int optname, const void* optval, socklen_t optlen) {
	if (!sock)
		return -AU_ERR_EBADF;
	switch (optname) {
	case SO_BINDTODEVICE: {
		AuVFSNode* nic;
		char ifname[32];
		size_t n;
		if (!optval || optlen == 0) {
			sock->binedDev = NULL;
			return 0;
		}
		n = optlen;
		if (n >= sizeof(ifname))
			n = sizeof(ifname) - 1;
		memcpy(ifname, optval, n);
		ifname[n] = '\0';
		if (!ifname[0])
			return -AU_ERR_EINVAL;
		nic = AuGetNetworkAdapter(ifname);
		if (!nic)
			return -AU_ERR_EINVAL;
		sock->binedDev = nic;
		return 0;
	}
	case SO_NONBLOCK: {
		int on;
		if (!optval || optlen < sizeof(int))
			return -AU_ERR_EINVAL;
		memcpy(&on, optval, sizeof(int));
		sock->nonblock = on ? 1 : 0;
		return 0;
	}
	case SO_REUSEADDR: {
		int on;
		if (!optval || optlen < sizeof(int))
			return -AU_ERR_EINVAL;
		memcpy(&on, optval, sizeof(int));
		sock->reuseaddr = on ? 1 : 0;
		return 0;
	}
	case SO_KEEPALIVE: {
		int on;
		if (!optval || optlen < sizeof(int))
			return -AU_ERR_EINVAL;
		memcpy(&on, optval, sizeof(int));
		sock->keepalive = on ? 1 : 0;
		return 0;
	}
	case SO_RCVBUF: {
		int sz;
		if (!optval || optlen < sizeof(int))
			return -AU_ERR_EINVAL;
		memcpy(&sz, optval, sizeof(int));
		if (sz < 2048)
			return -AU_ERR_EINVAL;
		if (sz > (1024 * 1024))
			sz = 1024 * 1024;
		sock->rcvbuf = sz;
		return 0;
	}
	case SO_SNDBUF: {
		int sz;
		if (!optval || optlen < sizeof(int))
			return -AU_ERR_EINVAL;
		memcpy(&sz, optval, sizeof(int));
		if (sz < 2048)
			return -AU_ERR_EINVAL;
		if (sz > (1024 * 1024))
			sz = 1024 * 1024;
		sock->sndbuf = sz;
		return 0;
	}
	case SO_ERROR:
	case SO_TYPE:
	case SO_ACCEPTCONN:
		/* Read-only. */
		return -AU_ERR_ENOPROTOOPT;
	default:
		return -AU_ERR_ENOPROTOOPT;
	}
}

static int AuSocketTCPOption(AuSocket* sock, int optname, const void* optval, socklen_t optlen) {
	int on;
	if (!sock || sock->so_type != SOCK_STREAM)
		return -AU_ERR_ENOPROTOOPT;
	switch (optname) {
	case TCP_NODELAY:
		if (!optval || optlen < sizeof(int))
			return -AU_ERR_EINVAL;
		memcpy(&on, optval, sizeof(int));
		sock->nodelay = on ? 1 : 0;
		return 0;
	default:
		return -AU_ERR_ENOPROTOOPT;
	}
}

int AuSocketSetOpt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	if (!sock)
		return -AU_ERR_ENOTSOCK;
	switch (level) {
	case SOL_SOCKET:
		return AuSocketSOSocket(sock, optname, optval, optlen);
	case IPPROTOCOL_TCP:
		return AuSocketTCPOption(sock, optname, optval, optlen);
	default:
		return -AU_ERR_ENOPROTOOPT;
	}
}

static int AuSocketPutOpt(void* optval, socklen_t* optlen, const void* src, socklen_t len) {
	if (!optlen || !optval)
		return -AU_ERR_EFAULT;
	if (*optlen < len) {
		*optlen = len;
		return -AU_ERR_EINVAL;
	}
	memcpy(optval, src, len);
	*optlen = len;
	return 0;
}

int AuSocketGetOpt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
	AuSocket* sock = AuSocketFromFd(sockfd);
	int val;
	int rc;
	char ifname[32];

	if (!sock)
		return -AU_ERR_ENOTSOCK;
	if (!optlen)
		return -AU_ERR_EFAULT;

	if (level == IPPROTOCOL_TCP) {
		if (sock->so_type != SOCK_STREAM)
			return -AU_ERR_ENOPROTOOPT;
		if (optname == TCP_NODELAY) {
			val = sock->nodelay;
			return AuSocketPutOpt(optval, optlen, &val, sizeof(val));
		}
		return -AU_ERR_ENOPROTOOPT;
	}
	if (level != SOL_SOCKET)
		return -AU_ERR_ENOPROTOOPT;

	switch (optname) {
	case SO_ERROR:
		val = sock->so_error;
		rc = AuSocketPutOpt(optval, optlen, &val, sizeof(val));
		/* SO_ERROR is consumed only when the caller actually received
		 * the value.  A short buffer reports EINVAL and keeps the error
		 * pending for the next getsockopt(). */
		if (rc == 0)
			sock->so_error = 0;
		return rc;
	case SO_TYPE:
		val = sock->so_type;
		return AuSocketPutOpt(optval, optlen, &val, sizeof(val));
	case SO_ACCEPTCONN:
		val = sock->listening;
		return AuSocketPutOpt(optval, optlen, &val, sizeof(val));
	case SO_NONBLOCK:
		val = sock->nonblock;
		return AuSocketPutOpt(optval, optlen, &val, sizeof(val));
	case SO_KEEPALIVE:
		val = sock->keepalive;
		return AuSocketPutOpt(optval, optlen, &val, sizeof(val));
	case SO_REUSEADDR:
		val = sock->reuseaddr;
		return AuSocketPutOpt(optval, optlen, &val, sizeof(val));
	case SO_RCVBUF:
		val = sock->rcvbuf;
		return AuSocketPutOpt(optval, optlen, &val, sizeof(val));
	case SO_SNDBUF:
		val = sock->sndbuf;
		return AuSocketPutOpt(optval, optlen, &val, sizeof(val));
	case SO_BINDTODEVICE: {
		AuVFSNode* nic = (AuVFSNode*)sock->binedDev;
		if (!nic)
			return AuSocketPutOpt(optval, optlen, "", 1);
		memset(ifname, 0, sizeof(ifname));
		strncpy(ifname, nic->filename, sizeof(ifname) - 1);
		return AuSocketPutOpt(optval, optlen, ifname, (socklen_t)(strlen(ifname) + 1));
	}
	default:
		return -AU_ERR_ENOPROTOOPT;
	}
}

void AuSocketWakeRx(AuSocket* sock) {
	if (sock)
		AuWaitQueue_Wake(&sock->rx_wait);
}

void AuSocketWakeTx(AuSocket* sock) {
	if (sock)
		AuWaitQueue_Wake(&sock->tx_wait);
}

void AuSocketWakeAccept(AuSocket* sock) {
	if (sock)
		AuWaitQueue_Wake(&sock->accept_wait);
}

void AuSocketWaitSlice(AuWaitQueue* q, uint64_t ms) {
	AuWaitQEntry e;
	AA64Thread* thr = AuGetCurrentThread();

	if (!q || !thr)
		return;
	memset(&e, 0, sizeof(e));
	e.thread = thr;
	AuWaitQueueAdd(q, &e);
	AuSleepThread(thr, ms ? ms : 1);
	AuScheduleNext();
	AuWaitQueueRemove(&e);
}

int AuSocketProtocolPoll(AuVFSNode* fs, AuVFSNode* file, AuPollTable* table) {
	AuSocket* sock;

	(void)fs;
	if (!file)
		return POLLNVAL;
	sock = (AuSocket*)file->device;
	if (!sock)
		return POLLNVAL;
	if (table && table->thread) {
		AuPollWait(&sock->rx_wait, table);
		AuPollWait(&sock->tx_wait, table);
		AuPollWait(&sock->accept_wait, table);
	}
	/* poll(2) is the wake-up path for most applications; drain the NIC
	 * here so a poll-only driver still makes progress without relying on
	 * the 1 ms sleep fallback. */
	AuNetRxPoll();
	if (sock->poll)
		return sock->poll(sock, 0);
	/* Unknown protocol: keep legacy behaviour rather than claiming a hang. */
	return POLLIN | POLLOUT;
}
AuSocket* AuNetCreateSocket() {
	AuSocket* sock = (AuSocket*)kmalloc(sizeof(AuSocket));
	if (!sock)
		return NULL;
	memset(sock, 0, sizeof(AuSocket));
	sock->rxstack = AuStackCreate();
	if (!sock->rxstack) {
		kfree(sock);
		return NULL;
	}
	AuWaitQueueInitialize(&sock->rx_wait);
	AuWaitQueueInitialize(&sock->tx_wait);
	AuWaitQueueInitialize(&sock->accept_wait);
	sock->rcvbuf = 65536;
	sock->sndbuf = 65536;
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
	AU_NET_LOG("Raw Socket Closed \r\n");
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
		AU_NET_LOG("[aurora] net route entry added \r\n");
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
		AU_NET_LOG("Populating route table \r\n");
		if (!arg)
			return 1;
		AuRouteEntryInfo* info = (AuRouteEntryInfo*)arg;
		if (info->index == -1)
			return 1;
		if (!info->route_entry)
			return 1;
		AuRouteEntry* entry = (AuRouteEntry*)info->route_entry;
		AuRouteTablePopulate(entry, info->index);
		AU_NET_LOG("[aurora]: route table populated \r\n");
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
		AU_NET_LOG("[aurora]: DNS server idx %d -> %u (global)\r\n",
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
		AU_NET_LOG("[aurora]: Getting dns entry of index -> %d \r\n", dnsentry->index);
		if (AuDnsGetServer4(dnsentry->index, &addr) == 0 && addr != 0) {
			dnsentry->address = addr;
			AU_NET_LOG("[aurora]: dns entry address -> %d \r\n", dnsentry->address);
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
		AU_NET_LOG("[aurora]: dns entry address -> %d \r\n", dnsentry->address);
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
		AU_NET_LOG("[aurora]: nf append hook=%d proto=%d dport=%d target=%d in=%s rc=%d count=%d\r\n",
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
static int AuRawSocketPoll(AuSocket* sock, int events) {
	int mask = POLLOUT;

	(void)events;
	if (!sock)
		return POLLNVAL;
	if (sock->rxstack && sock->rxstack->itemCount)
		mask |= POLLIN;
	return mask;
}

/**
 * @brief AuCreateRawSocket -- creates a very basic raw socket
 * interface
 * @param type -- type of the socket
 * @param protocol -- unused
 */
int AuCreateRawSocket(int type, int protocol) {
	int fd;
	AuSocket* sock;

	(void)protocol;
	if ((type & SOCK_TYPE_MASK) != SOCK_RAW)
		return -AU_ERR_EINVAL;
	sock = AuNetCreateSocket();
	if (!sock)
		return -AU_ERR_EMFILE;
	sock->receive = AuRawSocketReceive;
	sock->send = AuRawSocketSend;
	sock->poll = AuRawSocketPoll;
	sock->so_type = SOCK_RAW;
	sock->family = AF_RAW;

	fd = AuSocketInstallFd(sock, "XERawSocket", AuRawSocketClose);
	if (fd < 0) {
		if (sock->rxstack)
			kfree(sock->rxstack);
		kfree(sock);
		return fd;
	}
	list_add(raw_socket_list, sock);
	AU_NET_LOG("*****///Raw Socket created %x\r\n", sock);
	return fd;
}

/**
 * @brief AuCreateSocket -- create a new socket for specific
 * domain
 * @param domain -- domain type -- IPV4_INET, RAW, IPV6_INET or UNIX
 * @param type -- type, optionally ORed with SOCK_NONBLOCK/SOCK_CLOEXEC
 * @param protocol -- protocol version
 */
int AuCreateSocket(int domain, int type, int protocol) {
	int stype = type & SOCK_TYPE_MASK;
	int flags = type & ~SOCK_TYPE_MASK;
	int fd;
	AuSocket* sock;

	if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC))
		return -AU_ERR_EINVAL;
	if (stype != SOCK_STREAM && stype != SOCK_DGRAM && stype != SOCK_RAW)
		return -AU_ERR_EINVAL;

	switch (domain) {
	case AF_INET:
		fd = CreateIPv4Socket(stype, protocol);
		break;
	case AF_INET6:
		fd = CreateIPv6Socket(stype, protocol);
		break;
	case AF_RAW:
		fd = AuCreateRawSocket(stype, protocol);
		break;
	case AF_UNIX:
		fd = AuUnixCreateSocket(stype, protocol);
		break;
	default:
		return -AU_ERR_EAFNOSUPPORT;
	}
	if (fd < 0)
		return fd;

	sock = AuSocketFromFd(fd);
	if (sock) {
		sock->nonblock = (flags & SOCK_NONBLOCK) ? 1 : 0;
		sock->cloexec = (flags & SOCK_CLOEXEC) ? 1 : 0;
		sock->family = (uint8_t)domain;
	}
	return fd;
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
