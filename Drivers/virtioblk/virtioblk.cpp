/**
* @file virtioblk.cpp
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

#include <aurora.h>
#include <aucon.h>
#include "virtioblk.h"
#include <audrv.h>
#include <pcie.h>
#include <Drivers/virtio.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <Hal/AA64/gic.h>
#include <Mm/pmmngr.h>
#include <Mm/dma.h>
#include <Fs/vdisk.h>
#include <string.h>
#include <stdio.h>

#define VIRTIO_PCI_CAP_ID 0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_F_VERSION_1 (1ull << 32)
#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_STATUS_ACK 0x01
#define VIRTIO_STATUS_DRIVER 0x02
#define VIRTIO_STATUS_DRIVER_OK 0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED 0x80

#define VIRTIO_DEFAULT_SECT_COUNT 1


struct VirtioBLKBuffer {
	uint64_t Addr;
	uint32_t Length;
	uint16_t Flags;
	uint16_t Next;
};

struct VirtioBLKAvail {
	uint16_t flags;
	volatile uint16_t index;
	uint16_t ring[256];
	uint16_t int_index;
};

struct VirtioBLKRing {
	uint32_t index;
	uint32_t length;
};

struct VirtioBLKUsed {
	uint16_t flags;
	volatile uint16_t index;
	struct VirtioBLKRing ring[256];
	uint16_t int_index;
};

struct VirtioBLKQueue {
	struct VirtioBLKBuffer buffers[256];
	struct VirtioBLKAvail available;
	struct VirtioBLKUsed used;
};


/** static internal usable variables (per-device instance: the driver binds
 * every virtio-blk PCI function, i.e. both the ESP drive and the persistent
 * data drive, so singletons would make the second device clobber the first.
 * disk->data points at the owning instance --axiss */
struct VirtblkInst {
	bool in_use;
	volatile uint8_t* notifyBase;
	uint32_t notifyOffMultiplier;
	VirtioCommonCfg* cfg;
	VirtioBLKQueue* requestQ;
	uint64_t requestQ_sz;
	void* reqVirt;
	uint64_t reqPhys;
	uint64_t lastReq;
};

#define VIRTBLK_MAX_INST 4
static VirtblkInst s_inst[VIRTBLK_MAX_INST];
static AuDMAGlobalClass _blkclass;
static bool _dmaReady;


/**
 * @brief virtioblk_reset -- reset the device
 * @param cfg -- pointer to virtio common config
 */
void virtioblk_reset(VirtioCommonCfg* cfg) {
	/* Reset the device */
	cfg->DeviceStatus = 0;

	isb_flush();
	dsb_ish();

	cfg->DeviceStatus = VIRTIO_STATUS_ACKNOWLEDGE;
	isb_flush();
	dsb_ish();

	cfg->DeviceStatus |= VIRTIO_STATUS_DRIVER;
	isb_flush();
	dsb_ish();

	UARTDebugOut("[virtio-blk]: reset completed successfully \r\n");
}


/**
 * @brief virtioblk_feature_negotiate -- check available
 * features of host device and enable only those
 * supported by the driver
 * @param cfg -- pointer to virtio common config
 */
void virtioblk_feature_negotiate(VirtioCommonCfg* cfg) {
	cfg->DevFeatureSelect = 0;
	isb_flush();
	dsb_ish();
	uint32_t features_lo = cfg->DevFeature;
	cfg->DevFeatureSelect = 1;
	isb_flush();
	dsb_ish();
	uint32_t feature_hi = cfg->DevFeature;
	uint64_t features = ((uint64_t)feature_hi << 32) | features_lo;

	if (!(features & VIRTIO_F_VERSION_1))
		AuTextOut("[virtio-blk]: warning: device is not modern VirtIO! \r\n");

	uint64_t guestfeatures = 0;
	guestfeatures |= VIRTIO_F_VERSION_1;
	guestfeatures &= features;

	cfg->GuestFeatureSelect = 0;
	cfg->GuestFeature = guestfeatures & UINT32_MAX;
	isb_flush();
	dsb_ish();

	cfg->GuestFeatureSelect = 1;
	cfg->GuestFeature = (guestfeatures >> 32) & UINT32_MAX;
	isb_flush();
	dsb_ish();
	UARTDebugOut("[virtio-blk]: features negotiation done \r\n");
}


/**
 * @brief virtioblk_notify_queue -- notify host that new command
 * is present
 * @param queueIdx -- queue number, zero for controlq and
 * one for cursorq
 */
void virtioblk_notify_queu(VirtblkInst* inst, uint16_t queueIdx) {
	VirtioCommonCfg* cfg = inst->cfg;
	cfg->QueueSelect = queueIdx;
	isb_flush();
	dsb_ish();
	uint16_t notify_off = cfg->QueueNotifyOff;
	volatile uint16_t* notifyAddr =
		(volatile uint16_t*)((uint64_t)inst->notifyBase + notify_off * inst->notifyOffMultiplier);
	*notifyAddr = queueIdx;
	isb_flush();
	dsb_ish();
}


void virtioblk_interrupt_handler(int spiID) {
	UARTDebugOut("[virtioblk] interrupt++ \r\n");
}


/**
 * @brief virtioblk_alloc_requestQ -- allocate requestQ
 * of virtio
 */
void virtioblk_alloc_requestQ(VirtblkInst* inst) {
	VirtioCommonCfg* cfg = inst->cfg;
	cfg->QueueSelect = 0;
	isb_flush();
	dsb_ish();

	int queueSz = cfg->QueueSize;
	inst->requestQ_sz = queueSz;
	UARTDebugOut("[virtio-blk]: requestQ_sz : %d  - sizeof(VirtioQueue) -> %d \r\n", inst->requestQ_sz, sizeof(VirtioQueue));
	uint64_t queuePhys = AuPmmngrAllocPages(2, 1, 0, AURORA_PAGE_DMA);
	memset((void*)queuePhys, 0, 0x1000*2);
	inst->requestQ = (struct VirtioBLKQueue*)AuMapMMIO(queuePhys, 2);

	cfg->QueueDesc = queuePhys;
	cfg->QueueAvail = (queuePhys)+OFFSETOF(struct VirtioBLKQueue, available);
	cfg->QueueUsed = (queuePhys)+OFFSETOF(struct VirtioBLKQueue, used);
	cfg->MSix = 0;
	cfg->QueueMSixVector = 0;
	cfg->QueueEnable = 1;
	isb_flush();
	dsb_ish();

	cfg->DeviceStatus = 4;
	isb_flush();
	dsb_ish();

}

/**
 * @brief virtioblk_read -- read data from disc
 */
bool virtioblk_read(VirtblkInst* inst, size_t lba, uint64_t buffer, size_t numSector) {
	VirtioBLKQueue* requestQ = inst->requestQ;
	uint64_t requestQ_sz = inst->requestQ_sz;
	memset(inst->reqVirt, 0, 512);
    virtio_blk_req_header_t* hdr = (virtio_blk_req_header_t*)inst->reqVirt;
	uint8_t* status = (uint8_t*)((uint64_t)inst->reqVirt + 16);

	uint16_t idx = requestQ->available.index % requestQ_sz;


	hdr->type = VIRTIO_BLK_OP_IN;
	hdr->reserved = 0;
	hdr->sector = lba;
	*status = 0xFF;

	
	requestQ->buffers[(idx + 0) % requestQ_sz].Addr = inst->reqPhys;
	requestQ->buffers[(idx + 0) % requestQ_sz].Length = sizeof(virtio_blk_req_header_t);
	requestQ->buffers[(idx + 0) % requestQ_sz].Flags = VIRTQ_DESC_F_NEXT;
	requestQ->buffers[(idx + 0) % requestQ_sz].Next =  (idx + 1) % requestQ_sz;
	
	requestQ->buffers[(idx + 1) % requestQ_sz].Addr = buffer;
	requestQ->buffers[(idx + 1) % requestQ_sz].Length = numSector * 512;
	requestQ->buffers[(idx + 1) % requestQ_sz].Flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
	requestQ->buffers[(idx + 1) % requestQ_sz].Next = (idx + 2) % requestQ_sz;


	requestQ->buffers[(idx + 2) % requestQ_sz].Addr = (inst->reqPhys + 16);
	requestQ->buffers[(idx + 2) % requestQ_sz].Length = 1;
	requestQ->buffers[(idx + 2) % requestQ_sz].Flags = VIRTQ_DESC_F_WRITE;
	requestQ->buffers[(idx + 2) % requestQ_sz].Next = 0;

	uint16_t ringSlot = requestQ->available.index % requestQ_sz;
	requestQ->available.ring[ringSlot] = idx;
	isb_flush();
	dsb_ish();

	requestQ->available.index++;
	//txq->available.index %= txq_sz;
	isb_flush();
	dsb_ish();
	

	virtioblk_notify_queu(inst, 0);

	while (requestQ->used.index == inst->lastReq)
		dsb_ish();

	inst->lastReq = requestQ->used.index;

	return (*status == VIRTIO_BLK_S_OK);
}


/**
 * @brief virtioblk_write -- write data to disc
 */
bool virtioblk_write(VirtblkInst* inst, size_t lba, uint64_t buffer, size_t numSector) {
	VirtioBLKQueue* requestQ = inst->requestQ;
	uint64_t requestQ_sz = inst->requestQ_sz;
	memset(inst->reqVirt, 0, 512);
	virtio_blk_req_header_t* hdr = (virtio_blk_req_header_t*)inst->reqVirt;
	uint8_t* status = (uint8_t*)((uint64_t)inst->reqVirt + 16);

	uint16_t idx = requestQ->available.index % requestQ_sz;


	hdr->type = VIRTIO_BLK_OP_OUT;
	hdr->reserved = 0;
	hdr->sector = lba;
	*status = 0xFF;

	requestQ->buffers[(idx + 0) % requestQ_sz].Addr = inst->reqPhys;
	requestQ->buffers[(idx + 0) % requestQ_sz].Length = sizeof(virtio_blk_req_header_t);
	requestQ->buffers[(idx + 0) % requestQ_sz].Flags = VIRTQ_DESC_F_NEXT;
	requestQ->buffers[(idx + 0) % requestQ_sz].Next = (idx + 1) % requestQ_sz;


	requestQ->buffers[(idx + 1) % requestQ_sz].Addr = buffer;
	requestQ->buffers[(idx + 1) % requestQ_sz].Length = numSector * 512;
	requestQ->buffers[(idx + 1) % requestQ_sz].Flags = VIRTQ_DESC_F_NEXT;
	requestQ->buffers[(idx + 1) % requestQ_sz].Next = (idx + 2) % requestQ_sz;


	requestQ->buffers[(idx + 2) % requestQ_sz].Addr = (inst->reqPhys + 16);
	requestQ->buffers[(idx + 2) % requestQ_sz].Length = 1;
	requestQ->buffers[(idx + 2) % requestQ_sz].Flags = VIRTQ_DESC_F_WRITE;
	requestQ->buffers[(idx + 2) % requestQ_sz].Next = 0;

	uint16_t ringSlot = requestQ->available.index % requestQ_sz;
	requestQ->available.ring[ringSlot] = idx;
	isb_flush();
	dsb_ish();

	requestQ->available.index++;
	isb_flush();
	dsb_ish();


	virtioblk_notify_queu(inst, 0);

	while (requestQ->used.index == inst->lastReq)
		dsb_ish();

	inst->lastReq = requestQ->used.index;

	return (*status == VIRTIO_BLK_S_OK);
}

int virtblk_read_vdisk(AuVDisk* disk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	if (count == 0)
		return 0;
	if (!buffer)
		return 0;
	VirtblkInst* inst = (VirtblkInst*)disk->data;
	if (!inst || !inst->in_use)
		return 0;
	virtioblk_read(inst, lba, V2P((uint64_t)buffer), count);
	return 512 * count;
}


int virtblk_write_vdisk(AuVDisk* disk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	if (count == 0)
		return 0;
	if (!buffer)
		return 0;
	VirtblkInst* inst = (VirtblkInst*)disk->data;
	if (!inst || !inst->in_use)
		return 0;
	virtioblk_write(inst, lba, V2P((uint64_t)buffer), count);
	return 512 * count;
}


/*
* AuDriverUnload -- deattach the driver from
* aurora system
*/
AU_EXTERN AU_EXPORT int AuDriverUnload() {
	for (int i = 0; i < VIRTBLK_MAX_INST; i++) {
		if (s_inst[i].in_use && s_inst[i].cfg)
			virtioblk_reset(s_inst[i].cfg);
	}
	return 0;
}


/*
* AuDriverMain -- Main entry for virtio-blk device
*/
AU_EXTERN AU_EXPORT int AuDriverMain(AuDriver * drv) {
	int bus = drv->bus;
	int dev = drv->dev;
	int func = drv->func;
	uint64_t device = drv->device;

	/* one instance per PCI function: the ESP drive and the persistent
	 * data drive each get here once --axiss */
	VirtblkInst* inst = NULL;
	int instIdx = -1;
	for (int i = 0; i < VIRTBLK_MAX_INST; i++) {
		if (!s_inst[i].in_use) {
			inst = &s_inst[i];
			instIdx = i;
			break;
		}
	}
	if (!inst) {
		AuTextOut("[virtio-blk]: no free instance, device ignored \r\n");
		return 0;
	}
	memset(inst, 0, sizeof(VirtblkInst));
	inst->in_use = true;

	AuTextOut("[virtio-blk]: initializing instance %d \r\n", instIdx);
	uint64_t barLo = AuPCIERead(device, PCI_BAR4, bus, dev, func);
	uint64_t barHi = AuPCIERead(device, PCI_BAR5, bus, dev, func);
	uint64_t bar = ((uint64_t)barHi << 32) | (barLo & ~0xFULL);

	uint64_t finalAddr = (uint64_t)AuMapMMIO(bar, 1);
	VirtioCommonCfg* cfg = (VirtioCommonCfg*)finalAddr;
	inst->cfg = cfg;

	uint16_t command = AuPCIERead(device, PCI_COMMAND, bus, dev, func);
	command |= 4;
	command |= 2;
	command |= 1;
	AuPCIEWrite(device, PCI_COMMAND, command, bus, dev, func);
	isb_flush();
	dsb_ish();

	uint64_t devcfg_offset = 0;
	uint8_t cap_ptr = AuPCIERead(device, PCI_CAPABILITIES_PTR, bus, dev, func);
	while (cap_ptr != 0) {
		volatile virtio_pci_cap* cap = (volatile virtio_pci_cap*)(device + cap_ptr);
		if (cap->cap_vndr == VIRTIO_PCI_CAP_ID) {
			if (cap->cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
				devcfg_offset = cap->offset;
				//break;
			}
			if (cap->cfg_type == 2) { //NOTIFY_CFG
				uint64_t nbase = (bar + cap->offset);
				inst->notifyBase = (volatile uint8_t*)AuMapMMIO(nbase, 1);
				virtio_notifier_cap* notify = (virtio_notifier_cap*)cap;
				inst->notifyOffMultiplier = notify->notifer_mult_base;
			}
		}
		cap_ptr = cap->cap_next;
	}
	if (devcfg_offset == 0)
		devcfg_offset = 0x2000;

	if (!_dmaReady) {
		AuDMAGlobalClassInitialize(&_blkclass, "virtblk");
		_dmaReady = true;
	}

	inst->reqVirt = AuDMAGClassAlloc(&_blkclass, 512, &inst->reqPhys);
	UARTDebugOut("[virtio-blk-%d]: number of queues : %d \r\n", instIdx, cfg->Queues);

	virtioblk_dev_config* blkconfig = (virtioblk_dev_config*)(bar + devcfg_offset);
	UARTDebugOut("[virtio-blk-%d]: max_size : %d \r\n", instIdx, blkconfig->size_max);
	UARTDebugOut("[virtio-blk-%d]: total sectors : %d \r\n", instIdx, blkconfig->capacity);
	UARTDebugOut("[virtio-blk-%d]: sector size : %d \r\n", instIdx, blkconfig->blk_sz);

	uint64_t total_sect = blkconfig->capacity;

	uint64_t storage_sz_in_bytes = total_sect * 512;
	uint64_t size_mb = storage_sz_in_bytes / (1024 * 1024);
	uint64_t size_gb = storage_sz_in_bytes / (1024 * 1024 * 1024);

	/** reset the virtio block device **/
	virtioblk_reset(cfg);

	/* acknowledge */
	cfg->DeviceStatus |= 0x01;
	isb_flush();
	dsb_ish();

	/* driver status */
	cfg->DeviceStatus |= 0x02;
	isb_flush();
	dsb_ish();

	/* feature negotiate */
	virtioblk_feature_negotiate(cfg);

	cfg->DeviceStatus |= VIRTIO_STATUS_ACK
		| VIRTIO_STATUS_DRIVER |
		VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
	isb_flush();
	dsb_ish();


	int spiID = AuGICAllocateSPI();
	UARTDebugOut("[virtio-blk]: spi id: %d \n", spiID);
	if (AuPCIEAllocMSI(device, spiID, bus, dev, func)) {
		UARTDebugOut("[virtio-blk]: msi/msi-x allocated \r\n");
	}
	GICEnableSPIIRQ(spiID);
	//GICSetTargetCPU(spiID);
	isb_flush();
	dsb_ish();

	GICRegisterSPIHandler(&virtioblk_interrupt_handler, spiID);

	virtioblk_alloc_requestQ(inst);

	/** whenever new storage device detected, three kernel objects
	 * are needed to create :
	 *  (1) --- VDisk info for read write access
	 *  (2) --- Device file at /dev for ioctl and raw read write 
	 *          without partition overhead, write it to anywhere
	 *  (3) --- File System node after registering partitions
	 */

	AuVDisk* disk = AuCreateVDisk();
	strcpy(disk->diskname, "virtio-blk");
	{
		int off = strlen(disk->diskname);
		sztoa(instIdx, disk->diskname + off, 10);
	}
	disk->data = inst;
	disk->Read = virtblk_read_vdisk; 
	disk->Write = virtblk_write_vdisk;
	disk->max_blocks = total_sect;
	disk->currentLBA = 0;

	AuVDiskCreateStorageFile(disk->diskPath);
	//** now open the disk file and add read write ioctl to it

	AuVDiskRegister(disk);

	/* Stable guest path: a FAT32 disk labelled XENEVADATA (see
	 * build_and_run_qemu.sh mkfs) is the persistent data disk and is
	 * renamed to /data. Anything else (e.g. the ESP boot drive, which
	 * also binds here) keeps its auto-assigned letter mount. --axiss */
	if (disk->fsys) {
		uint8_t* sect = (uint8_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		if (sect) {
			memset(sect, 0, 512);
			if (disk->Read(disk, 0, 1, (uint64_t*)sect) > 0 &&
				memcmp(sect + 0x47, "XENEVADATA ", 11) == 0) {
				strcpy(disk->fsys->filename, "data");
			}
			AuPmmngrReleasePage((uint64_t)V2P((uint64_t)sect));
		}
		AuTextOut("[virtio-blk-%d]: mounted at /%s \r\n", instIdx, disk->fsys->filename);
		UARTDebugOut("[virtio-blk-%d]: mounted at /%s \r\n", instIdx, disk->fsys->filename);
	}

	AuTextOut("[virtio-blk-%d]: initialized with max %d MB and %d GB \r\n", instIdx, size_mb, size_gb);
	AuTextOut("[virtio-blk-%d]: diskpath : %s \r\n", instIdx, disk->diskPath);

	return 0;
}
