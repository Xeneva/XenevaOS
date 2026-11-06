/**
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

#include <Drivers/usb.h>
#include <list.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <aurora.h>
#include <Mm/vmmngr.h>
#include <aucon.h>
#include <audrv.h>
#include <Fs/vfs.h>
#include <pe.h>
#include <string.h>

list_t* usb_devices;
uint8_t* configFile;


/*
 * AuUSBSubsystemInit -- initialize the
 * aurora usb system
 */
void AuUSBSubsystemInit() {
	usb_devices = initialize_list();

	/* Read in the USB driver config file */
	/* Cache it in drv list so that, hotpluggin
	 * doesn't force the system to open and read
	 * config file frequently
	 */
	uint64_t* config = (uint64_t*)P2V((uint64_t)AuPmmngrAlloc());
	memset(config, 0, PAGE_SIZE);

	AuVFSNode* fsys = AuVFSFind("/");
	AuTextOut("FSYS : %x \n", fsys);
	AuVFSNode* file = AuVFSOpen("/usbdrv.cnf");
	if (!file) {
		AuTextOut("[Aurora]: USB failed to open usbdrv.cnf, file not found \n");
		AuTextOut("[Aurora]: Failed to initialise USB subsystem \n");
		return;
	}

	int filesize = file->size / 1024;

	if (filesize < 4096)
		AuVFSNodeReadBlock(fsys, file, (uint64_t*)V2P((size_t)config));

	configFile = (uint8_t*)config;


	/* if any driver is not cached, then we recheck it
	 * if still, it's not present, we wait for the user
	 * to install it on the system
	 */
}


int AuUSBLoadDriver(AuUSBDeviceStruc* dev, uint32_t vendor_id, uint32_t device_id, uint8_t* buffer, int entryoff) {

	/* Get the entry offset for required device driver */
	char* offset = AuGetConfEntry(vendor_id, device_id, buffer, entryoff);

	if (offset == NULL)
		return 1;
	char* p = strchr(offset, ']');
	if (p)
		p++;

	/* get the driver path */
	char drivername[32];
	memset(drivername, 0, 32);
	int i = 0;
	for (i = 0; i < 32; i++) {
		if (p[i] == '|')
			break;
		drivername[i] = p[i];
	}

	AuTextOut("USB Drivername -> %s \r\n", drivername);
	drivername[i] = 0;

	uint64_t driver_load_base = AuDrvMgrGetBaseAddress();

	int next_base_offset = 0;
	uint64_t* virtual_base = (uint64_t*)driver_load_base;

	AuVFSNode* fsys = AuVFSFind(drivername);
	AuVFSNode* file = AuVFSOpen(drivername);
	uint64_t* buffer_ = (uint64_t*)AuPmmngrAlloc();
	memset(buffer_, 0, 4096);
	AuVFSNodeReadBlock(fsys, file, buffer_);
	AuMapPage((uint64_t)buffer_, driver_load_base, 0);
	next_base_offset = 1;


	while (file->eof != 1) {
		uint64_t* block = (uint64_t*)AuPmmngrAlloc();
		memset(block, 0, 4096);
		AuVFSNodeReadBlock(fsys, file, block);
		AuMapPage((uint64_t)block, (driver_load_base + static_cast<uint64_t>(next_base_offset) * 4096), 0);
		next_base_offset++;
	}

	IMAGE_DOS_HEADER* dos_ = (IMAGE_DOS_HEADER*)virtual_base;
	PIMAGE_NT_HEADERS nt = raw_offset<PIMAGE_NT_HEADERS>(dos_, dos_->e_lfanew);

	uint8_t* relocatebuff = (uint8_t*)virtual_base;
	uint64_t original_base = nt->OptionalHeader.ImageBase;
	uint64_t new_addr = (uint64_t)virtual_base;
	uint64_t diff = new_addr - original_base;
	AuKernelRelocatePE(relocatebuff, nt, diff);

	void* entry_addr = AuGetProcAddress((void*)driver_load_base, "AuUSBDriverMain");
	void* unload_addr = AuGetProcAddress((void*)driver_load_base, "AuUSBDriverUnload");

	AuKernelLinkDLL(virtual_base);

	driver_load_base = driver_load_base + static_cast<uint64_t>(next_base_offset) * 4096;
	AuDrvMgrSetBaseAddress(driver_load_base);

	au_usb_drv_entry entry = (au_usb_drv_entry)entry_addr;
	au_usb_drv_unload unload = (au_usb_drv_unload)unload_addr;
	dev->ClassEntry = entry;
	dev->ClassUnload = unload;
	entry(dev);

	return 0;
}


/*
 * AuUSBGetDeviceStruc -- returns USB device struc by looking
 * its data field
 * @param data -- Pointer to host controller related data
 */
AU_EXTERN AU_EXPORT void* AuUSBGetDeviceStruc(void* data) {
	for (int j = 0; j < usb_devices->pointer; j++) {
		AuUSBDeviceStruc* dev = (AuUSBDeviceStruc*)list_get_at(usb_devices, j);
		if (dev->data == data)
			return dev;
	}
	return NULL;
}
/*
 * AuUSBDeviceDisconnect -- device detach callback
 * called from host controller
 * @param dev -- Pointer to USB Device structure
 */
AU_EXTERN AU_EXPORT void AuUSBDeviceDisconnect(AuUSBDeviceStruc* dev) {
	/* free up the device data structure and
	 * the driver allocated memory
	 */
	if (!dev) 
		return;
	if (!dev->ClassUnload)
		return;

	dev->ClassUnload(dev);
	/* now free up everything here */
	/* TODO: free up allocated physical memories */
}

/*
 * AuUSBDeviceConnect -- function is called from the host
 * driver whenever a port change event occurs
 * @param device -- Pointer to AuUSBDevice passed by the host
 */
AU_EXTERN AU_EXPORT void AuUSBDeviceConnect(AuUSBDeviceStruc* device) {

	AuTextOut("USB: New device connected : Class -> %x, SubClass -> %x \n", device->classCode, device->subClassCode);
	
	if (AuUSBLoadDriver(device, device->classCode, device->subClassCode, configFile, 1)) {
		AuTextOut("[USB]: Failed to load to device-driver, Class Code -> %x, Sub Class Code -> %x \n", device->classCode,
			device->subClassCode);
		return;
	}
	device->driverInitialized = true;
	list_add(usb_devices, device);


	/* check for the class code and subclass code */
	/* check for the registered driver files with
	 * that class code and subclass code
	 * if found, load and start that driver
	 */

}
