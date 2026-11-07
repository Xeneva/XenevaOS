/**
* @file audrv.c
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

#include <audrv.h>
#include <aucomponent.h>
#include <Cred/group.h>
#include <aubuiltin.h>
#include <Fs/Dev/devhw.h>
#include <Fs/Dev/devgpu.h>
#include <Sound/sndproxy.h>
#include <Net/netproxy.h>
#include <austatic.h>
#include <Ipc/auipc.h>
#include <Fs/srvfs.h>
#include <Hal/AA64/gic.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <pe.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif
#include <stdio.h>
#include <Mm/kmalloc.h>
#include <pcie.h>
#include <aucon.h>
#include <string.h>
#include <Drivers/uart.h>

/** @TODO: Implement UEFI based pcie discovery mechanism and fix
 * AuBootDriverLoad
 */

/** @brief 0xFFFFC00000400000 - 0xFFFFC00000A00000 -- Kernel Boot Drivers
 *  0xFFFFC00000A00000 - Kernel Runtime Drivers
 */
#define AU_DRIVER_BASE_START	0xFFFFC00000A00000
#define AU_MAX_SUPPORTED_DEVICE 256

AuDriver* drivers[246];
AuDriver* bootDrivers[10];
AuDevice* au_devices[AU_MAX_SUPPORTED_DEVICE];
static int _dev_count_;
static uint32_t driver_class_unique_id = 0;
static uint32_t driver_boot_unique_id = 0;
static uint64_t driver_load_base = 0;

static void AuDriverInstallConsoleHook(AuDriver* driver);

/* Free list of unloaded driver regions (virtual extents above AU_DRIVER_BASE_START).
 * New drivers take the first free extent that fits; otherwise the bump pointer. */
#define AU_DRV_MAX_FREE_REGIONS 64
static struct { uint64_t base; size_t pages; } free_regions[AU_DRV_MAX_FREE_REGIONS];
static int free_region_count = 0;

static uint64_t AuDrvRegionAlloc(size_t pages) {
	for (int i = 0; i < free_region_count; i++) {
		if (free_regions[i].pages < pages)
			continue;
		uint64_t base = free_regions[i].base;
		free_regions[i].base += (uint64_t)pages * 4096;
		free_regions[i].pages -= pages;
		if (free_regions[i].pages == 0) {
			free_regions[i] = free_regions[--free_region_count];
		}
		return base;
	}
	uint64_t base = driver_load_base;
	driver_load_base += (uint64_t)pages * 4096;
	return base;
}

static void AuDrvRegionFree(uint64_t base, size_t pages) {
	uint64_t end = base + (uint64_t)pages * 4096;
	/* give the tail back to the bump pointer if this was the last region */
	if (end == driver_load_base) {
		driver_load_base = base;
		return;
	}
	/* merge with an adjacent free extent */
	for (int i = 0; i < free_region_count; i++) {
		if (free_regions[i].base + (uint64_t)free_regions[i].pages * 4096 == base) {
			free_regions[i].pages += pages;
			return;
		}
		if (end == free_regions[i].base) {
			free_regions[i].base = base;
			free_regions[i].pages += pages;
			return;
		}
	}
	if (free_region_count < AU_DRV_MAX_FREE_REGIONS) {
		free_regions[free_region_count].base = base;
		free_regions[free_region_count].pages = pages;
		free_region_count++;
	}
	/* table full: the extent is leaked, never reused (safe, just wasteful) */
}
uint64_t* scratchBuffer;

//! request an unique id for driver class
uint32_t AuRequestDriverId() {
	uint32_t uid = driver_class_unique_id;
	driver_class_unique_id++;
	return uid;
}

uint32_t AuRequestBootDriverId() {
	uint32_t uid = driver_boot_unique_id;
	driver_boot_unique_id++;
	return uid;
}

/**
 * @brief AuDecreaseDriverCount -- decrease the
 * number of device count
 */
void AuDecreaseDriverCount() {
	_dev_count_--;
}

/**
 * @brief AuIncreaseDriverCount -- increase the
 * number of device count
 */
void AuIncreaseDriverCount() {
	_dev_count_++;
}

/**
* @brief AuGetConfEntry -- Get an entry offset in the file for required device
* @param vendor_id -- vendor id of the product
* @param device_id -- device id of the product
* @param buffer -- configuration file buffer
* @param entryoff -- entry offset from where search begins
*/
char* AuGetConfEntry(uint32_t vendor_id, uint32_t device_id, uint8_t* buffer, int entryoff) {
re:
	int entry_num = entryoff;
	char* fbuf = (char*)buffer;
	/* Check the entry for the device */
search:
	char* p = strchr(fbuf, '(');
	if (p) {
		p++;
		fbuf++;
	}
	int entret = atoi(p);

	/* Check for last entry '(0)' it indicates that
	* there is no more entry
	*/
	if (entret == 0) {
		return 0;
	}

	if (entret != entry_num)
		goto search;

	/* Search for vendor id of the product */
	fbuf = p;
	p = strchr(fbuf, '[');
	int venid, devid = 0;
	int pi = 0;
	if (p)
		p++;

	fbuf = p;
	/* PCI IDs in audrv.cnf are decimal and may use all four digits.
	 * Keep room for the terminator before passing the field to atoi(). */
	char num[16];
	memset(num, 0, sizeof(num));
	int i;
	for (i = 0; i < (int)sizeof(num) - 1; i++) {
		if (p[i] == ',' || p[i] == ']')
			break;
		num[i] = p[i];
		fbuf++;
	}

	venid = atoi(num);

	memset(num, 0, sizeof(num));
	/* Now search for device id / product id */
	p = strchr(fbuf, ',');
	if (p)
		p++;
	for (int i = 0; i < (int)sizeof(num) - 1; i++) {
		if (p[i] == ',' || p[i] == ']')
			break;
		num[i] = p[i];
	}

	devid = atoi(num);

	if (vendor_id != venid || devid != device_id) {
		entryoff++;
		goto re;
	}

	/* Finally we found the device driver */
	if (vendor_id == venid && devid == device_id) {
		return fbuf;
	}

	return 0;
}

/**
 * @brief AuCreateDriverInstance -- creates a new driver
 * slot
 * @param drivername -- name of the driver
 */
AuDriver* AuCreateDriverInstance(char* drivername) {
	AuDriver* driver = (AuDriver*)kmalloc(sizeof(AuDriver));
	memset(driver, 0, sizeof(AuDriver));
	strcpy(driver->name, drivername);
	driver->id = AuRequestDriverId();
	driver->present = false;
	drivers[driver->id] = driver;
	AuIncreaseDriverCount();
	return driver;
}

/**
 * @brief AuCreateDriverInstance -- creates a new driver
 * slot
 * @param drivername -- name of the driver
 */
AuDriver* AuCreateBootDriverInstance(char* drivername) {
	AuDriver* driver = (AuDriver*)kmalloc(sizeof(AuDriver));
	memset(driver, 0, sizeof(AuDriver));
	strcpy(driver->name, drivername);
	driver->id = AuRequestBootDriverId();
	driver->present = false;
	bootDrivers[driver->id] = driver;
	return driver;
}

/**
* @brief AuGetDriverName -- Extract the driver path from its entry offset
* @param vendor_id -- vendor id of the product
* @param device_id -- device id of the product
* @param buffer -- configuration file buffer
* @param entryoff -- entry offset from where search begins
*/
AuDriver* AuGetDriverName(uint32_t vendor_id, uint32_t device_id, uint8_t* buffer, int entryoff) {
	/* Get the entry offset for required device driver */
	char* offset = AuGetConfEntry(vendor_id, device_id, buffer, entryoff);
	if (offset == NULL)
		return NULL;
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

	drivername[i] = 0;
    AuTextOut("drivername : %s \r\n", drivername);
	AuDriver* drv = AuCreateDriverInstance(drivername);
	return drv;
}

/**
 * @brief AuBoardIterateModule -- iterate all modules 
 * present in board config file
 * @param board -- pointer to board config buffer
 * @param moduleCount -- total number of module described
 */
void AuBoardIterateModule(uint8_t* board, int moduleCount) {
	char* fbuf = (char*)board;
	int fcount = 0;
search:
	if (fcount >= moduleCount)
		return;
	char* p = strchr(fbuf, '[');
	if (p) {
		p++;
		fbuf++;
	} else {
		return;
	}

	char fontname[33];
	int i = 0;
	for (i = 0; i < 32; i++) {
		if (p[i] == ']') {
			fbuf = p + i;
			fbuf++;
			break;
		}
		fontname[i] = p[i];
		fbuf++;
	}
	fontname[i] = 0;

	p = fbuf;
	char filename[33];
	for (i = 0; i < 32; i++) {
		if (p[i] == '|')
			break;
		filename[i] = p[i];
		fbuf++;
	}
	filename[i] = 0;

	AuTextOut("[aurora]: Board entry : %s \r\n", filename);
	AuCreateDriverInstance(filename);
	goto search;
}

/**
* @brief AuDriverLoad -- Manage and loads dll drivers
* @param filename -- file path
* @param driver -- driver instance
*/
void AuDriverLoad(char* filename, AuDriver* driver) {
	int next_base_offset = 0;
	uint64_t* virtual_base = 0;
	uint64_t load_base = 0;
	AuTextOut("[aurora]: Loading driver -> %s\r\n", filename);

	/* A user-space hwdev capability is the exclusive owner of its PCI
	 * function. Reject the load before mapping either a static or DLL image;
	 * otherwise a reload can race the capability handoff and create two
	 * independent command paths to the same hardware. Non-PCI board drivers
	 * have no device address and are not subject to this check. */
	if (driver && driver->device &&
		AuHwdevClaimsPci(driver->bus, driver->dev, driver->func)) {
		UARTDebugOut("[audrv]: %s blocked by live user PCI capability at %x:%x.%x\r\n",
			filename, driver->bus, driver->dev, driver->func);
		driver->entry = NULL;
		driver->unload = NULL;
		driver->present = false;
		return;
	}

	/* unikernel profile: the driver is part of this image, there is nothing to load */
	const AuStaticComponent* sc = AuStaticComponentFind(filename);
	if (sc) {
		driver->entry = sc->entry;
		driver->unload = NULL;
		driver->flags = 0;               /* resident: never unloadable */
		driver->refcount = 0;
		driver->resources = 0;
		driver->base = driver->end = 0;
		driver->region_pages = 0;
		driver->present = true;
		if (sc->console_present)
			AuConsoleSetPresentHook((AuConsolePresentFn)sc->console_present);
		UARTDebugOut("[audrv]: %s is a static component (linked into the kernel)\r\n", filename);
		return;
	}

	AuVFSNode* fsys = AuVFSFind(filename);
	AuVFSNode* file = AuVFSOpen(filename);
	if (!file) {
		AuTextOut("[aurora]: audrvmngr failed to load driver : %s \n", filename);
		return;
	}
	UARTDebugOut("loading file : %s \r\n", file->filename);

	size_t file_offset = 0;
	while (file->eof != 1) {
		uint64_t block = ((uint64_t)scratchBuffer + file_offset);
		size_t bytes_read = AuVFSNodeReadBlock(fsys, file, (uint64_t*)block);
		if (bytes_read == 0)
			break;
		file_offset += bytes_read;
	}

	UARTDebugOut("File read complete : scratch buffer : %x \r\n", scratchBuffer);
	IMAGE_DOS_HEADER* dos_ = (IMAGE_DOS_HEADER*)scratchBuffer;
	PIMAGE_NT_HEADERS nt = RAW_OFFSET(PIMAGE_NT_HEADERS, dos_, dos_->e_lfanew);
	driver->original_load_base = nt->OptionalHeader.ImageBase;
	driver->image_sz = nt->OptionalHeader.SizeOfImage;
	driver->region_pages = (driver->image_sz + 4095) / 4096;
	load_base = AuDrvRegionAlloc(driver->region_pages);
	virtual_base = (uint64_t*)load_base;
	AuTextOut("[aurora]: driver %s at %x (%d pages)\r\n", filename, load_base, (int)driver->region_pages);
	PSECTION_HEADER sectionHeader =
		RAW_OFFSET(PSECTION_HEADER, &nt->OptionalHeader, nt->FileHeader.SizeOfOptionaHeader);

	uint64_t first_block = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	/* first_block is a PHYSICAL address: reach it through the kernel direct
	 * map. The identity map that made the raw pointer work only exists during
	 * early boot, so a load triggered from a user process (/dev/drvctl) faulted. */
	memset((void*)P2V(first_block), 0, 4096);
	AuMapPage(first_block, load_base, PTE_NORMAL_MEM);
	memcpy((void*)load_base, scratchBuffer, nt->OptionalHeader.SizeOfHeaders);
	next_base_offset++;

	for (size_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
		size_t load_addr = (size_t)virtual_base + sectionHeader[i].VirtualAddress;
		void* sect_addr = (void*)load_addr;
		size_t sectsz = sectionHeader[i].VirtualSize;
		if (sectionHeader[i].SizeOfRawData > sectsz)
			sectsz = sectionHeader[i].SizeOfRawData;

		size_t aligned_addr = load_addr & ~0xFFFULL;
		int req_pages = (sectsz + (load_addr & 0xFFF)) / 4096 +
						(((sectsz + (load_addr & 0xFFF)) % 4096) ? 1 : 0);
		uint64_t* block = 0;
		for (int j = 0; j < req_pages; j++) {
			uint64_t alloc = (aligned_addr + j * 0x1000);
			if (!AuGetPhysicalAddress(alloc)) {
				AuMapPage((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL), alloc, PTE_NORMAL_MEM);
				memset((void*)alloc, 0, 4096);
				next_base_offset++;
			}
			if (!block)
				block = (uint64_t*)alloc;
		}

		memcpy(sect_addr,
			   RAW_OFFSET(uint8_t*, scratchBuffer, sectionHeader[i].PointerToRawData),
			   sectionHeader[i].SizeOfRawData);
		if (sectionHeader[i].VirtualSize > sectionHeader[i].SizeOfRawData)
			memset(RAW_OFFSET(void*, sect_addr, sectionHeader[i].SizeOfRawData),
				   0,
				   sectionHeader[i].VirtualSize - sectionHeader[i].SizeOfRawData);
	}

	uint8_t* relocatebuff = (uint8_t*)virtual_base;
	uint64_t original_base = nt->OptionalHeader.ImageBase;
	uint64_t new_addr = (uint64_t)virtual_base;
	driver->new_load_base = new_addr;
	uint64_t diff = new_addr - original_base;

	AuKernelRelocatePE(relocatebuff, nt, diff);
	void* entry_addr = AuGetProcAddress((void*)load_base, "AuDriverMain");
	void* unload_addr = AuGetProcAddress((void*)load_base, "AuDriverUnload");
	uint32_t* flags_addr = (uint32_t*)AuGetProcAddress((void*)load_base, "AuDriverFlags");
	AuKernelLinkDLL((void*)virtual_base);
	driver->entry = (au_drv_entry)entry_addr;
	driver->unload = (au_drv_unload)unload_addr;
	driver->flags = flags_addr ? *flags_addr : 0;
	driver->refcount = 0;
	driver->resources = 0;
	driver->base = AU_DRIVER_BASE_START;
	driver->end = driver->base + file->size;
	driver->present = true;
	(void)next_base_offset; /* mapping count is now bounded by region_pages */
	kfree(file);
}

/* Install callbacks only after the driver's entry point has completed. A
 * driver may emit kernel output while it is still allocating its queues and
 * framebuffer; exposing its presenter earlier lets that output call into a
 * partially initialized image. */
static void AuDriverInstallConsoleHook(AuDriver* driver) {
	if (!driver || !driver->new_load_base)
		return;
	AuConsolePresentFn present =
		(AuConsolePresentFn)AuGetProcAddress((void*)driver->new_load_base,
			"VirtGpuConsolePresent");
	if (present) {
		AuConsoleSetPresentHook(present);
		UARTDebugOut("[audrv]: console mirror hooked\r\n");
	}
}

extern bool AuIsPCIeInitialized();

/** we're using FontManagerGetFontCount because
 * both board config descriptor file and 
 * font config file share same writing style
 */
extern int FontManagerGetFontCount(uint8_t* buffer);
/**
* @brief AuDrvMngrInitialize -- Initialize the driver manager
* @param info -- kernel boot info
*/
void AuDrvMngrInitialize(KERNEL_BOOT_INFO* info) {
	driver_class_unique_id = 0;
	driver_boot_unique_id = 0;
	driver_load_base = AU_DRIVER_BASE_START;
	_dev_count_ = 0;

	/* I copy driver images through this 1 MiB physically contiguous area, fragile but works? --axiss */
	scratchBuffer = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPages(
		(1024 * 1024) / 0x1000, 1, 0, AURORA_PAGE_KERNEL));
	if (!scratchBuffer) {
		AuTextOut("[aurora]: unable to allocate driver scratch memory\r\n");
		return;
	}
	memset(scratchBuffer, 0, 1024 * 1024);

	AuTextOut("[aurora]: initializing drivers, please wait... \r\n");
	/* Load the conf data */
	uint64_t* conf = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	uint64_t* boardcnf = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(conf, 0, PAGE_SIZE);
	memset(boardcnf, 0, PAGE_SIZE);

	AuVFSNode* fsys = AuVFSFind("/");
	AuVFSNode* file = AuVFSOpen("/audrv.cnf");
	if (!file) {
		AuTextOut("[aurora]: Driver Manager failed to open audrv.cnf, file not found \r\n");
		return;
	}
	int filesize = file->size / 1024;

	if (filesize < 4096)
		AuVFSNodeReadBlock(fsys, file, conf);

	uint8_t* confdata = (uint8_t*)conf;

	bool proceed = 0;

	AuTextOut("[aurora]: audrv.cnf read successfully %s \r\n", file->filename);

	/** now initialize board specific driver list **/
	AuVFSNode* board = AuVFSOpen("/board.cnf");
	if (board) {
		filesize = board->size / 1024;
		if (filesize < 4096) {
			AuVFSNodeReadBlock(fsys, board, boardcnf);
		}
		AuTextOut("[aurora]: board.cnf read successfully %s \r\n", board->filename);

	} else
		AuTextOut("[aurora] Driver Manager failed to open board.cnf, file not found \r\n");

	AuDriver* drv = NULL;
	/* AuDrvManager will be responsible for loading drivers through
	 * scanning MMIO, PCIe and other bus systems
	 */
	if (AuIsPCIeInitialized()) {
		uint32_t vend_id, dev_id, class_code, sub_class = 0;
		uint32_t device = 0;
		for (uint16_t bus = 0; bus < 0x20; bus++) {
			for (uint16_t dev = 0; dev < 32; dev++) {
				for (uint16_t func = 0; func < 8; func++) {
					uint64_t device = AuPCIEGetDevice(0, bus, dev, func);

					vend_id = AuPCIERead(device, PCI_VENDOR_ID, bus, dev, func);
					dev_id = AuPCIERead(device, PCI_DEVICE_ID, bus, dev, func);
					class_code = AuPCIERead(device, PCI_CLASS, bus, dev, func);
					sub_class = AuPCIERead(device, PCI_SUBCLASS, bus, dev, func);
					/*if (class_code != 0xFF && sub_class != 0xFF) {
						AuTextOut("Class Code : %x, subClass : %x \n", class_code, sub_class);
					}*/
					if (dev_id == 0xFFFF || vend_id == 0xFFFF)
						continue;
					char* offset = AuGetConfEntry(class_code, sub_class, confdata, 1);
					if (offset == NULL)
						drv = AuGetDriverName(vend_id, dev_id, confdata, 1);
					else
						drv = AuGetDriverName(class_code, sub_class, confdata, 1);

					if (drv) {
						drv->classCode = class_code;
						drv->subClassCode = sub_class;
						drv->device = device;
						drv->vendorID = vend_id;
						drv->deviceID = dev_id;
						drv->bus = bus;
						drv->func = func;
						drv->dev = dev;
						drv = NULL;
					}
				}
			}
		}
		proceed = 1;
	}

#if !defined(__TARGET_BOARD_QEMU_VIRT__)
	/*if PCIe fails then */
	if (board) {
		/** using font manager's function because both board config
		 * data and font manager config data shares same style
		 */
		int num_module_entry = FontManagerGetFontCount((uint8_t*)boardcnf);
		AuTextOut("[aurora]: number of board's module entry : %d \r\n", num_module_entry);
		AuBoardIterateModule(boardcnf, num_module_entry);
		proceed = 1;
	}
#endif

	if (proceed) {
		/* Serially call each startup entries of each driver */
		for (int i = 0; i < driver_class_unique_id; i++) {
			AuDriver* driver = drivers[i];
		#ifdef __XENEVA_GPUSRV__
			/* Keep the fallback component registered for drvctl hand-back, but
			 * never probe it in the user-space GPU profile. The first owner of
			 * the virtio-GPU PCI function is gpusrv, not a kernel DLL. */
			if (driver && strstr(driver->name, "virtgpu")) {
				UARTDebugOut("[audrv]: virtgpu reserved for user-space gpusrv\r\n");
				continue;
			}
		#endif
			AuDriverLoad(driver->name, driver);
			if (driver->entry) {
				if (driver->entry(driver) == 0)
					AuDriverInstallConsoleHook(driver);
			}
		}
		/* Station driver is not a PCI function. virtio-net stays class 02,00. */
		if (AuVFSOpen("/wifi.dll")) {
			AuDriver* wifi = AuCreateDriverInstance("/wifi.dll");
			if (wifi) {
				AuDriverLoad(wifi->name, wifi);
				if (wifi->entry && wifi->entry(wifi) == 0)
					AuDriverInstallConsoleHook(wifi);
			}
		}
	}

	AuDriverCtlInitialize();
	AuHwdevInitialize();
	#ifdef __XENEVA_GPUSRV__
	AuGpuProxyInitialize();
	#endif
	AuSndProxyInitialize();
	AuNetProxyInitialize();
	AuIpcInitialize();
	AuSrvfsInitialize();
#ifndef __XENEVA_BYPASS_VIRTIO_INPUT__
	/* The vendored input path writes /dev/kybrd and /dev/mice directly;
	 * a user-space input server would compete for the same functions. */
	AuInputSrvInitialize();
#endif
#ifdef __XENEVA_DRV_SELFTEST__
	{
		extern void AuDriverSelfTest(void);
		AuDriverSelfTest();
	}
#endif
	AuTextOut("[aurora]: AuDrvManager initialized successfully \r\n");
	kfree(file);
}

/**
 * @brief AuRegisterDevice -- register a new device to
 * aurora system
 * @param dev -- Pointer to device to add
 */
AU_EXTERN AU_EXPORT void AuRegisterDevice(AuDevice* dev) {
	au_devices[_dev_count_] = dev;
	_dev_count_++;
}

/**
 * @brief AuCheckDevice -- checks an aurora device if it's
 * already present
 * @param classC -- class code of the device to check
 * @param subclassC -- sub class code of the device to check
 * @param progIF -- programming interface of the device
 */
AU_EXTERN AU_EXPORT bool AuCheckDevice(uint16_t classC, uint16_t subclassC, uint8_t progIF) {
	for (int i = 0; i < _dev_count_; i++) {
		if (au_devices[i]->classCode == classC && au_devices[i]->subClassCode == subclassC &&
			au_devices[i]->progIf == progIF)
			return true;
	}
	return false;
}

/**
 * @brief AuBootDriverLoad -- loads all boot drivers
 * @param driverBuffer - Pointer to driver location in memory
 * @param driver -- Pointer to aurora driver structure
 */
void AuBootDriverLoad(void* driverBuffer, AuDriver* driver) {
	int next_base_offset = 0;

	uint64_t buffer = (uint64_t)driverBuffer;
	next_base_offset = 1;

	IMAGE_DOS_HEADER* dos_ = (IMAGE_DOS_HEADER*)driverBuffer;
	PIMAGE_NT_HEADERS nt = RAW_OFFSET(PIMAGE_NT_HEADERS, dos_, dos_->e_lfanew);

	PSECTION_HEADER secthdr =
		RAW_OFFSET(PSECTION_HEADER, &nt->OptionalHeader, nt->FileHeader.SizeOfOptionaHeader);

	void* entry_addr = AuGetProcAddress(driverBuffer, "AuDriverMain");
	void* unload_addr = AuGetProcAddress(driverBuffer, "AuDriverUnload");

	AuKernelLinkDLL(driverBuffer);
	driver->entry = (au_drv_entry)entry_addr;
	driver->unload = (au_drv_unload)unload_addr;
	driver->base = (uint64_t)driverBuffer;
	driver->end = 0;
	driver->present = true;
}

/**
 * @brief AuBootDriverInitialise -- Initialise and load all boot time drivers
 * @param info -- Kernel boot information passed by XNLDR
 * [TODO] : Everything is hard coded for now
 */
AU_EXTERN AU_EXPORT void AuBootDriverInitialise(KERNEL_BOOT_INFO* info) {
	int total_boot_driver = 0;
	/* HARD CODED */

	/* THE AHCI Controller */
	if (info->driver_entry1) {
		AuDriver* driver = AuCreateBootDriverInstance("AHCIController");
		AuBootDriverLoad(info->driver_entry1, driver);
	}

	/* THE NVMe Controller */
	if (info->driver_entry2) {
		AuDriver* driver = AuCreateBootDriverInstance("NVMeController");
		AuBootDriverLoad(info->driver_entry2, driver);
	}

	/* The USB 3.x Controller*/
	if (info->driver_entry3) {
		AuDriver* driver = AuCreateBootDriverInstance("XHCIController");
		AuBootDriverLoad(info->driver_entry3, driver);
	}

	/* Serially call each startup entries of each driver */
	for (int i = 0; i < driver_boot_unique_id; i++) {
		AuDriver* driver = bootDrivers[i];
		driver->entry(driver);
	}
}

/**
 * @brief AuDrvMgrGetBaseAddress -- returns the current
 * driver load base address
 */
uint64_t AuDrvMgrGetBaseAddress() {
	return driver_load_base;
}

/**
 * @brief AuDrvMgrSetBaseAddress -- sets a new base
 * address for driver to load
 * it's highly risky because, if we set it to
 * kernel stack location, kernel will crash
 */
void AuDrvMgrSetBaseAddress(uint64_t base_address) {
	driver_load_base = base_address;
}

/**
 * @brief AuDrvManagerCheckFault -- check if this driver contain the fault
 * address, helpful for debugging
 * @param fault_addr -- address of fault
 */
AuDriver* AuDrvManagerCheckFault(uint64_t fault_addr) {
	for (int i = 0; i < 246; i++) {
		AuDriver* drv = drivers[i];
		if (drv) {
			if (drv->new_load_base != 0 && fault_addr > drv->new_load_base &&
				fault_addr <= drv->new_load_base + drv->image_sz)
				return drv;
		}
	}
	return NULL;
}

/**
 * @brief AuDrvCatchFault -- resolves original virtual address of the driver
 * and relocate its symbol name
 * @param drv -- Pointer to driver 
 * @param fault_addr -- istruction pointer address
 */
void AuDrvCatchFault(AuDriver* drv, uint64_t fault_addr) {
	UARTDebugOut("[aurora]: crash in kernel driver : %s \r\n", drv->name);
	uint64_t rva = fault_addr - drv->new_load_base;
	uint64_t original_va = drv->original_load_base + rva;

	UARTDebugOut(
		"[aurora]: relocated address: %x, original address : %x \r\n", fault_addr, original_va);
	AuCoffResolveAddress((uint8_t*)drv->new_load_base, fault_addr);
}

/**
 * @brief AuDrvUnloadAll -- call unload handler of all registered drivers
 */
void AuDrvUnloadAll() {
	for (int i = 0; i < 246; i++) {
		AuDriver* drv = drivers[i];
		if (drv) {
			if (drv->unload)
				drv->unload(drv);
		}
	}
}


/* ===================================================================
 * Component model: lookup, refcounts, resource ledger, runtime unload
 * =================================================================== */

#include <Fs/Dev/devfs.h>
#include <Sound/sound.h>

static bool AuDriverContains(AuDriver* drv, uint64_t addr) {
	return drv && drv->present && drv->new_load_base &&
		   addr >= drv->new_load_base &&
		   addr < drv->new_load_base + (uint64_t)drv->region_pages * 4096;
}

/**
 * @brief AuDriverOwnerOf -- the loaded driver whose image contains addr
 */
AuDriver* AuDriverOwnerOf(uint64_t addr) {
	for (uint32_t i = 0; i < driver_class_unique_id && i < 246; i++)
		if (AuDriverContains(drivers[i], addr))
			return drivers[i];
	return 0;
}

/* exact name first, then substring ("virtsnd" finds "/virtsnd.dll") */
static AuDriver* AuDriverLookup(const char* name, bool include_unloaded) {
	if (!name || !*name)
		return 0;
	for (int pass = 0; pass < 2; pass++) {
		for (uint32_t i = 0; i < driver_class_unique_id && i < 246; i++) {
			AuDriver* d = drivers[i];
			if (!d || (!d->present && !include_unloaded))
				continue;
			if (pass == 0 ? strcmp(d->name, name) == 0 : strstr(d->name, name) != 0)
				return d;
		}
	}
	return 0;
}

AuDriver* AuDriverFindByName(const char* name) {
	return AuDriverLookup(name, false);
}

int AuDriverRef(AuDriver* drv) {
	if (!drv || !drv->present)
		return -1;
	return ++drv->refcount;
}

void AuDriverUnref(AuDriver* drv) {
	if (drv && drv->refcount > 0)
		drv->refcount--;
}

void AuDriverTrack(AuDriver* drv, uint8_t kind, uint64_t value, uint64_t extra, uint64_t extra2) {
	if (!drv)
		return;
	AuResource* r = (AuResource*)kmalloc(sizeof(AuResource));
	if (!r)
		return;
	r->kind = kind;
	r->value = value;
	r->extra = extra;
	r->extra2 = extra2;
	r->next = (AuResource*)drv->resources;
	drv->resources = r;
}

void AuDriverUntrack(AuDriver* drv, uint8_t kind, uint64_t value) {
	if (!drv)
		return;
	AuResource** link = (AuResource**)&drv->resources;
	while (*link) {
		AuResource* r = *link;
		if (r->kind == kind && r->value == value) {
			*link = r->next;
			kfree(r);
			return;
		}
		link = &r->next;
	}
}

void AuDriverUntrackAny(uint8_t kind, uint64_t value) {
	for (uint32_t i = 0; i < driver_class_unique_id && i < 246; i++)
		if (drivers[i] && drivers[i]->resources)
			AuDriverUntrack(drivers[i], kind, value);
}

/* ---- tracking wrappers handed to drivers instead of the kernel originals ----
 * The owner is found from the caller: a handler pointer or a return address
 * that lies inside a loaded driver image. noinline keeps the return address
 * pointing at the driver. */
#define AU_CALLER_DRIVER() AuDriverOwnerOf((uint64_t)__builtin_return_address(0))

/* A page the driver mapped as MMIO and is now giving back must lose that
 * mapping first, or a stale device-memory alias would outlive the page. */
static void AuDriverDropMmioAlias(uint64_t phys) {
	for (uint32_t i = 0; i < driver_class_unique_id && i < 246; i++) {
		AuDriver* d = drivers[i];
		if (!d)
			continue;
		for (AuResource* r = (AuResource*)d->resources; r; r = r->next) {
			if (r->kind == AU_RES_MMIO && r->extra == phys && r->extra2 == 1) {
				uint64_t virt = r->value;
				AuDriverUntrack(d, AU_RES_MMIO, virt);
				AuFreePages(virt, false, 4096);
				return;
			}
		}
	}
}

__attribute__((noinline)) static void tracked_GICRegisterSPIHandler(void* fptr, int spi) {
	bool was_free = (GICGetSPIHandler(spi) == 0);
	GICRegisterSPIHandler(fptr, spi);
	if (was_free && GICGetSPIHandler(spi) == fptr)
		AuDriverTrack(AuDriverOwnerOf((uint64_t)fptr), AU_RES_IRQ_SPI, (uint64_t)spi, 0, 0);
}

__attribute__((noinline)) static uint64_t tracked_AuPmmngrAllocPage(uint8_t type) {
	uint64_t p = AuPmmngrAllocPage(type);
	if (p)
		AuDriverTrack(AU_CALLER_DRIVER(), AU_RES_PHYS_PAGE, p, 0, 0);
	return p;
}

__attribute__((noinline)) static uint64_t tracked_AuPmmngrAllocPages(uint32_t pages,
	uint32_t alignment, uint64_t ceiling, uint8_t type) {
	uint64_t p = AuPmmngrAllocPages(pages, alignment, ceiling, type);
	if (p)
		AuDriverTrack(AU_CALLER_DRIVER(), AU_RES_PHYS_RUN, p, pages, 0);
	return p;
}

__attribute__((noinline)) static bool tracked_AuPmmngrReleasePage(uint64_t phys) {
	AuDriverDropMmioAlias(phys);
	AuDriverUntrackAny(AU_RES_PHYS_PAGE, phys);
	return AuPmmngrReleasePage(phys);
}

__attribute__((noinline)) static bool tracked_AuPmmngrReleasePages(uint64_t phys) {
	AuDriverUntrackAny(AU_RES_PHYS_RUN, phys);
	return AuPmmngrReleasePages(phys);
}

__attribute__((noinline)) static void* tracked_AuMapMMIO(uint64_t phys, size_t count) {
	void* v = AuMapMMIO(phys, count);
	if (v)
		AuDriverTrack(AU_CALLER_DRIVER(), AU_RES_MMIO, (uint64_t)v, phys, (uint64_t)count);
	return v;
}

static const struct { const char* name; void* fn; } au_import_overrides[] = {
	{"GICRegisterSPIHandler", (void*)tracked_GICRegisterSPIHandler},
	{"AuPmmngrAllocPage",     (void*)tracked_AuPmmngrAllocPage},
	{"AuPmmngrAllocPages",    (void*)tracked_AuPmmngrAllocPages},
	{"AuPmmngrReleasePage",   (void*)tracked_AuPmmngrReleasePage},
	{"AuPmmngrReleasePages",  (void*)tracked_AuPmmngrReleasePages},
	{"AuMapMMIO",             (void*)tracked_AuMapMMIO},
};

void* AuDriverImportOverride(const char* name) {
	for (size_t i = 0; i < sizeof(au_import_overrides) / sizeof(au_import_overrides[0]); i++)
		if (strcmp(au_import_overrides[i].name, name) == 0)
			return au_import_overrides[i].fn;
	return 0;
}

/**
 * @brief AuDriverUnload -- unload a running driver
 *
 * Order matters: ask the driver first (it may veto), then cut every
 * kernel -> driver path BEFORE the image is unmapped: IRQ handlers are
 * cleared and the driver's MMIO mappings removed. Physical pages the driver
 * still owns are only REPORTED, never force-freed: the kernel may have taken
 * them over without telling the ledger, and a leak is safe where a double
 * free is not. Returns an AU_UNLOAD_* code.
 */
int AuDriverUnload(AuDriver* drv) {
	if (!drv || !drv->present)
		return AU_UNLOAD_NOT_FOUND;
	if (!(drv->flags & AU_DRV_FLAG_UNLOADABLE) || !drv->unload) {
		UARTDebugOut("[audrv]: %s did not opt in to unloading\r\n", drv->name);
		return AU_UNLOAD_NOT_OPTED;
	}
	if (drv->refcount > 0) {
		UARTDebugOut("[audrv]: %s busy (refcount %d)\r\n", drv->name, drv->refcount);
		return AU_UNLOAD_BUSY;
	}
	/* The GPU presenter is the only component called from the console writer.
	 * Cut that callback before entering the component's unload routine, then
	 * restore the boot framebuffer after the image is gone. Keeping this in the
	 * kernel manager avoids adding a console-repointing dependency to the
	 * handoff DLL's import library. */
	bool restoring_gpu = strstr(drv->name, "virtgpu") != 0;
	if (restoring_gpu)
		AuConsoleSetPresentHook(0);
	if (drv->unload(drv) != 0) {
		UARTDebugOut("[audrv]: %s vetoed unload\r\n", drv->name);
		return AU_UNLOAD_VETOED;
	}

	int irqs = 0, mmio = 0, leaked_pages = 0;
	AuResource* r = (AuResource*)drv->resources;
	drv->resources = 0;
	while (r) {
		AuResource* next = r->next;
		switch (r->kind) {
		case AU_RES_IRQ_SPI:
			GICClearSPIHandler((int)r->value);
			irqs++;
			break;
		case AU_RES_MMIO:
			AuFreePages(r->value, false, (size_t)r->extra2 * 4096);
			mmio++;
			break;
		case AU_RES_PHYS_PAGE:
			leaked_pages++;
			break;
		case AU_RES_PHYS_RUN:
			leaked_pages += (int)r->extra;
			break;
		}
		kfree(r);
		r = next;
	}

	uint64_t base = drv->new_load_base;
	size_t pages = drv->region_pages;
	drv->present = false;
	drv->entry = 0;
	drv->unload = 0;
	drv->flags = 0;
	AuFreePages(base, true, pages * 4096);
	AuDrvRegionFree(base, pages);
	drv->new_load_base = 0;
	drv->region_pages = 0;
	if (restoring_gpu)
		AuConsoleRestoreBootFb();
	UARTDebugOut("[audrv]: unloaded %s: image %x (%d pages) freed, %d irq cleared, %d mmio unmapped, %d page(s) leaked\r\n",
				 drv->name, base, (int)pages, irqs, mmio, leaked_pages);
	return AU_UNLOAD_OK;
}

int AuDriverUnloadByName(const char* name) {
	return AuDriverUnload(AuDriverLookup(name, false));
}

/**
 * @brief AuDriverLoadByName -- (re)load a driver that is not currently
 * loaded, then run its AuDriverMain. Returns 0 on success.
 */
int AuDriverLoadByName(const char* name) {
	AuDriver* drv = AuDriverLookup(name, true);
	if (!drv)
		return -1;
	if (drv->present)
		return -2;
	AuDriverLoad(drv->name, drv);
	if (!drv->present || !drv->entry)
		return -3;
	int rc = drv->entry(drv);
	if (rc == 0)
		AuDriverInstallConsoleHook(drv);
	if (rc != 0) {
		/* AuDriverLoad marks the image present before calling its entry point.
		 * Do not leave a failed initialization looking like a live driver: the
		 * next handoff would otherwise report "already loaded" and strand any
		 * resources acquired before the failure. Unloadable components own the
		 * partial-init cleanup contract. */
		int cleanup = AuDriverUnload(drv);
		UARTDebugOut("[audrv]: %s initialization failed rc=%d cleanup=%d\r\n",
			drv->name, rc, cleanup);
	}
	return rc;
}

bool AuDriverClaimsPci(int bus, int dev, int func) {
	for (uint32_t i = 0; i < driver_class_unique_id && i < 246; i++) {
		AuDriver* d = drivers[i];
		if (d && d->present && d->bus == bus && d->dev == dev && d->func == func)
			return true;
	}
	return AuBuiltinClaimsPci(bus, dev, func);
}

static void AuDriverCtlListBuiltin(const char* name, bool bound, int bus, int dev, int func) {
	UARTDebugOut("[drvctl]: %s %s (built-in) %x:%x.%x\r\n", name, bound ? "bound" : "released", bus, dev, func);
}

/* ---- /dev/drvctl: write "unload NAME", "load NAME" or "list" ---- */
static int AuDriverCtlExecute(char* cmd) {
	if (strncmp(cmd, "unload ", 7) == 0) {
		int rc = AuDriverUnloadByName(cmd + 7);
		if (rc == AU_UNLOAD_NOT_FOUND)               /* not a loadable driver: try the built-ins */
			rc = AuBuiltinUnload(cmd + 7);
		UARTDebugOut("[drvctl]: unload %s -> %d\r\n", cmd + 7, rc);
		return rc;
	} else if (strncmp(cmd, "load ", 5) == 0) {
		int rc = AuDriverLoadByName(cmd + 5);
		if (rc == -1)
			rc = AuBuiltinLoad(cmd + 5);
		UARTDebugOut("[drvctl]: load %s -> %d\r\n", cmd + 5, rc);
		return rc;
	} else if (strcmp(cmd, "list") == 0) {
		for (uint32_t i = 0; i < driver_class_unique_id && i < 246; i++) {
			AuDriver* d = drivers[i];
			if (d)
				UARTDebugOut("[drvctl]: %s %s base=%x ref=%d unloadable=%d\r\n", d->name,
							 d->present ? "loaded" : "unloaded", d->new_load_base,
							 d->refcount, (d->flags & AU_DRV_FLAG_UNLOADABLE) ? 1 : 0);
		}
		AuBuiltinList(AuDriverCtlListBuiltin);
	} else {
		UARTDebugOut("[drvctl]: unknown command '%s'\r\n", cmd);
		return AU_DRVCTL_E_UNKNOWN;
	}
	return AU_UNLOAD_OK;
}

static size_t AuDriverCtlNormalize(char* cmd, size_t n) {
	cmd[n] = 0;
	while (n && (cmd[n - 1] == '\n' || cmd[n - 1] == '\r' || cmd[n - 1] == ' ' || cmd[n - 1] == '\t'))
		cmd[--n] = 0;
	return n;
}

static size_t AuDriverCtlWrite(AuVFSNode* fsys, AuVFSNode* file, uint64_t* buffer, uint32_t length) {
	(void)fsys; (void)file;
	if (!AuHwdevCallerAllowed(AuHwdevCurrentProcess())) {
		UARTDebugOut("[drvctl]: denied (caller not in the daemons group)\r\n");
		return 0;
	}
	char cmd[AU_DRVCTL_CMD_MAX];
	size_t n = length < sizeof(cmd) - 1 ? length : sizeof(cmd) - 1;
	memcpy(cmd, buffer, n);
	AuDriverCtlNormalize(cmd, n);
	(void)AuDriverCtlExecute(cmd);
	return length;
}

static int AuDriverCtlIoControl(AuVFSNode* file, int code, void* arg) {
	(void)file;
	if (code != AU_DRVCTL_IOC_EXEC || !arg)
		return AU_DRVCTL_E_UNKNOWN;
	if (!AuHwdevCallerAllowed(AuHwdevCurrentProcess())) {
		UARTDebugOut("[drvctl]: denied (caller not in the daemons group)\r\n");
		return AU_UNLOAD_VETOED;
	}
	AuDriverCtlRequest* req = (AuDriverCtlRequest*)arg;
	char cmd[AU_DRVCTL_CMD_MAX];
	memcpy(cmd, req->command, sizeof(cmd));
	cmd[sizeof(cmd) - 1] = 0;
	size_t n = 0;
	while (n < sizeof(cmd) && cmd[n])
		n++;
	AuDriverCtlNormalize(cmd, n);
	return AuDriverCtlExecute(cmd);
}

void AuDriverCtlInitialize(void) {
	AuVFSNode* fsys = AuVFSFind("/dev");
	if (!fsys)
		return;
	AuVFSNode* node = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(node, 0, sizeof(AuVFSNode));
	strcpy(node->filename, "drvctl");
	node->flags = FS_FLAG_DEVICE;
	node->uid = 0;
	/* privileged: the daemons group, like hwdev and sndsrv, so OpenFile refuses an
	 * ordinary process outright instead of relying on the handler's own check */
	node->gid = AuCredGetGroupID(AURORA_GID_MISC_DAEMONS);
	node->device = fsys;
	node->write = AuDriverCtlWrite;
	node->iocontrol = AuDriverCtlIoControl;
	AuDevFSAddFile(fsys, "/", node);
}

#ifdef __XENEVA_DRV_SELFTEST__
extern AuSound* _cards[];
static int AuSelftestCards(void) {
	int n = 0;
	for (int i = 0; i < 256; i++)
		if (_cards[i])
			n++;
	return n;
}

static void AuSelftestLedger(AuDriver* d, int* irq, int* page, int* run, int* mmio) {
	*irq = *page = *run = *mmio = 0;
	for (AuResource* r = (AuResource*)d->resources; r; r = r->next) {
		if (r->kind == AU_RES_IRQ_SPI) (*irq)++;
		else if (r->kind == AU_RES_PHYS_PAGE) (*page)++;
		else if (r->kind == AU_RES_PHYS_RUN) (*run)++;
		else if (r->kind == AU_RES_MMIO) (*mmio)++;
	}
}

/* Boot-time check of the whole path on a real driver (virtsnd): unload it,
 * confirm the card is gone and its address range is free, reload it, confirm
 * it came back at the same address. Results go to the serial log. */
void AuDriverSelfTest(void) {
	int failures = 0;
	AuDriver* d = AuDriverLookup("virtsnd", false);
	if (!d) {
		UARTDebugOut("[drvtest] SKIP: virtsnd not loaded\r\n");
		return;
	}
	uint64_t base0 = d->new_load_base;
	size_t pages0 = d->region_pages;
	int cards0 = AuSelftestCards();
	UARTDebugOut("[drvtest] before: base=%x pages=%d cards=%d flags=%x\r\n", base0, (int)pages0, cards0, d->flags);
	int li, lp, lr, lm;
	AuSelftestLedger(d, &li, &lp, &lr, &lm);
	UARTDebugOut("[drvtest] ledger: irq=%d page=%d run=%d mmio=%d (virtsnd allocates 3 pages + 3 mmio aliases => wrappers must be live)\r\n", li, lp, lr, lm);
	if (lp < 3 || lm < 3) {
		UARTDebugOut("[drvtest] FAIL: import tracking not active\r\n");
		failures++;
	}
	AuDriver* nb = AuDriverLookup("virtblk", false);
	if (nb) {
		int nrc = AuDriverUnload(nb);
		UARTDebugOut("[drvtest] driver without opt-in (%s) -> %d (expect %d NOT_OPTED)\r\n", nb->name, nrc, AU_UNLOAD_NOT_OPTED);
		if (nrc != AU_UNLOAD_NOT_OPTED || !nb->present) failures++;
	}

	AuDriverRef(d);
	int rc = AuDriverUnload(d);
	UARTDebugOut("[drvtest] unload while referenced -> %d (expect %d BUSY)\r\n", rc, AU_UNLOAD_BUSY);
	if (rc != AU_UNLOAD_BUSY) failures++;
	AuDriverUnref(d);

	rc = AuDriverUnload(d);
	UARTDebugOut("[drvtest] unload -> %d (expect 0)\r\n", rc);
	if (rc != AU_UNLOAD_OK) failures++;
	int cards1 = AuSelftestCards();
	UARTDebugOut("[drvtest] after unload: present=%d cards=%d (expect %d)\r\n", d->present, cards1, cards0 - 1);
	if (d->present || cards1 != cards0 - 1) failures++;
	if (AuGetPhysicalAddress(base0)) {
		UARTDebugOut("[drvtest] FAIL: old image still mapped\r\n");
		failures++;
	}

	rc = AuDriverLoadByName("virtsnd");
	UARTDebugOut("[drvtest] reload -> %d (expect 0)\r\n", rc);
	if (rc != 0) failures++;
	int cards2 = AuSelftestCards();
	UARTDebugOut("[drvtest] after reload: present=%d base=%x (was %x) cards=%d (expect %d)\r\n",
				 d->present, d->new_load_base, base0, cards2, cards0);
	if (!d->present || d->new_load_base != base0 || cards2 != cards0) failures++;

	UARTDebugOut("[drvtest] %s (%d failure(s))\r\n", failures ? "FAIL" : "PASS", failures);
}
#endif
