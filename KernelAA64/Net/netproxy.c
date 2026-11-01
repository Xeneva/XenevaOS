/**
 * netproxy.c -- kernel half of a user-space NIC driver.
 *
 * A server process (Process/netsrv) that owns the network card through
 * /dev/hwdev passes the kernel a shared ring (see BaseHdr/netsrv.h). The kernel
 * registers the same adapters the in-kernel virtio-net driver did, but their
 * write() queues a frame in the TX ring and the stack's RX poll drains the RX
 * ring. The rest of the network stack cannot tell the difference.
 *
 * Safety rules:
 *   - the ring must be one of the caller's own hwdev DMA buffers
 *   - nothing here ever waits for the server; a full TX ring drops the frame
 *   - frames from the ring are length-checked and copied out before parsing
 *   - when the server's device handle goes away, the adapters are removed
 *     BEFORE the ring memory is freed
 *   - the AuNetworkDevice (IP configuration) outlives a server restart
 */
#include <Net/netproxy.h>
#include <Net/aunet.h>
#include <Net/ethernet.h>
#include <Fs/Dev/devhw.h>
#include <Fs/Dev/devfs.h>
#include <Fs/vfs.h>
#include <Cred/group.h>
#include <Mm/kmalloc.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <netsrv.h>
#include <timer.h>
#include <string.h>
#include <_null.h>

static struct {
	volatile bool attached;
	volatile int inflight;
	AuProcess* owner;
	NetRingHdr* hdr;
	uint8_t* txslots;
	uint8_t* rxslots;
	AuVFSNode* nic;            /* "e1000" */
	AuVFSNode* alias;          /* "virtio-net" */
	AuNetworkDevice* ndev;     /* kept across attach cycles */
	uint64_t tx_frames, rx_frames, tx_dropped, rx_bad;
	uint8_t rxcopy[NETSRV_MAX_FRAME];
} np;

static inline void np_barrier(void) {
	__asm__ volatile("dmb ish" ::: "memory");
}

/* Kernel addresses live in the upper half. Check the range without adding
 * len to a user pointer first, since that addition can wrap. Keep the strict
 * upper-bound behavior used by the other device-pointer validators. */
static bool np_user_range(const void* ptr, size_t len) {
	const uint64_t user_top = 0x0000800000000000ULL;
	uint64_t start = (uint64_t)ptr;
	return start && start < user_top && len < user_top - start;
}

/* the stack calls this to send; never blocks, never trusts the ring */
static size_t np_write(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	(void)node; (void)file;
	if (!np.attached || !buffer || !len)
		return len;
	np.inflight++;
	np_barrier();
	if (np.attached) {
		NetRingHdr* h = np.hdr;
		if (len > NETSRV_MAX_FRAME)
			len = NETSRV_MAX_FRAME;
		uint32_t head = h->tx_head;
		if (head - h->tx_tail >= NETSRV_SLOTS) {
			h->tx_dropped++;
			np.tx_dropped++;
		} else {
			uint8_t* slot = np.txslots + (head & (NETSRV_SLOTS - 1)) * NETSRV_SLOT_SIZE;
			memcpy(slot + NETSRV_SLOT_HDR, buffer, len);
			*(volatile uint32_t*)slot = len;
			np_barrier();
			h->tx_head = head + 1;
			np.tx_frames++;
		}
	}
	np_barrier();
	np.inflight--;
	return len;
}

/* registered with AuNetRegisterRxPoll: drain frames the server queued */
static void np_rxpoll(void) {
	static int in_poll;
	if (in_poll || !np.attached || !np.nic)
		return;
	in_poll = 1;
	NetRingHdr* h = np.hdr;
	int budget = NETSRV_SLOTS;
	while (np.attached && budget-- > 0) {
		uint32_t tail = h->rx_tail;
		np_barrier();
		if (tail == h->rx_head)
			break;
		uint8_t* slot = np.rxslots + (tail & (NETSRV_SLOTS - 1)) * NETSRV_SLOT_SIZE;
		uint32_t len = *(volatile uint32_t*)slot;
		if (len >= 14 && len <= NETSRV_MAX_FRAME) {
			memcpy(np.rxcopy, slot + NETSRV_SLOT_HDR, len);
			np_barrier();
			h->rx_tail = tail + 1;        /* slot is ours to give back now */
			np.rx_frames++;
			AuEthernetHandle(np.rxcopy, (int)len, np.nic);
		} else {
			np.rx_bad++;
			h->rx_tail = tail + 1;
		}
	}
	in_poll = 0;
}

static int np_ioctl_adapter(AuVFSNode* file, int code, void* arg) {
	AuNetworkDevice* ndev = np.ndev;
	(void)file;
	if (!ndev || !arg)
		return 1;
	switch (code) {
	case AUNET_GET_HARDWARE_ADDRESS: memcpy(arg, ndev->mac, 6); return 0;
	case AUNET_GET_IPV4_ADDRESS:     memcpy(arg, &ndev->ipv4addr, sizeof(ndev->ipv4addr)); return 0;
	case AUNET_SET_IPV4_ADDRESS:
		memcpy(&ndev->ipv4addr, arg, sizeof(ndev->ipv4addr));
		if (np.alias) AuNetAddConnectedRoute4(np.alias, "virtio-net");
		return 0;
	case AUNET_GET_GATEWAY_ADDRESS:  memcpy(arg, &ndev->ipv4gateway, sizeof(ndev->ipv4gateway)); return 0;
	case AUNET_SET_GATEWAY_ADDRESS:
		memcpy(&ndev->ipv4gateway, arg, sizeof(ndev->ipv4gateway));
		if (np.alias) AuNetAddDefaultRoute4(np.alias, "virtio-net");
		return 0;
	case AUNET_GET_SUBNET_MASK:      memcpy(arg, &ndev->ipv4subnet, sizeof(ndev->ipv4subnet)); return 0;
	case AUNET_SET_SUBNET_MASK:
		memcpy(&ndev->ipv4subnet, arg, sizeof(ndev->ipv4subnet));
		if (np.alias) AuNetAddConnectedRoute4(np.alias, "virtio-net");
		return 0;
	case AUNET_GET_LINK_STATUS:      memcpy(arg, &ndev->linkStatus, sizeof(ndev->linkStatus)); return 0;
	case AUNET_GET_IPV6_ADDRESS:     memcpy(arg, &ndev->ipv6addr, sizeof(ndev->ipv6addr)); return 0;
	case AUNET_SET_IPV6_ADDRESS:     memcpy(&ndev->ipv6addr, arg, sizeof(ndev->ipv6addr)); return 0;
	case AUNET_GET_IPV6_GATEWAY:     memcpy(arg, &ndev->ipv6gateway, sizeof(ndev->ipv6gateway)); return 0;
	case AUNET_SET_IPV6_GATEWAY:     memcpy(&ndev->ipv6gateway, arg, sizeof(ndev->ipv6gateway)); return 0;
	case AUNET_GET_IPV6_PREFIX:      memcpy(arg, &ndev->ipv6prefixLen, sizeof(ndev->ipv6prefixLen)); return 0;
	case AUNET_SET_IPV6_PREFIX:      memcpy(&ndev->ipv6prefixLen, arg, sizeof(ndev->ipv6prefixLen)); return 0;
	}
	return 1;
}

static AuVFSNode* np_make_node(const char* name) {
	AuVFSNode* n = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(n, 0, sizeof(AuVFSNode));
	strcpy(n->filename, name);
	n->flags = FS_FLAG_DEVICE;
	n->write = np_write;
	n->iocontrol = np_ioctl_adapter;
	n->device = np.ndev;
	return n;
}

static void np_detach(void) {
	if (!np.attached)
		return;
	np.attached = false;                 /* writers and the poller bail out */
	np_barrier();
	AuNetRegisterRxPoll(NULL);
	AuRemoveNetAdapter((char*)"virtio-net");     /* nodes stay, now dead */
	AuRemoveNetAdapter((char*)"e1000");
	np.nic = np.alias = NULL;
	uint64_t t0 = AuGetCurrentMS();
	while (np.inflight > 0 && AuGetCurrentMS() - t0 < 200)
		__asm__ volatile("yield");
	np.hdr = NULL;
	np.txslots = np.rxslots = NULL;
	np.owner = NULL;
	if (np.ndev)
		np.ndev->linkStatus = 0;
	AU_NET_LOG("[netproxy]: detached (tx %d, rx %d, tx dropped %d, rx bad %d)\r\n",
				 (int)np.tx_frames, (int)np.rx_frames, (int)np.tx_dropped, (int)np.rx_bad);
}

void AuNetProxyOwnerGone(AuProcess* proc) {
	if (np.attached && np.owner == proc)
		np_detach();
}

static int np_ioctl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	AuProcess* proc = AuHwdevCurrentProcess();
	if (!AuHwdevCallerAllowed(proc))
		return NETSRV_E_PERM;

	switch (code) {
	case NETSRV_IOC_ATTACH: {
		if (!np_user_range(arg, sizeof(NetsrvAttach)))
			return NETSRV_E_INVAL;
		NetsrvAttach* req = (NetsrvAttach*)arg;
		if (np.attached)
			return NETSRV_E_BUSY;
		if (req->ring_len < NETSRV_RING_BYTES || (req->ring_phys & 0xFFF))
			return NETSRV_E_INVAL;
		if (!AuHwdevOwnsDma(proc, req->ring_phys, req->ring_len))
			return NETSRV_E_PERM;
		NetRingHdr* hdr = (NetRingHdr*)P2V(req->ring_phys);
		if (hdr->magic != NETSRV_RING_MAGIC || hdr->slots != NETSRV_SLOTS ||
			hdr->slot_size != NETSRV_SLOT_SIZE)
			return NETSRV_E_INVAL;

		hdr->tx_head = hdr->tx_tail = 0;
		hdr->rx_head = hdr->rx_tail = 0;
		hdr->tx_dropped = hdr->rx_dropped = 0;

		if (!np.ndev) {
			np.ndev = (AuNetworkDevice*)kmalloc(sizeof(AuNetworkDevice));
			memset(np.ndev, 0, sizeof(AuNetworkDevice));
			np.ndev->type = NETDEV_TYPE_ETHERNET;
			np.ndev->ipv4addr = MAKE_IP(10, 0, 2, 15);
			np.ndev->ipv4gateway = MAKE_IP(10, 0, 2, 2);
			np.ndev->ipv4subnet = MAKE_IP(255, 255, 255, 0);
			np.ndev->dns_ipv4_1 = MAKE_IP(10, 0, 2, 3);
			np.ndev->ipv6addr.s6_addr[0] = 0xfe;
			np.ndev->ipv6addr.s6_addr[1] = 0xc0;
			np.ndev->ipv6addr.s6_addr[15] = 0x64;
			np.ndev->ipv6gateway.s6_addr[0] = 0xfe;
			np.ndev->ipv6gateway.s6_addr[1] = 0xc0;
			np.ndev->ipv6gateway.s6_addr[15] = 0x02;
			np.ndev->ipv6prefixLen = 64;
		}
		memcpy(np.ndev->mac, (const void*)hdr->mac, 6);
		np.ndev->linkStatus = 1;

		np.hdr = hdr;
		np.txslots = (uint8_t*)hdr + NETSRV_TX_OFFSET;
		np.rxslots = (uint8_t*)hdr + NETSRV_RX_OFFSET;
		np.owner = proc;
		np.inflight = 0;
		np.nic = np_make_node("e1000");
		np.alias = np_make_node("virtio-net");
		np_barrier();
		np.attached = true;
		np.nic = AuAddNetAdapter(np.nic, "e1000");
		np.alias = AuAddNetAdapter(np.alias, "virtio-net");
		AuNetAddConnectedRoute4(np.alias, "virtio-net");
		AuNetAddDefaultRoute4(np.alias, "virtio-net");
		AuNetAddConnectedRoute6(np.alias, "virtio-net");
		AuNetAddDefaultRoute6(np.alias, "virtio-net");
		AuNetRegisterRxPoll(np_rxpoll);
		AU_NET_LOG("[netproxy]: attached ring %x for pid %d, adapters registered\r\n",
					 (uint32_t)req->ring_phys, proc->proc_id);
		return NETSRV_OK;
	}
	case NETSRV_IOC_DETACH:
		if (np.attached && np.owner == proc) {
			np_detach();
			return NETSRV_OK;
		}
		return NETSRV_E_INVAL;
	case NETSRV_IOC_STATS: {
		if (!np_user_range(arg, sizeof(NetsrvStats)))
			return NETSRV_E_INVAL;
		NetsrvStats* st = (NetsrvStats*)arg;
		st->tx_frames = np.tx_frames;
		st->rx_frames = np.rx_frames;
		st->tx_dropped = np.tx_dropped;
		st->rx_bad = np.rx_bad;
		return NETSRV_OK;
	}
	}
	return NETSRV_E_INVAL;
}

void AuNetProxyInitialize(void) {
	AuVFSNode* fsys = AuVFSFind("/dev");
	if (!fsys)
		return;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "netsrv");
	node->flags = FS_FLAG_DEVICE;
	node->uid = 0;
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_DAEMONS);
	node->device = fsys;
	node->iocontrol = np_ioctl;
	AuDevFSAddFile(fsys, "/", node);
	AU_NET_LOG("[netproxy]: /dev/netsrv registered\r\n");
}
