/**
* @file virtioblk.c
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

/*
 * Minimal synchronous (polling, no IRQ yet) virtio-blk driver built on the
 * shared modern virtio-pci plumbing in Drivers/virtiopci.c. First real
 * consumer of that helper -- kept deliberately simple: one queue, one
 * request in flight at a time, sector-granular read/write. Good enough to
 * give the kernel runtime block I/O beyond the RAM-resident initrd; queueing
 * multiple in-flight requests and wiring an IRQ instead of polling are
 * natural follow-ups once something upstream actually needs the throughput
 * --axiss
 */

#include <dtb.h>
#include <aucon.h>
#include <kernelAA64.h>
#include <_null.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <pcie.h>
#include <Drivers/virtio.h>
#include <Drivers/uart.h>



#define VIRTIO_BLK_T_IN	 0
#define VIRTIO_BLK_T_OUT 1

#define VIRTIO_BLK_S_OK	 0

#define VIRTIO_BLK_SECTOR_SIZE 512

/* status byte lives right after the 16-byte request header, well clear of
 * it, in the same page --axiss */
#define VIRTIO_BLK_STATUS_OFFSET 64

struct VirtioBlkReqHeader {
	uint32_t type;
	uint32_t reserved;
	uint64_t sector;
};

/* only the field we actually use; the rest of virtio_blk_config depends on
 * features we don't negotiate yet --axiss */
struct VirtioBlkConfig {
	volatile uint64_t capacity;
};

static struct VirtioPCIDevice blkDev;
static struct VirtqDesc* blkDesc;
static struct VirtqAvailHdr* blkAvail;
static struct VirtqUsedHdr* blkUsed;
static uint16_t blkQueueSize;
static uint16_t blkLastUsed;

static uint64_t blkReqPhys;
static void* blkReqVirt;
static uint64_t blkDataPhys;
static void* blkDataVirt;

static bool blkReady;

/**
 * @brief AuVirtioBlkRequest -- submit one synchronous request and poll for
 * completion
 * @param type -- VIRTIO_BLK_T_IN (read) or VIRTIO_BLK_T_OUT (write)
 * @param sector -- 512-byte sector index
 * @param dataVirt -- mapped virtual pointer to the data buffer
 * @param dataPhys -- physical address of the same buffer
 * @param len -- length in bytes, must be a multiple of 512
 * @return true if the device reported VIRTIO_BLK_S_OK
 */
static bool AuVirtioBlkRequest(uint32_t type, uint64_t sector, void* dataVirt, uint64_t dataPhys,
								uint32_t len) {
	if (!blkReady)
		return false;

	struct VirtioBlkReqHeader* hdr = (struct VirtioBlkReqHeader*)blkReqVirt;
	hdr->type = type;
	hdr->reserved = 0;
	hdr->sector = sector;

	volatile uint8_t* status = (volatile uint8_t*)((uint8_t*)blkReqVirt + VIRTIO_BLK_STATUS_OFFSET);
	*status = 0xFF; /* poison so a device that never touches it is obvious */

	aa64_dc_cvac_range(blkReqVirt, VIRTIO_BLK_STATUS_OFFSET + 1);
	if (type == VIRTIO_BLK_T_OUT)
		aa64_dc_cvac_range(dataVirt, len);
	isb_flush();
	dsb_sy_barrier();

	uint16_t dHdr = 0, dData = 1, dStatus = 2; /* fixed chain, one request at a time */

	blkDesc[dHdr].addr = blkReqPhys;
	blkDesc[dHdr].len = sizeof(struct VirtioBlkReqHeader);
	blkDesc[dHdr].flags = VIRTQ_DESC_F_NEXT;
	blkDesc[dHdr].next = dData;

	blkDesc[dData].addr = dataPhys;
	blkDesc[dData].len = len;
	blkDesc[dData].flags = VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0);
	blkDesc[dData].next = dStatus;

	blkDesc[dStatus].addr = blkReqPhys + VIRTIO_BLK_STATUS_OFFSET;
	blkDesc[dStatus].len = 1;
	blkDesc[dStatus].flags = VIRTQ_DESC_F_WRITE;
	blkDesc[dStatus].next = 0;

	uint16_t availSlot = blkAvail->idx % blkQueueSize;
	blkAvail->ring[availSlot] = dHdr;
	isb_flush();
	dsb_sy_barrier();
	blkAvail->idx = blkAvail->idx + 1;
	isb_flush();
	dsb_sy_barrier();

	AuVirtioPCINotifyQueue(&blkDev, 0);

	uint64_t usedRingBytes = sizeof(struct VirtqUsedHdr) +
							  (uint64_t)blkQueueSize * sizeof(struct VirtqUsedElem);
	uint32_t spins = 0;
	while (1) {
		aa64_dc_ivac_range((void*)blkUsed, usedRingBytes);
		dsb_sy_barrier();
		if (blkUsed->idx != blkLastUsed)
			break;
		if (++spins > 50000000) {
			UARTDebugOut("virtio-blk: request timed out (sector %d) \r\n", (uint32_t)sector);
			/* Do not allow later callers to reuse a queue whose completion state
			 * is unknown. The device is reset by the next initialization attempt. */
			blkReady = false;
			blkDev.common->DeviceStatus |= VIRTIO_STATUS_FAILED;
			isb_flush();
			dsb_ish();
			return false;
		}
	}
	blkLastUsed++;

	if (type == VIRTIO_BLK_T_IN)
		aa64_dc_ivac_range(dataVirt, len);
	aa64_dc_ivac_range((void*)status, 1);
	isb_flush();
	dsb_sy_barrier();

	if (*status != VIRTIO_BLK_S_OK) {
		UARTDebugOut("virtio-blk: request failed, status=%d \r\n", *status);
		return false;
	}
	return true;
}

/**
 * @brief AuVirtioBlkReadSector -- read one 512-byte sector into buf
 */
bool AuVirtioBlkReadSector(uint64_t sector, void* bufVirt, uint64_t bufPhys) {
	return AuVirtioBlkRequest(VIRTIO_BLK_T_IN, sector, bufVirt, bufPhys, VIRTIO_BLK_SECTOR_SIZE);
}

/**
 * @brief AuVirtioBlkWriteSector -- write one 512-byte sector from buf
 */
bool AuVirtioBlkWriteSector(uint64_t sector, void* bufVirt, uint64_t bufPhys) {
	return AuVirtioBlkRequest(VIRTIO_BLK_T_OUT, sector, bufVirt, bufPhys, VIRTIO_BLK_SECTOR_SIZE);
}

/**
 * @brief AuVirtioBlkInitialize -- initialize the virtio block device
 * @param device -- pcie config space address
 * @param bus, dev, func -- pcie location of the device
 */
void AuVirtioBlkInitialize(uint64_t device, int bus, int dev, int func) {
	if (device == 0 || device == 0xFFFFFFFF)
		return;

	UARTDebugOut("[aurora]: Virtio Block device found \r\n");

	if (!AuVirtioPCIInit(device, bus, dev, func, 0, &blkDev)) {
		UARTDebugOut("virtio-blk: device init/feature negotiation failed \r\n");
		return;
	}

	blkQueueSize = AuVirtioPCISetupQueue(&blkDev, 0, &blkDesc, &blkAvail, &blkUsed,
										  VIRTIO_MSI_NO_VECTOR);
	if (blkQueueSize == 0) {
		UARTDebugOut("virtio-blk: request queue setup failed \r\n");
		blkDev.common->DeviceStatus |= VIRTIO_STATUS_FAILED;
		return;
	}
	blkLastUsed = 0;

	blkReqPhys = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	blkReqVirt = AuMapMMIO(blkReqPhys, 1);
	memset(blkReqVirt, 0, PAGE_SIZE);

	blkDataPhys = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	blkDataVirt = AuMapMMIO(blkDataPhys, 1);
	memset(blkDataVirt, 0, PAGE_SIZE);

	blkDev.common->DeviceStatus |= VIRTIO_STATUS_DRIVER_OK;
	isb_flush();
	dsb_ish();

	blkReady = true;

	if (blkDev.deviceCfg) {
		struct VirtioBlkConfig* cfg = (struct VirtioBlkConfig*)blkDev.deviceCfg;
		uint64_t capacity = cfg->capacity;
		UARTDebugOut("virtio-blk: capacity = %d sectors (~%d MiB), queue size = %d \r\n",
					 (uint32_t)capacity, (uint32_t)((capacity * VIRTIO_BLK_SECTOR_SIZE) / (1024 * 1024)),
					 blkQueueSize);
	}

	/* self-test: read sector 0 and print its signature bytes so a boot log
	 * actually proves the capability walk + negotiation + virtqueue path
	 * works, not just that it compiled --axiss */
	if (AuVirtioBlkReadSector(0, blkDataVirt, blkDataPhys)) {
		uint8_t* b = (uint8_t*)blkDataVirt;
		UARTDebugOut("virtio-blk: sector 0 read OK, first byte=%x, boot sig=%x %x \r\n", b[0],
					 b[510], b[511]);
	} else {
		UARTDebugOut("virtio-blk: sector 0 self-test read FAILED \r\n");
	}
}

