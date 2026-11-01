/**
* @file virtio.h
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

#ifndef __VIRTIO_H__
#define __VIRTIO_H__

#include <stdint.h>
#include <stddef.h>
#include <aurora.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif

struct VirtioCommonCfg {
	volatile uint32_t DevFeatureSelect;
	volatile uint32_t DevFeature;
	volatile uint32_t GuestFeatureSelect;
	volatile uint32_t GuestFeature;
	volatile uint16_t MSix;
	volatile uint16_t Queues;
	volatile uint8_t  DeviceStatus;
	volatile uint8_t  ConfigGeneration;

	volatile uint16_t QueueSelect;
	volatile uint16_t QueueSize;
	volatile uint16_t QueueMSixVector;
	volatile uint16_t QueueEnable;
	volatile uint16_t QueueNotifyOff;

	volatile uint64_t QueueDesc;
	volatile uint64_t QueueAvail;
	volatile uint64_t QueueUsed;
};

struct VirtioDeviceConfig {
	volatile uint8_t select;
	volatile uint8_t subsel;
	volatile uint8_t size;
	volatile uint8_t pad[5];
	union {
		struct {
			volatile uint32_t min;
			volatile uint32_t max;
			volatile uint32_t fuzz;
			volatile uint32_t flat;
			volatile uint32_t res;
		}tablet_data;
		uint8_t str[128];
	}data;
};

struct VirtioBuffer {
	uint64_t Addr;
	uint32_t Length;
	uint16_t Flags;
	uint16_t Next;
};

struct VirtioAvail {
	uint16_t flags;
	volatile uint16_t index;
	uint16_t ring[64];
	uint16_t int_index;
};

struct VirtioRing {
	uint32_t index;
	uint32_t length;
};

struct VirtioUsed {
	uint16_t flags;
	volatile uint16_t index;
	struct VirtioRing ring[64];
	uint16_t int_index;
};

struct VirtioQueue {
	struct VirtioBuffer buffers[64];
	struct VirtioAvail available;
	struct VirtioUsed used;
};

struct VirtioInputEvent {
	uint16_t type;
	uint16_t code;
	uint32_t value;
};


#define OFFSETOF(s,m) ((size_t)&(((s*)0)->m))

#define VIRTIO_NET_F_CSUM (1ULL << 0)
#define VIRTIO_NET_F_GUEST_CSUM (1ULL << 2)
#define VIRTIO_NET_F_MAC (1ULL << 5)
#define VIRTIO_NET_F_CTRL_VQ (1ULL << 17)
#define VIRTIO_NET_HDR_GSO_NONE 0

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
typedef struct _virtio_net_hdr_ {
	uint8_t flags;
	uint8_t gso_type;
	uint16_t hdr_len;
	uint16_t gso_size;
	uint16_t csum_start;
	uint16_t csum_offset;
	uint16_t num_buffers; /* QEMU virtio-net consumes 12-byte hdr on TX */
#ifdef _MSC_VER
} virtio_net_hdr_t;
#pragma pack(pop)
#else
} __attribute__((packed)) virtio_net_hdr_t;
#endif


typedef struct {
	uint8_t cap_vndr;
	uint8_t cap_next;
	uint8_t cap_len;
	uint8_t cfg_type;
	uint8_t bar;
	uint8_t padding[3];
	uint32_t offset;
	uint32_t length;
}virtio_pci_cap;



struct virtio_notifier_cap {
	virtio_pci_cap cap;
	uint32_t notifer_mult_base;
};

/* modern virtio-pci capability types, virtio-v1.1 sec 4.1.4 */
#define VIRTIO_PCI_CAP_VENDOR_ID  0x09 /* PCI capability ID for "vendor specific" */
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG	  3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_PCI_CAP_PCI_CFG	  5

/* device status register bits, virtio-v1.1 sec 2.1 */
#define VIRTIO_STATUS_ACKNOWLEDGE		  0x01
#define VIRTIO_STATUS_DRIVER			  0x02
#define VIRTIO_STATUS_DRIVER_OK		  0x04
#define VIRTIO_STATUS_FEATURES_OK		  0x08
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED			  0x80

/* bit 32 of the feature bitmap (high half, feature-select 1) --
 * required on every modern (non-transitional-legacy) negotiation --axiss */
#define VIRTIO_F_VERSION_1_BIT 32

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2
#define VIRTIO_MSI_NO_VECTOR 0xFFFF

/* split-ring layout, sized to whatever queue size the device reports --
 * NOT the fixed 64-entry VirtioQueue above. Modern virtio-pci gives
 * QueueDesc/QueueAvail/QueueUsed as independent 64-bit addresses, so
 * these don't need to be contiguous or share a struct --axiss */
struct VirtqDesc {
	volatile uint64_t addr;
	volatile uint32_t len;
	volatile uint16_t flags;
	volatile uint16_t next;
};

/* Driver -> device: do not raise an interrupt for this queue. The driver
 * still sees completions in the used ring; it just polls them when it
 * needs the slots back. */
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

struct VirtqAvailHdr {
	volatile uint16_t flags;
	volatile uint16_t idx;
	volatile uint16_t ring[];
};

struct VirtqUsedElem {
	volatile uint32_t id;
	volatile uint32_t len;
};

struct VirtqUsedHdr {
	volatile uint16_t flags;
	volatile uint16_t idx;
	volatile struct VirtqUsedElem ring[];
};

/**
 * @brief a mapped, reset-and-negotiated modern virtio-pci device. Per-device
 * drivers build their own virtqueues and config parsing on top of this.
 */
struct VirtioPCIDevice {
	uint64_t address;
	int bus, dev, func;
	struct VirtioCommonCfg* common;
	volatile uint8_t* isr;
	void* deviceCfg;
	uint8_t* notifyBase;
	uint32_t notifyOffMultiplier;
};

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief AuVirtioPCIInit -- walks the PCI capability list of a modern
 * virtio-pci device, maps its common/notify/isr/device config BARs, resets
 * it and negotiates VIRTIO_F_VERSION_1 plus wantedFeaturesLow (bits 0-31).
 * @return false (device left reset) if the capability walk or the
 * ACKNOWLEDGE->DRIVER->FEATURES_OK handshake fails
 */
AU_EXTERN AU_EXPORT bool AuVirtioPCIInit(uint64_t address, int bus, int dev, int func,
							 uint32_t wantedFeaturesLow, struct VirtioPCIDevice* out);

/**
 * @brief AuVirtioPCISetupQueue -- selects queue qidx, allocates desc/avail/used
 * rings sized to the device-reported queue size and enables the queue.
 * @param msixVector -- MSI-X table index to bind the queue to, or
 * VIRTIO_MSI_NO_VECTOR for polling (no queue interrupt)
 * @return the negotiated queue size, or 0 on allocation/mapping failure or
 * when the device reports size 0/unavailable
 */
AU_EXTERN AU_EXPORT uint16_t AuVirtioPCISetupQueue(struct VirtioPCIDevice* dev,
									   uint16_t qidx,
									   struct VirtqDesc** outDesc,
									   struct VirtqAvailHdr** outAvail,
									   struct VirtqUsedHdr** outUsed,
									   uint16_t msixVector);

/**
 * @brief AuVirtioPCINotifyQueue -- kicks the device for queue qidx
 */
AU_EXTERN AU_EXPORT void AuVirtioPCINotifyQueue(struct VirtioPCIDevice* dev, uint16_t qidx);

AU_EXTERN AU_EXPORT void AuVirtioPCIPostAvail(struct VirtqDesc* desc,
	struct VirtqAvailHdr* avail, uint16_t qsize, uint64_t bufPhys, uint32_t bufSize,
	uint16_t nbuf, uint16_t flags);
#ifdef __cplusplus
}
#endif

/**
 * @brief AuVirtioBlkInitialize -- initialize the virtio block device
 */
extern void AuVirtioBlkInitialize(uint64_t device, int bus, int dev, int func);

/**
 * @brief AuVirtioKbdInitialize -- initialize the virtio keyboard
 */
extern void AuVirtioKbdInitialize(uint64_t device, int bus, int dev, int func);

/**
 * @brief AuVirtioTabletInitialize -- initialize virtio tablet
 */
extern void AuVirtioTabletInitialize(uint64_t device, int bus, int dev, int func);

extern void AuVirtioKbdDown();

extern void AuVirtioTabletDown();

/**
 * @brief 
 */
 /**
  * @brief AuVirtioNetInitialize -- initialize the virtio network 
  * device
  * @param device -- virtio network device address
  */
extern void AuVirtioNetInitialize(uint64_t device);
#endif
