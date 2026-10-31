/**
* @file virtiopci.c
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
 * Shared modern virtio-pci (v1.0+) plumbing: PCI capability-list walk,
 * BAR resolution and the ACKNOWLEDGE->DRIVER->FEATURES_OK->DRIVER_OK
 * handshake, plus generic split-ring virtqueue setup sized to whatever
 * queue size the device actually reports. virtioblk.c polls
 * (VIRTIO_MSI_NO_VECTOR); virtiokbd.c/virtiotablet.c bind the queue
 * to MSI-X vector 0 so input IRQs actually fire --axiss
 */

#include <pcie.h>
#include <Drivers/virtio.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <_null.h>

static bool AuVirtioPCIFindCap(uint64_t address, int bus, int dev, int func, uint8_t cfgType,
								uint8_t* outBar, uint32_t* outOffset, uint32_t* outLength,
								uint32_t* outNotifyMult) {
	uint32_t capptr = AuPCIERead64(address, PCI_CAPABILITIES_PTR, 1, bus, dev, func) & 0xFF;
	/* A corrupt device must not make early boot spin forever. PCI capabilities
	 * are at least two-byte aligned and the list is bounded by config space. */
	for (uint32_t count = 0; capptr != 0 && count < 48; ++count) {
		if (capptr < 0x40 || capptr > 0xFC || (capptr & 1))
			break;
		uint8_t capId = AuPCIERead64(address, capptr + 0, 1, bus, dev, func) & 0xFF;
		uint8_t capNext = AuPCIERead64(address, capptr + 1, 1, bus, dev, func) & 0xFF;
		if (capId == VIRTIO_PCI_CAP_VENDOR_ID) {
			uint8_t capLen = AuPCIERead64(address, capptr + 2, 1, bus, dev, func) & 0xFF;
			if (capLen < 16 || capptr + capLen > 0x100)
				break;
			uint8_t type = AuPCIERead64(address, capptr + 3, 1, bus, dev, func) & 0xFF;
			if (type == cfgType) {
				*outBar = AuPCIERead64(address, capptr + 4, 1, bus, dev, func) & 0xFF;
				*outOffset = AuPCIERead64(address, capptr + 8, 4, bus, dev, func);
				*outLength = AuPCIERead64(address, capptr + 12, 4, bus, dev, func);
				if (outNotifyMult && cfgType == VIRTIO_PCI_CAP_NOTIFY_CFG)
					*outNotifyMult = AuPCIERead64(address, capptr + 16, 4, bus, dev, func);
				return true;
			}
		}
		capptr = capNext;
	}
	return false;
}

static void* AuVirtioPCIMapCap(uint64_t address, int bus, int dev, int func, uint8_t bar,
								uint32_t offset, uint32_t length) {
	size_t barsz = 0;
	uint64_t barBase = AuPCIEReadBAR(address, bus, dev, func, bar, &barsz);
	if (barBase == 0 || barsz == 0 || length == 0 || offset > barsz || length > barsz - offset)
		return NULL;
	uint64_t mapBase = barBase & ~(uint64_t)(PAGE_SIZE - 1);
	uint64_t delta = barBase - mapBase;
	uint64_t pages = (delta + offset + length + (PAGE_SIZE - 1)) / PAGE_SIZE;
	uint8_t* mapped = (uint8_t*)AuMapMMIO(mapBase, pages);
	if (!mapped)
		return NULL;
	return mapped + delta + offset;
}

/**
 * @brief AuVirtioPCIInit -- see Drivers/virtio.h
 */
bool AuVirtioPCIInit(uint64_t address, int bus, int dev, int func, uint32_t wantedFeaturesLow,
					  struct VirtioPCIDevice* out) {
	memset(out, 0, sizeof(*out));
	out->address = address;
	out->bus = bus;
	out->dev = dev;
	out->func = func;

	uint16_t command = AuPCIERead(address, PCI_COMMAND, bus, dev, func);
	command |= 0x7; /* I/O space, memory space, bus master */
	AuPCIEWrite(address, PCI_COMMAND, command, bus, dev, func);
	isb_flush();
	dsb_ish();

	uint8_t bar;
	uint32_t offset, length, notifyMult = 0;

	if (!AuVirtioPCIFindCap(address, bus, dev, func, VIRTIO_PCI_CAP_COMMON_CFG, &bar, &offset,
							 &length, NULL)) {
		UARTDebugOut("virtio-pci: no common cfg capability \r\n");
		return false;
	}
	out->common = (struct VirtioCommonCfg*)AuVirtioPCIMapCap(address, bus, dev, func, bar, offset,
															   length);

	if (AuVirtioPCIFindCap(address, bus, dev, func, VIRTIO_PCI_CAP_ISR_CFG, &bar, &offset, &length,
							NULL))
		out->isr = (volatile uint8_t*)AuVirtioPCIMapCap(address, bus, dev, func, bar, offset,
														  length);

	if (AuVirtioPCIFindCap(address, bus, dev, func, VIRTIO_PCI_CAP_DEVICE_CFG, &bar, &offset,
							&length, NULL))
		out->deviceCfg = AuVirtioPCIMapCap(address, bus, dev, func, bar, offset, length);

	if (!AuVirtioPCIFindCap(address, bus, dev, func, VIRTIO_PCI_CAP_NOTIFY_CFG, &bar, &offset,
							 &length, &notifyMult)) {
		UARTDebugOut("virtio-pci: no notify cfg capability \r\n");
		return false;
	}
	out->notifyBase = (uint8_t*)AuVirtioPCIMapCap(address, bus, dev, func, bar, offset, length);
	out->notifyOffMultiplier = notifyMult;

	if (!out->common || !out->notifyBase) {
		UARTDebugOut("virtio-pci: failed to map a required capability BAR \r\n");
		return false;
	}

	/* reset, then ACKNOWLEDGE -> DRIVER (virtio-v1.1 sec 3.1.1) */
	out->common->DeviceStatus = 0;
	isb_flush();
	dsb_ish();
	out->common->DeviceStatus = VIRTIO_STATUS_ACKNOWLEDGE;
	isb_flush();
	dsb_ish();
	out->common->DeviceStatus |= VIRTIO_STATUS_DRIVER;
	isb_flush();
	dsb_ish();

	out->common->DevFeatureSelect = 0;
	isb_flush();
	dsb_ish();
	uint32_t devFeatLow = out->common->DevFeature;
	out->common->DevFeatureSelect = 1;
	isb_flush();
	dsb_ish();
	uint32_t devFeatHigh = out->common->DevFeature;

	uint32_t wantHigh = 1U << (VIRTIO_F_VERSION_1_BIT - 32);
	uint32_t negHigh = devFeatHigh & wantHigh;
	if (!(negHigh & wantHigh)) {
		UARTDebugOut("virtio-pci: device does not offer VIRTIO_F_VERSION_1 \r\n");
		out->common->DeviceStatus |= VIRTIO_STATUS_FAILED;
		return false;
	}
	uint32_t negLow = devFeatLow & wantedFeaturesLow;

	out->common->GuestFeatureSelect = 0;
	out->common->GuestFeature = negLow;
	isb_flush();
	dsb_ish();
	out->common->GuestFeatureSelect = 1;
	out->common->GuestFeature = negHigh;
	isb_flush();
	dsb_ish();

	out->common->DeviceStatus |= VIRTIO_STATUS_FEATURES_OK;
	isb_flush();
	dsb_ish();

	uint8_t status = out->common->DeviceStatus;
	if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
		UARTDebugOut("virtio-pci: device rejected the negotiated feature set \r\n");
		out->common->DeviceStatus |= VIRTIO_STATUS_FAILED;
		return false;
	}

	/* caller sets up its virtqueues, then raises DRIVER_OK itself */
	return true;
}

/**
 * @brief AuVirtioPCISetupQueue -- see Drivers/virtio.h
 */
uint16_t AuVirtioPCISetupQueue(struct VirtioPCIDevice* dev, uint16_t qidx,
								struct VirtqDesc** outDesc, struct VirtqAvailHdr** outAvail,
								struct VirtqUsedHdr** outUsed, uint16_t msixVector) {
	dev->common->QueueSelect = qidx;
	isb_flush();
	dsb_ish();
	uint16_t qsize = dev->common->QueueSize;
	if (qsize == 0) {
		UARTDebugOut("virtio-pci: queue %d unavailable (size 0) \r\n", qidx);
		return 0;
	}

	size_t descBytes = (size_t)qsize * sizeof(struct VirtqDesc);
	size_t availBytes = sizeof(struct VirtqAvailHdr) + (size_t)qsize * sizeof(uint16_t);
	size_t usedBytes = sizeof(struct VirtqUsedHdr) + (size_t)qsize * sizeof(struct VirtqUsedElem);
	size_t descPages = (descBytes + PAGE_SIZE - 1) / PAGE_SIZE;
	size_t availPages = (availBytes + PAGE_SIZE - 1) / PAGE_SIZE;
	size_t usedPages = (usedBytes + PAGE_SIZE - 1) / PAGE_SIZE;
	uint64_t descPhys = AuPmmngrAllocPages(descPages, 1, 0, AURORA_PAGE_NORMAL);
	uint64_t availPhys = AuPmmngrAllocPages(availPages, 1, 0, AURORA_PAGE_NORMAL);
	uint64_t usedPhys = AuPmmngrAllocPages(usedPages, 1, 0, AURORA_PAGE_NORMAL);
	if (descPhys == UINT64_MAX || availPhys == UINT64_MAX || usedPhys == UINT64_MAX) {
		if (descPhys != UINT64_MAX) AuPmmngrReleasePages(descPhys);
		if (availPhys != UINT64_MAX) AuPmmngrReleasePages(availPhys);
		if (usedPhys != UINT64_MAX) AuPmmngrReleasePages(usedPhys);
		UARTDebugOut("virtio-pci: queue %d allocation failed\r\n", qidx);
		return 0;
	}

	struct VirtqDesc* desc = (struct VirtqDesc*)AuMapMMIO(descPhys, descPages);
	struct VirtqAvailHdr* avail = (struct VirtqAvailHdr*)AuMapMMIO(availPhys, availPages);
	struct VirtqUsedHdr* used = (struct VirtqUsedHdr*)AuMapMMIO(usedPhys, usedPages);
	if (!desc || !avail || !used) {
		UARTDebugOut("virtio-pci: queue %d mapping failed\r\n", qidx);
		AuPmmngrReleasePages(descPhys);
		AuPmmngrReleasePages(availPhys);
		AuPmmngrReleasePages(usedPhys);
		return 0;
	}

	memset((void*)desc, 0, descPages * PAGE_SIZE);
	memset((void*)avail, 0, availPages * PAGE_SIZE);
	memset((void*)used, 0, usedPages * PAGE_SIZE);

	dev->common->QueueDesc = descPhys;
	dev->common->QueueAvail = availPhys;
	dev->common->QueueUsed = usedPhys;
	/* config + queue share vector 0 when the caller enabled MSI-X; polling
	 * devices pass VIRTIO_MSI_NO_VECTOR so the device will not interrupt */
	if (msixVector != VIRTIO_MSI_NO_VECTOR)
		dev->common->MSix = msixVector;
	dev->common->QueueMSixVector = msixVector;
	isb_flush();
	dsb_ish();
	dev->common->QueueEnable = 1;
	isb_flush();
	dsb_ish();

	*outDesc = desc;
	*outAvail = avail;
	*outUsed = used;
	return qsize;
}

void AuVirtioPCIPostAvail(struct VirtqDesc* desc, struct VirtqAvailHdr* avail,
						  uint16_t qsize, uint64_t bufPhys, uint32_t bufSize,
						  uint16_t nbuf, uint16_t flags) {
	uint16_t i;

	if (!desc || !avail || !qsize || !nbuf || nbuf > qsize || !bufPhys || !bufSize)
		return;
	for (i = 0; i < nbuf; i++) {
		desc[i].addr = bufPhys + (uint64_t)i * bufSize;
		desc[i].len = bufSize;
		desc[i].flags = flags;
		desc[i].next = 0;
		avail->ring[i] = i;
	}
	isb_flush();
	dsb_ish();
	avail->idx = nbuf;
	isb_flush();
	dsb_ish();
	UARTDebugOut("virtio post avail idx=%d nbuf=%d desc0=%x\r\n",
				 avail->idx, nbuf, (uint32_t)desc[0].addr);
}

/**
 * @brief AuVirtioPCINotifyQueue -- see Drivers/virtio.h
 */
void AuVirtioPCINotifyQueue(struct VirtioPCIDevice* dev, uint16_t qidx) {
	dev->common->QueueSelect = qidx;
	isb_flush();
	dsb_ish();
	uint16_t notifyOff = dev->common->QueueNotifyOff;
	volatile uint16_t* notify =
		(volatile uint16_t*)(dev->notifyBase + (uint32_t)notifyOff * dev->notifyOffMultiplier);
	*notify = qidx;
	isb_flush();
	dsb_ish();
}
