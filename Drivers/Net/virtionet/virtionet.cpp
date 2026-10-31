/**
* @file virtionet.c
* 
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

#include <pcie.h>
#include <audrv.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/gic.h>
#include <Fs/Dev/devinput.h>
#include <Fs/vfs.h>
#include <Drivers/virtio.h>
#include <Drivers/uart.h>
#include <Net/wifi.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <aucon.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/sched.h>
#include <Net/aunet.h>
#include <Net/ethernet.h>
#include <string.h>

/* Deep enough to absorb a slirp burst without dropping frames or stalling
 * the TX ring. 64 * 2 KiB = 128 KiB per direction. */
#define RX_BUFFER_COUNT 64
#define TX_BUFFER_COUNT 64
#define TX_BUFFER_SIZE	2048
#define RX_BUFFER_SIZE	2048
#define MAKE_IP(a, b, c, d)                                                                        \
	((uint32_t)(d) << 24 | (uint32_t)(c) << 16 | (uint32_t)(b) << 8 | (uint32_t)(a))

static struct VirtioPCIDevice netDev;
static struct VirtqDesc* rx_desc;
static struct VirtqAvailHdr* rx_avail;
static struct VirtqUsedHdr* rx_used;
static uint16_t rx_qsize;
static uint16_t rx_nbuf;
static uint64_t rx_buf_phys;
static uint8_t* rx_buf_virt;
static struct VirtqDesc* tx_desc;
static struct VirtqAvailHdr* tx_avail;
static struct VirtqUsedHdr* tx_used;
static uint16_t tx_qsize;
static uint16_t tx_nbuf;
static uint64_t tx_buf_phys;
static uint8_t* tx_buf_virt;
static uint16_t rx_index;
/* TX completion cursor: how far through tx_used the driver has consumed.
 * Transmit must never lap it (see AuVirtioTransmit). */
static uint16_t tx_done;
static AuVFSNode* nic;
static AuNetworkDevice* ndev;
/* MSI-X vector 0 is bound to both queues when allocation succeeds.
 * VIRTIO_MSI_NO_VECTOR keeps the old poll-only path as a fallback. */
static uint16_t net_msix_vector = VIRTIO_MSI_NO_VECTOR;
static int net_spi = -1;

/**
 * VirtioNetCfg -- virtio net configuration
 * structure, this structure can be found
 * at device configration structure offset
 * from PCIe capabilities area
 */
struct VirtioNetCfg {
	uint8_t mac[6];
	uint16_t status;
	uint16_t maxVirtqueuePairs;
	uint16_t mtu;
	uint32_t speed;
	uint8_t duplex;
	uint8_t rssMaxKeySz;
	uint16_t rssMaxIndirectionTableLen;
	uint32_t supportedHashTypes;
	uint32_t supportedTunnelTypes;
};
static void virt_cache_inv(void* addr, size_t sz) {
	uint64_t p = (uint64_t)addr & ~63ULL;
	uint64_t end = (uint64_t)addr + sz;
	for (; p < end; p += 64)
		__asm__ volatile("dc ivac, %0" : : "r"(p) : "memory");
	dsb_ish();
}

static void virt_cache_clean(void* addr, size_t sz) {
	uint64_t p = (uint64_t)addr & ~63ULL;
	uint64_t end = (uint64_t)addr + sz;
	for (; p < end; p += 64)
		__asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
	dsb_ish();
}

static void AuVirtioNetRxPoll(void) {
	static int in_poll;
	uint16_t budget;
	uint32_t total = 0;
	int more;

	if (in_poll)
		return;
	if (!rx_used || !rx_avail || !rx_desc || !rx_qsize || !rx_nbuf || !rx_buf_virt)
		return;
	in_poll = 1;
	/* NAPI-style suppression: while this pass drains, the device must not
	 * raise an interrupt per completed buffer. Re-arm afterwards and look
	 * once more, so a completion that landed in the window between the
	 * last check and the flag clear is still handled here. */
	for (;;) {
		rx_avail->flags = VIRTQ_AVAIL_F_NO_INTERRUPT;
		dsb_ish();
		isb_flush();
		budget = rx_qsize;
		while (budget-- && rx_index != rx_used->idx) {
			uint16_t used_slot = rx_index % rx_qsize;
			uint32_t buf_id = rx_used->ring[used_slot].id % rx_nbuf;
			uint32_t totlen = rx_used->ring[used_slot].len;
			uint8_t* buffer = rx_buf_virt + (buf_id * RX_BUFFER_SIZE);
			void* eth = buffer + sizeof(virtio_net_hdr_t);
			uint32_t ethlen =
				totlen > sizeof(virtio_net_hdr_t) ? totlen - (uint32_t)sizeof(virtio_net_hdr_t) : 0;

			/* The device owns this buffer until it is placed back on the
			 * available ring below, so invalidate before reading it. */
			if (ethlen)
				virt_cache_inv(buffer, totlen);
			if (nic && ethlen) {
				if (!AuWifiPortalRx(eth, (int)ethlen))
					AuEthernetHandle(eth, (int)ethlen, nic);
			}
			rx_avail->ring[rx_avail->idx % rx_qsize] = (uint16_t)buf_id;
			rx_avail->idx++;
			dsb_ish();
			isb_flush();
			rx_index++;
			total++;
		}
		rx_avail->flags = 0;
		dsb_ish();
		isb_flush();
		more = (rx_index != rx_used->idx);
		/* Bound one invocation so a continuously fed queue cannot pin the
		 * CPU in the interrupt handler; the next poll picks up the rest. */
		if (!more || total >= (uint32_t)rx_qsize * 2)
			break;
	}
	in_poll = 0;
}

void AuVirtioNetHandler(int spiNum) {
	(void)spiNum;
	/* Go through the core poll so the deferred-ACK batch is flushed even
	 * when the drain was driven by an interrupt rather than a syscall. */
	AuNetRxPoll();
}

static void AuVirtioNetRxinitialize(void) {
	uint64_t rxbuff;
	uint32_t pages;

	rx_qsize =
		AuVirtioPCISetupQueue(&netDev, 0, &rx_desc, &rx_avail, &rx_used, net_msix_vector);
	if (!rx_qsize || !rx_desc || !rx_avail || !rx_used) {
		AU_NET_LOG("[aurora]: virtio-net RX queue setup failed\r\n");
		return;
	}
	rx_nbuf = rx_qsize;
	AU_NET_LOG("[aurora]: rx queue size : %d nbuf=%d\r\n", rx_qsize, rx_nbuf);

	pages = (rx_nbuf * RX_BUFFER_SIZE + 4095) / 4096;
	if (pages < 4)
		pages = 4;
	rxbuff = (uint64_t)AuPmmngrAllocPages(pages, 1, 0, AURORA_PAGE_DMA);
	if (!rxbuff || rxbuff == UINT64_MAX) {
		AU_NET_LOG("[aurora]: unable to allocate contiguous virtio RX buffer\r\n");
		return;
	}
	rx_buf_phys = rxbuff;
	rx_buf_virt = (uint8_t*)P2V(rxbuff);
	AuVirtioPCIPostAvail(
		rx_desc, rx_avail, rx_qsize, rxbuff, RX_BUFFER_SIZE, rx_nbuf, VIRTQ_DESC_F_WRITE);
	AuVirtioPCINotifyQueue(&netDev, 0);
	AU_NET_LOG("[aurora]: virtio rx queue initialized hdr=%d\r\n", (int)sizeof(virtio_net_hdr_t));
}

static void AuVirtioNetTxinitialize(void) {
	uint64_t txbuff;
	uint32_t pages;
	int i;

	tx_qsize =
		AuVirtioPCISetupQueue(&netDev, 1, &tx_desc, &tx_avail, &tx_used, net_msix_vector);
	if (!tx_qsize || !tx_desc || !tx_avail || !tx_used) {
		AU_NET_LOG("[aurora]: virtio-net TX queue setup failed\r\n");
		return;
	}
	tx_nbuf = tx_qsize > TX_BUFFER_COUNT ? TX_BUFFER_COUNT : tx_qsize;
	AU_NET_LOG("[aurora]: tx queue size : %d nbuf=%d\r\n", tx_qsize, tx_nbuf);

	pages = (tx_nbuf * TX_BUFFER_SIZE + 4095) / 4096;
	if (pages < 4)
		pages = 4;
	txbuff = (uint64_t)AuPmmngrAllocPages(pages, 1, 0, AURORA_PAGE_DMA);
	if (!txbuff || txbuff == UINT64_MAX) {
		AU_NET_LOG("[aurora]: unable to allocate virtio TX buffer\r\n");
		return;
	}
	tx_buf_phys = txbuff;
	tx_buf_virt = (uint8_t*)P2V(txbuff);
	for (i = 0; i < (int)tx_nbuf; i++) {
		tx_desc[i].addr = txbuff + (uint64_t)i * TX_BUFFER_SIZE;
		tx_desc[i].len = TX_BUFFER_SIZE;
		tx_desc[i].flags = 0;
		tx_desc[i].next = 0;
	}
	tx_avail->idx = 0;
	/* TX completions only free ring slots, and AuVirtioTransmit reclaims
	 * them by reading tx_used->idx before it needs a slot. Suppressing the
	 * notification keeps every ACK from raising an interrupt on the same
	 * MSI-X vector the RX queue uses. */
	tx_avail->flags = VIRTQ_AVAIL_F_NO_INTERRUPT;
	dsb_ish();
	isb_flush();
	AU_NET_LOG("[aurora]: virtio tx queue initialized\r\n");
}

/**
 * @brief AuVirtioTransmit -- send one Ethernet frame on virtio-net
 * @param packet -- frame bytes
 * @param len -- length in bytes
 * @return 0 on kick, -1 when the TX ring stays full (caller retries)
 *
 * The 8 TX buffers round-robin, so a slot may only be reused once the
 * device reports its previous use complete. Without this gate, bursts
 * rewrite descriptors QEMU has not consumed yet and the wire sees
 * spliced/corrupt frames (TCP stalls that never recover).
 */
static int AuVirtioTransmit(void* packet, uint16_t len) {
	uint16_t idx;
	uint8_t* buff;
	virtio_net_hdr_t* hdr;
	uint16_t total;
	if (!tx_desc || !tx_avail || !tx_used || !tx_qsize || !tx_nbuf || !tx_buf_virt)
		return -1;
	/* Reclaim completions first; then require a free slot. The used
	 * ring lives in device memory like the RX path's, so a plain read
	 * is coherent. Bounded spin: a wedged device must not hang the
	 * caller forever. */
	uint64_t spins = 0;
	for (;;) {
		uint16_t done = tx_used->idx;
		while (tx_done != done)
			tx_done++;
		if ((uint16_t)(tx_avail->idx - tx_done) < tx_nbuf)
			break;
		if (++spins > 2000000)
			return -1;
	}
	idx = tx_avail->idx % tx_nbuf;
	buff = tx_buf_virt + (idx * TX_BUFFER_SIZE);
	hdr = (virtio_net_hdr_t*)buff;
	/* Only the 12-byte virtio header needs clearing; the frame bytes are
	 * fully overwritten below. Zeroing the whole 2 KiB slot per ACK was
	 * pure overhead on the transmit path. */
	hdr->flags = 0;
	hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
	hdr->hdr_len = 0;
	hdr->gso_size = 0;
	hdr->csum_start = 0;
	hdr->csum_offset = 0;
	hdr->num_buffers = 0;
	if (len + sizeof(virtio_net_hdr_t) > TX_BUFFER_SIZE)
		len = TX_BUFFER_SIZE - sizeof(virtio_net_hdr_t);
	memcpy(buff + sizeof(virtio_net_hdr_t), packet, len);
	total = (uint16_t)(len + sizeof(virtio_net_hdr_t));
	tx_desc[idx].addr = tx_buf_phys + (uint64_t)idx * TX_BUFFER_SIZE;
	tx_desc[idx].len = total;
	tx_desc[idx].flags = 0;
	tx_desc[idx].next = 0;
	tx_avail->ring[tx_avail->idx % tx_qsize] = idx;
	virt_cache_clean(buff, total);
	tx_avail->idx++;
	dsb_ish();
	isb_flush();
	AuVirtioPCINotifyQueue(&netDev, 1);
	return 0;
}

extern "C" void AuVirtioNetPortalTx(void* packet, uint16_t len) {
	AuVirtioTransmit(packet, len);
}

/**
 * @brief AuVirtioWrite -- VFS write callback for the NIC
 * @param node -- unused
 * @param file -- unused
 * @param buffer -- frame to send
 * @param len -- length in bytes
 */
static size_t AuVirtioWrite(AuVFSNode* node, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	(void)node;
	(void)file;
	if (AuVirtioTransmit(buffer, (uint16_t)len) != 0)
		return 0;
	return len;
}

/**
 * @brief AuVirtioNetIOCtl -- NIC ioctl (MAC, IPv4, gateway, mask, link)
 * @param file -- unused
 * @param code -- AUNET_* request
 * @param arg -- user buffer
 */
static int AuVirtioNetIOCtl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	if (!ndev || !arg)
		return 1;
	switch (code) {
	case AUNET_GET_HARDWARE_ADDRESS:
		AU_NET_LOG("VirtioNet getting mac address \r\n");
		memcpy(arg, ndev->mac, 6);
		return 0;
	case AUNET_GET_IPV4_ADDRESS:
		memcpy(arg, &ndev->ipv4addr, sizeof(ndev->ipv4addr));
		return 0;
	case AUNET_SET_IPV4_ADDRESS:
		memcpy(&ndev->ipv4addr, arg, sizeof(ndev->ipv4addr));
		if (nic)
			AuNetAddConnectedRoute4(nic, "virtio-net");
		return 0;
	case AUNET_GET_GATEWAY_ADDRESS:
		memcpy(arg, &ndev->ipv4gateway, sizeof(ndev->ipv4gateway));
		return 0;
	case AUNET_SET_GATEWAY_ADDRESS:
		memcpy(&ndev->ipv4gateway, arg, sizeof(ndev->ipv4gateway));
		if (nic)
			AuNetAddDefaultRoute4(nic, "virtio-net");
		return 0;
	case AUNET_GET_SUBNET_MASK:
		memcpy(arg, &ndev->ipv4subnet, sizeof(ndev->ipv4subnet));
		return 0;
	case AUNET_SET_SUBNET_MASK:
		memcpy(&ndev->ipv4subnet, arg, sizeof(ndev->ipv4subnet));
		if (nic)
			AuNetAddConnectedRoute4(nic, "virtio-net");
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

/**
 * @brief AuVirtioNetInitialize -- initialize the virtio network device
 * @param device -- device address passed by PCIe
 */
void AuVirtioNetInitialize(uint64_t device, int bus, int dev, int func) {
	struct VirtioNetCfg* netcfg;
	int i;

	AU_NET_LOG("[aurora]: virtio network device found \r\n");
	if (device == 0xFFFFFFFF)
		return;
	rx_index = 0;
	tx_done = 0;

	if (!AuVirtioPCIInit(device, bus, dev, func, (uint32_t)VIRTIO_NET_F_MAC, &netDev)) {
		AU_NET_LOG("[aurora]: virtio-net PCI init failed\r\n");
		return;
	}

	/* Bind both queues to MSI-X vector 0. Without this the device never
	 * raises an RX interrupt, so every blocking receive falls back to a
	 * polling slice. If MSI-X cannot be set up the driver stays poll-only. */
	net_spi = AuGICAllocateSPI();
	if (net_spi >= 0 && AuPCIEAllocMSI(device, (size_t)net_spi, bus, dev, func)) {
		net_msix_vector = 0;
		GICEnableSPIIRQ((uint32_t)net_spi);
		GICRegisterSPIHandler((void*)&AuVirtioNetHandler, net_spi);
		AU_NET_LOG("[aurora]: virtio-net MSI-X spi=%d vector=%d\r\n",
			net_spi, net_msix_vector);
	} else {
		net_msix_vector = VIRTIO_MSI_NO_VECTOR;
		AU_NET_LOG("[aurora]: virtio-net MSI-X unavailable, using polling\r\n");
	}

	AuVirtioNetRxinitialize();
	AuVirtioNetTxinitialize();
	if (!rx_qsize || !tx_qsize)
		return;

	netDev.common->DeviceStatus |= VIRTIO_STATUS_DRIVER_OK;
	isb_flush();
	dsb_ish();
	AuVirtioPCINotifyQueue(&netDev, 0);
	AuVirtioPCINotifyQueue(&netDev, 1);

	AU_NET_LOG("[aurora]: virtio-net-dev initialized successfully status=%x\r\n",
				 netDev.common->DeviceStatus);
	AU_NET_LOG("[aurora]: virtio-net-dev mac : ");
	ndev = (AuNetworkDevice*)kmalloc(sizeof(AuNetworkDevice));
	memset(ndev, 0, sizeof(AuNetworkDevice));
	ndev->type = NETDEV_TYPE_ETHERNET;
	ndev->linkStatus = 1;
	ndev->ipv4addr = MAKE_IP(10, 0, 2, 15);
	ndev->ipv4gateway = MAKE_IP(10, 0, 2, 2);
	ndev->ipv4subnet = MAKE_IP(255, 255, 255, 0);
	ndev->dns_ipv4_1 = MAKE_IP(10, 0, 2, 3);
	/* QEMU slirp IPv6: guest fec0::64, router fec0::2 (ping -6 fec0::2). */
	ndev->ipv6addr.s6_addr[0] = 0xfe;
	ndev->ipv6addr.s6_addr[1] = 0xc0;
	ndev->ipv6addr.s6_addr[15] = 0x64;
	ndev->ipv6gateway.s6_addr[0] = 0xfe;
	ndev->ipv6gateway.s6_addr[1] = 0xc0;
	ndev->ipv6gateway.s6_addr[15] = 0x02;
	ndev->ipv6prefixLen = 64;
	netcfg = (struct VirtioNetCfg*)netDev.deviceCfg;
	for (i = 0; i < 6; i++) {
		uint8_t b = netcfg ? netcfg->mac[i] : 0;
		AU_NET_LOG("%x::", b);
		ndev->mac[i] = b;
	}
	AU_NET_LOG("\r\n");

	nic = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(nic, 0, sizeof(AuVFSNode));
	strcpy(nic->filename, "e1000");
	nic->flags = FS_FLAG_DEVICE;
	nic->write = AuVirtioWrite;
	nic->iocontrol = AuVirtioNetIOCtl;
	nic->device = ndev;
	nic = AuAddNetAdapter(nic, "e1000");
	{
		AuVFSNode* alias = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
		memset(alias, 0, sizeof(AuVFSNode));
		strcpy(alias->filename, "virtio-net");
		alias->flags = FS_FLAG_DEVICE;
		alias->write = AuVirtioWrite;
		alias->iocontrol = AuVirtioNetIOCtl;
		alias->device = ndev;
		alias = AuAddNetAdapter(alias, "virtio-net");
		AuNetAddConnectedRoute4(alias, "virtio-net");
		AuNetAddDefaultRoute4(alias, "virtio-net");
		AuNetAddConnectedRoute6(alias, "virtio-net");
		AuNetAddDefaultRoute6(alias, "virtio-net");
		AuNetRegisterRxPoll(AuVirtioNetRxPoll);
		AuWifiPortalSetTx(AuVirtioNetPortalTx);
	}
}

/*
* AuDriverUnload -- deattach the driver from
* aurora system
*/
AU_DRIVER_FLAGS(AU_DRV_FLAG_UNLOADABLE);

AU_EXTERN AU_EXPORT int AuDriverUnload() {
	/* 1. stop the kernel from calling into this image */
	AuNetRegisterRxPoll(0);
	AuWifiPortalSetTx(0);
	AuRemoveNetAdapter((char*)"virtio-net"); /* frees the node */
	AuRemoveNetAdapter((char*)"e1000");
	nic = 0;
	/* 2. reset the device so it stops touching our buffers */
	if (netDev.common)
		netDev.common->DeviceStatus = 0;
	isb_flush();
	dsb_ish();
	/* queue memory and the interrupt are released by the loader's ledger */
	rx_qsize = tx_qsize = 0;
	rx_used = 0;
	rx_avail = 0;
	rx_desc = 0;
	rx_buf_virt = 0;
	tx_used = 0;
	tx_avail = 0;
	tx_desc = 0;
	tx_buf_virt = 0;
	if (ndev) {
		kfree(ndev);
		ndev = 0;
	}
	memset(&netDev, 0, sizeof(netDev));
	return 0;
}

/**
* AuDriverMain -- Main entry for virtio net driver
*/
AU_EXTERN AU_EXPORT int AuDriverMain() {
	int bus = 0, dev = 0, func = 0;
	uint64_t device = AuPCIEScanClass(0x02, 0x00, &bus, &dev, &func);
	if (device == 0xFFFFFFFF)
		return 1;
	AuVirtioNetInitialize(device, bus, dev, func);
	return 0;
}
