/**
* @file virtionet.c
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/gic.h>
#include <Fs/Dev/devinput.h>
#include <Fs/vfs.h>
#include <Drivers/virtio.h>
#include <Drivers/uart.h>
#include <Mm/pmmngr.h>
#include <Mm/kmalloc.h>
#include <aucon.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/sched.h>
#include <Net/aunet.h>
#include <Net/ethernet.h>
#include <string.h>

#define VIRTIO_F_VERSION_1		  (1ull << 32)
#define RX_BUFFER_COUNT			  8
#define TX_BUFFER_COUNT			  8
#define TX_BUFFER_SIZE			  2048
#define RX_BUFFER_SIZE			  2048
#define VIRTIO_PCI_CAP_ID		  0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

struct VirtioQueue* rxqueue;
struct VirtioQueue* txqueue;
volatile uint8_t* notifyBase;
uint32_t notifyOffMultiplier;
static virtio_net_hdr_t* rx_hdrs;
static uint16_t rx_index;
static uint16_t tx_index;
static struct VirtioCommonCfg* _cfg;
static AuVFSNode* nic;
static AuNetworkDevice* ndev;

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
/**
 * @brief AuVirtioNetHandler -- interrupt handler for
 * virtio-net-dev
 * @param spiNum -- shared peripheral interrupt number
 * passed by system
 */
static void AuVirtioNetRxPoll(void) {
	uint16_t them;
	if (!rxqueue)
		return;
	them = rxqueue->used.index;
	for (; rx_index != them; rx_index++) {
		uint32_t used_slot = rx_index % RX_BUFFER_COUNT;
		uint32_t buf_id = rxqueue->used.ring[used_slot].index % RX_BUFFER_COUNT;
		uint8_t* buffer = (uint8_t*)rx_hdrs + buf_id * RX_BUFFER_SIZE;
		uint32_t totlen = rxqueue->used.ring[used_slot].length;
		void* eth = (uint8_t*)buffer + sizeof(virtio_net_hdr_t);
		uint32_t ethlen = totlen > sizeof(virtio_net_hdr_t)
			? totlen - (uint32_t)sizeof(virtio_net_hdr_t)
			: 0;
		if (nic && ethlen)
			AuEthernetHandle(eth, (int)ethlen, nic);
		rxqueue->available.ring[rxqueue->available.index % RX_BUFFER_COUNT] = buf_id;
		rxqueue->available.index++;
		dsb_ish();
		isb_flush();
	}
}

void AuVirtioNetHandler(int spiNum) {
	(void)spiNum;
	AuVirtioNetRxPoll();
}
/**
 * @brief AuVirtioNetReset -- reset the net device
 * @param common -- Pointer to virtio common config desc
 */
static void AuVirtioNetReset(struct VirtioCommonCfg* common) {
	common->DeviceStatus = 0;
	isb_flush();
	dsb_ish();
	UARTDebugOut("[aurora]: virtio net device reset successfull \r\n");
}

/**
 * @brief AuVirtioNetNotifyQueue -- notify queue
 * @param cfg -- Pointer to common queue
 * @param queueIdx -- queue number, zero -- rx queue, one -- tx queue
 */
void AuVirtioNetNotifyQueue(struct VirtioCommonCfg* cfg, uint16_t queueIdx) {
	cfg->QueueSelect = queueIdx;
	isb_flush();
	dsb_ish();
	uint16_t notify_off = cfg->QueueNotifyOff;

	volatile uint16_t* notifyAddr =
		(volatile uint16_t*)((uint64_t)notifyBase + notify_off * notifyOffMultiplier);
	*notifyAddr = queueIdx;

	isb_flush();
	dsb_ish();
}

/**
 * @brief AuVirtioNetFeatureNegotiate -- driver feature negotiation
 * for virtio net device
 * @param common -- Pointer to virtio common config descriptor
 */
static void AuVirtioNetFeatureNegotiate(struct VirtioCommonCfg* common) {
	common->DevFeatureSelect = 0;
	isb_flush();
	dsb_ish();
	uint32_t features_lo = common->DevFeature;
	UARTDebugOut("[aurora]: virtio net device feature lo val : %x \r\n", features_lo);
	common->DevFeatureSelect = 1;
	isb_flush();
	dsb_ish();
	uint32_t feature_hi = common->DevFeature;
	uint64_t features = ((uint64_t)feature_hi << 32) | features_lo;

	if (!(features & VIRTIO_F_VERSION_1))
		UARTDebugOut("[aurora]: warning: virtio-net device is not modern VirtIO! \r\n");

	UARTDebugOut("[aurora]: virtio net device feature hi val : %x \r\n", feature_hi);

	uint64_t guestfeatures = 0;
	guestfeatures |= VIRTIO_F_VERSION_1;
	guestfeatures &= features;

	common->GuestFeatureSelect = 0;
	common->GuestFeature = guestfeatures & UINT32_MAX;
	isb_flush();
	dsb_ish();

	common->GuestFeatureSelect = 1;
	common->GuestFeature = (guestfeatures >> 32) & UINT32_MAX;
	isb_flush();
	dsb_ish();
}

/**
 * @brief AuVirtioNetRxInitialize -- initialize rx queue 
 * @param common -- pointer to virtio common config
 */
void AuVirtioNetRxinitialize(struct VirtioCommonCfg* common) {
	common->QueueSelect = 0;
	isb_flush();
	dsb_ish();
	common->QueueSize = RX_BUFFER_COUNT;
	isb_flush();
	dsb_ish();
	uint16_t qsize = common->QueueSize;
	UARTDebugOut("[aurora]: rx queue size : %d \r\n", qsize);
	uint64_t queuePhys = (uint64_t)
		AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	rxqueue = (struct VirtioQueue*)P2V(queuePhys);
	memset(rxqueue, 0, sizeof(*rxqueue));

	common->QueueDesc = queuePhys;
	common->QueueAvail = (queuePhys) + OFFSETOF(struct VirtioQueue, available);
	common->QueueUsed = (queuePhys) + OFFSETOF(struct VirtioQueue, used);
	common->MSix = 0;
	common->QueueMSixVector = 0;
	common->QueueEnable = 1;
	isb_flush();
	dsb_ish();

	/* I address this as one 16 KiB physical run since the device descriptors expect that --axiss */
	uint64_t rxbuff = (uint64_t)AuPmmngrAllocPages(4, 1, 0, AURORA_PAGE_DMA);
	if (!rxbuff) {
		UARTDebugOut("[aurora]: unable to allocate contiguous virtio RX buffer\r\n");
		return;
	}
	rx_hdrs = (virtio_net_hdr_t*)P2V(rxbuff);
	for (int i = 0; i < RX_BUFFER_COUNT; i++) {
		rxqueue->buffers[i].Addr = rxbuff + (i * 2048);
		rxqueue->buffers[i].Length = RX_BUFFER_SIZE;
		rxqueue->buffers[i].Flags = 2;
		rxqueue->buffers[i].Next = 0;
		rxqueue->available.ring[i] = i;
	}
	rxqueue->available.index = RX_BUFFER_COUNT;
	dsb_ish();
	isb_flush();
	AuVirtioNetNotifyQueue(common, 0);
	UARTDebugOut("[aurora]: virtio rx queue initialized \r\n");
}

/**
 * @brief AuVirtioNetTxInitialize -- initialize tx queue
 * @param common -- pointer to virtio common config
 */
void AuVirtioNetTxinitialize(struct VirtioCommonCfg* common) {
	common->QueueSelect = 1;
	isb_flush();
	dsb_ish();
	common->QueueSize = TX_BUFFER_COUNT;
	isb_flush();
	dsb_ish();
	uint16_t qsize = common->QueueSize;
	UARTDebugOut("[aurora]: tx queue size : %d \r\n", qsize);
	uint64_t queuePhys = (uint64_t)
		AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	txqueue = (struct VirtioQueue*)P2V(queuePhys);
	memset(txqueue, 0, sizeof(*txqueue));
	common->QueueDesc = queuePhys;
	common->QueueAvail = (queuePhys) + OFFSETOF(struct VirtioQueue, available);
	common->QueueUsed = (queuePhys) + OFFSETOF(struct VirtioQueue, used);
	common->MSix = 0;
	common->QueueMSixVector = 0;
	common->QueueEnable = 1;
	isb_flush();
	dsb_ish();

	uint64_t txbuff = (uint64_t)AuPmmngrAllocPages(4, 1, 0, AURORA_PAGE_DMA);
	if (!txbuff) {
		UARTDebugOut("[aurora]: unable to allocate virtio TX buffer\r\n");
		return;
	}
	for (int i = 0; i < TX_BUFFER_COUNT; i++) {
		txqueue->buffers[i].Addr = txbuff + (i * 2048);
		txqueue->buffers[i].Length = TX_BUFFER_SIZE;
		txqueue->buffers[i].Flags = 0;
		txqueue->buffers[i].Next = 0;
		txqueue->available.ring[i] = i;
	}
	txqueue->available.index = 0;
	isb_flush();
	dsb_ish();
	UARTDebugOut("[aurora]: virtio tx queue initialized \r\n");
}

/**
 * @brief AuVirtioTransmit -- send one Ethernet frame on virtio-net
 * @param packet -- frame bytes
 * @param len -- length in bytes
 */
static void AuVirtioTransmit(void* packet, uint16_t len) {
	uint16_t idx;
	uint8_t* buff;
	virtio_net_hdr_t* hdr;
	uint16_t total;
	if (!txqueue || !_cfg)
		return;
	idx = tx_index % TX_BUFFER_COUNT;
	buff = (uint8_t*)P2V(txqueue->buffers[idx].Addr);
	memset(buff, 0, TX_BUFFER_SIZE);
	hdr = (virtio_net_hdr_t*)buff;
	hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
	if (len + sizeof(virtio_net_hdr_t) > TX_BUFFER_SIZE)
		len = TX_BUFFER_SIZE - sizeof(virtio_net_hdr_t);
	memcpy(buff + sizeof(virtio_net_hdr_t), packet, len);
	total = (uint16_t)(len + sizeof(virtio_net_hdr_t));
	txqueue->buffers[idx].Length = total;
	txqueue->buffers[idx].Flags = 0;
	txqueue->buffers[idx].Next = 0;
	txqueue->available.ring[txqueue->available.index % TX_BUFFER_COUNT] = idx;
	txqueue->available.index++;
	dsb_ish();
	isb_flush();
	AuVirtioNetNotifyQueue(_cfg, 1);
	tx_index++;
	AuVirtioNetRxPoll();
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
	AuVirtioTransmit(buffer, (uint16_t)len);
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
		memcpy(arg, ndev->mac, 6);
		return 0;
	case AUNET_GET_IPV4_ADDRESS:
		memcpy(arg, &ndev->ipv4addr, sizeof(ndev->ipv4addr));
		return 0;
	case AUNET_SET_IPV4_ADDRESS:
		memcpy(&ndev->ipv4addr, arg, sizeof(ndev->ipv4addr));
		AuNetAddConnectedRoute4(nic, "virtio-net");
		return 0;
	case AUNET_GET_GATEWAY_ADDRESS:
		memcpy(arg, &ndev->ipv4gateway, sizeof(ndev->ipv4gateway));
		return 0;
	case AUNET_SET_GATEWAY_ADDRESS:
		memcpy(&ndev->ipv4gateway, arg, sizeof(ndev->ipv4gateway));
		AuNetAddDefaultRoute4(nic, "virtio-net");
		return 0;
	case AUNET_GET_SUBNET_MASK:
		memcpy(arg, &ndev->ipv4subnet, sizeof(ndev->ipv4subnet));
		return 0;
	case AUNET_SET_SUBNET_MASK:
		memcpy(&ndev->ipv4subnet, arg, sizeof(ndev->ipv4subnet));
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
		AuNetAddConnectedRoute6(nic, "virtio-net");
		return 0;
	case AUNET_GET_IPV6_GATEWAY:
		memcpy(arg, &ndev->ipv6gateway, sizeof(ndev->ipv6gateway));
		return 0;
	case AUNET_SET_IPV6_GATEWAY:
		memcpy(&ndev->ipv6gateway, arg, sizeof(ndev->ipv6gateway));
		AuNetAddDefaultRoute6(nic, "virtio-net");
		return 0;
	case AUNET_GET_IPV6_PREFIX:
		memcpy(arg, &ndev->ipv6prefixLen, sizeof(ndev->ipv6prefixLen));
		return 0;
	case AUNET_SET_IPV6_PREFIX:
		memcpy(&ndev->ipv6prefixLen, arg, sizeof(ndev->ipv6prefixLen));
		AuNetAddConnectedRoute6(nic, "virtio-net");
		return 0;
	default:
		return 1;
	}
}

/**
 * @brief AuVirtioNetInitialize -- initialize the virtio network device
 * @param device -- device address passed by PCIe
 */
void AuVirtioNetInitialize(uint64_t device) {
	UARTDebugOut("[aurora]: virtio network device found \r\n");
	UARTDebugOut("ok\r\n");
	int bus = 0;
	int func = 0;
	int dev = 0;
	if (device == 0xFFFFFFFF)
		return;
	rx_index = 0;
	tx_index = 0;

	uint16_t command = AuPCIERead(device, PCI_COMMAND, bus, dev, func);
	command |= 4;
	command |= 2;
	command |= 1;
	AuPCIEWrite(device, PCI_COMMAND, command, bus, dev, func);
	isb_flush();
	dsb_ish();

	uint64_t barLo = AuPCIERead(device, PCI_BAR4, bus, dev, func);
	uint64_t barHi = AuPCIERead(device, PCI_BAR5, bus, dev, func);
	uint64_t bar = ((uint64_t)barHi << 32) | (barLo & ~0xFULL);
	uint64_t finalAddr = (uint64_t)AuMapMMIO(bar, 16);

	uint8_t cap_ptr = AuPCIERead(device, PCI_CAPABILITIES_PTR, bus, dev, func);
	uint32_t devcfg_offset = 0;
	while (cap_ptr != 0) {
		volatile virtio_pci_cap* cap = (volatile virtio_pci_cap*)(device + cap_ptr);
		if (cap->cap_vndr == VIRTIO_PCI_CAP_ID) {
			if (cap->cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
				UARTDebugOut("[virtio-net]: device configuration offset : %x \n", cap->offset);
				devcfg_offset = cap->offset;
				//break;
			}
			if (cap->cfg_type == 2) { //NOTIFY_CFG
				notifyBase = (volatile uint8_t*)(finalAddr + cap->offset);
				struct virtio_notifier_cap* notify = (struct virtio_notifier_cap*)cap;
				notifyOffMultiplier = notify->notifer_mult_base;
				UARTDebugOut(
					"[virtio-net]: notify base : %x , off : %x\n", notifyBase, cap->offset);
				UARTDebugOut("notify_mult : %x, cap : %x \n", notifyOffMultiplier, cap);
			}
		}
		cap_ptr = cap->cap_next;
	}

	struct VirtioCommonCfg* common = (struct VirtioCommonCfg*)finalAddr;
	_cfg = common;
	AuVirtioNetReset(common);

	struct VirtioNetCfg* netcfg = (struct VirtioNetCfg*)(finalAddr + devcfg_offset);

	/* acknowledge */
	common->DeviceStatus |= 0x01;
	isb_flush();
	dsb_ish();

	/* driver status */
	common->DeviceStatus |= 0x02;
	isb_flush();
	dsb_ish();

	/** feature negotiation **/
	AuVirtioNetFeatureNegotiate(common);

	common->DeviceStatus |= 0x08;
	isb_flush();
	dsb_ish();

	int spiID = AuGICAllocateSPI();
	UARTDebugOut("[aurora]: virtio-net-dev spi id: %d \r\n", spiID);
	if (AuPCIEAllocMSI(device, spiID, bus, dev, func)) {
		UARTDebugOut("[aurora]: virtio-net-dev msi allocated \r\n");
	}
	GICEnableSPIIRQ(spiID);
	//GICSetTargetCPU(spiID);
	isb_flush();
	dsb_ish();

	GICRegisterSPIHandler(&AuVirtioNetHandler, spiID);

	/** enable RX+TX queue */
	AuVirtioNetRxinitialize(common);
	AuVirtioNetTxinitialize(common);
	common->DeviceStatus |= 0x08;
	common->DeviceStatus |= 0x04;
	isb_flush();
	dsb_ish();
	AuVirtioNetNotifyQueue(common, 0);
	AuVirtioNetNotifyQueue(common, 1);

	UARTDebugOut("[aurora]: virtio-net-dev initialized successfully \r\n");
	AuTextOut("[aurora]: virtio-net-dev mac : ");
	ndev = (AuNetworkDevice*)kmalloc(sizeof(AuNetworkDevice));
	memset(ndev, 0, sizeof(AuNetworkDevice));
	ndev->type = NETDEV_TYPE_ETHERNET;
	ndev->linkStatus = 1;
	ndev->ipv4addr = MAKE_IP(10, 0, 2, 15);
	ndev->ipv4gateway = MAKE_IP(10, 0, 2, 2);
	ndev->ipv4subnet = MAKE_IP(255, 255, 255, 0);
	ndev->dns_ipv4_1 = MAKE_IP(10, 0, 2, 3);
	/* QEMU slirp IPv6: guest fec0::64, router fec0::2. */
	ndev->ipv6addr.s6_addr[0] = 0xfe;
	ndev->ipv6addr.s6_addr[1] = 0xc0;
	ndev->ipv6addr.s6_addr[15] = 0x64;
	ndev->ipv6gateway.s6_addr[0] = 0xfe;
	ndev->ipv6gateway.s6_addr[1] = 0xc0;
	ndev->ipv6gateway.s6_addr[15] = 0x02;
	ndev->ipv6prefixLen = 64;
	for (int i = 0; i < 6; i++) {
		AuTextOut("%x::", netcfg->mac[i]);
		ndev->mac[i] = netcfg->mac[i];
	}
	AuTextOut("\r\n");

	nic = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(nic, 0, sizeof(AuVFSNode));
	strcpy(nic->filename, "e1000");
	nic->flags = FS_FLAG_DEVICE;
	nic->write = AuVirtioWrite;
	nic->iocontrol = AuVirtioNetIOCtl;
	nic->device = ndev;
	AuAddNetAdapter(nic, "e1000");
	{
		AuVFSNode* alias = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
		memset(alias, 0, sizeof(AuVFSNode));
		strcpy(alias->filename, "virtio-net");
		alias->flags = FS_FLAG_DEVICE;
		alias->write = AuVirtioWrite;
		alias->iocontrol = AuVirtioNetIOCtl;
		alias->device = ndev;
		AuAddNetAdapter(alias, "virtio-net");
		AuNetAddConnectedRoute4(alias, "virtio-net");
		AuNetAddDefaultRoute4(alias, "virtio-net");
		AuNetAddConnectedRoute6(alias, "virtio-net");
		AuNetAddDefaultRoute6(alias, "virtio-net");
		AuNetRegisterRxPoll(AuVirtioNetRxPoll);
	}
}
