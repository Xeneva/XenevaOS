/**
* @file virt.c
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
#include <Drivers/virtio.h>
#include <Drivers/usb.h>
#include <string.h>
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>

#define VIRTIO_INPUT_KEYBOARD 1
#define VIRTIO_INPUT_TABLET	  2

uint8_t AuVirtIOInputCheck(uint64_t device, int bus, int dev, int func) {
	uint64_t barLo = AuPCIERead(device, PCI_BAR4, bus, dev, func);
	uint64_t barHi = AuPCIERead(device, PCI_BAR5, bus, dev, func);
	uint64_t bar = ((uint64_t)barHi << 32) | (barLo & ~0xFULL);

	/* we don't need to map the bar to MMIO range, because this function
	 * is temporary one, won't be used later in the system. We have
	 * long way to go when lower half address range is cleared. Till then
	 * kernel has lower half of address range present, so taking advantage
	 * of lower half till then
	 */
	uint64_t finalAddr = bar;
	struct VirtioDeviceConfig* cfg = (struct VirtioDeviceConfig*)(finalAddr + 0x2000);
	cfg->select = 1;
	cfg->subsel = 0;
	isb_flush();
	dsb_ish();
	if (strstr(cfg->data.str, "Keyboard"))
		return VIRTIO_INPUT_KEYBOARD;
	else if (strstr(cfg->data.str, "Tablet"))
		return VIRTIO_INPUT_TABLET;

	return 0;
}

/**
 * @brief AuVirtIOInputInitialize -- initialize virtIO input device
 */
/* virtio-blk-pci, modern-only (disable-legacy=on in the qemu launch args) --
 * see BaseHdr/Drivers/virtio.h for the rest of the 0x1AF4:0x10xx scheme */
#define VIRTIO_PCI_DEVICE_ID_BLK 0x1042

/* headroom for blk+net+kbd+tablet+gpu/snd without silently dropping inputs.
 * virtio-rng (0x1044) is claimed later by the DCL virtio-rng.ko probe. */
#define MAX_VIRTIO_DEVICES 8

void AuVirtIOInputInitialize() {
	UARTDebugOut("AuVirtIO initializing inputs \r\n");
	int numVirtIODevice = 0;
	for (int bus = 0; bus < 255 && numVirtIODevice < MAX_VIRTIO_DEVICES; bus++) {
		for (int dev = 0; dev < PCI_DEVICE_PER_BUS && numVirtIODevice < MAX_VIRTIO_DEVICES; dev++) {
			for (int func = 0; func < PCI_FUNCTION_PER_DEVICE && numVirtIODevice < MAX_VIRTIO_DEVICES;
				 func++) {
				uint64_t address = AuPCIEGetDevice(0, bus, dev, func);
				if (address == 0)
					continue;
				if (address == 0xFFFFFFFF)
					continue;

				uint8_t class_code = AuPCIERead(address, PCI_CLASS, bus, dev, func);
				uint8_t sub_ClassCode = AuPCIERead(address, PCI_SUBCLASS, bus, dev, func);
				uint16_t vendID = AuPCIERead(address, PCI_VENDOR_ID, bus, dev, func);
				uint16_t devID = AuPCIERead(address, PCI_DEVICE_ID, bus, dev, func);
				// UARTDebugOut("Vendor ID attached : %x devID : %x \r\n", vendID, devID);
				//UARTDebugOut("Vendor ID attached : %x devID : %x \r\n", vendID, devID);
				if (vendID == 0x1AF4 && devID == 0x1052) {
					uint8_t devType = AuVirtIOInputCheck(address, bus, dev, func);
					if (devType == VIRTIO_INPUT_KEYBOARD) {
						numVirtIODevice++;
						AuVirtioKbdInitialize(address, bus, dev, func);
					} else if (devType == VIRTIO_INPUT_TABLET) {
						numVirtIODevice++;
						AuVirtioTabletInitialize(address, bus, dev, func);
					}
				}
				if (vendID == 0x1AF4 && (devID == 0x1041 || devID == 0x1000)) {
					/* external virtnet.dll owns this device; do NOT consume
					 * a MAX_VIRTIO_DEVICES slot or later inputs (tablet)
					 * get skipped depending on PCI order --axiss */
					UARTDebugOut("[aurora]: skipping virtionet initialization inside kernel \r\n");
					//AuVirtioNetInitialize(address);
				}
				if (vendID == 0x1AF4 && devID == VIRTIO_PCI_DEVICE_ID_BLK) {
					numVirtIODevice++;
					AuVirtioBlkInitialize(address, bus, dev, func);
				}
			}
		}
	}
	UARTDebugOut("Virtio Input initialized \r\n");
	AuXhciInitialize();
}

#define GOOGLE_GOLDFISH_RTC_BASE 0x09010000
#define RTC_TIME_LOW			 ((volatile uint32_t*)(GOOGLE_GOLDFISH_RTC_BASE + 0x00))
#define RTC_TIME_HIGH			 ((volatile uint32_t*)(GOOGLE_GOLDFISH_RTC_BASE + 0x04))

uint64_t AuVirtGetBootEpoch() {
	uint32_t low = *RTC_TIME_LOW;
	uint32_t high = *RTC_TIME_HIGH;
	return (((uint64_t)high << 32) | low);
}