/**
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

#ifndef __AU_DRV_H__
#define __AU_DRV_H__

#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif
#ifdef ARCH_X64
#include <Hal/x86_64_hal.h>
#include <Hal/hal.h>
#endif
#include <Fs/vfs.h>

#define DRIVER_CLASS_AUDIO 1
#define DRIVER_CLASS_VIDEO 2
#define DRIVER_CLASS_NETWORK 3
#define DRIVER_CLASS_CONNECTIVITY_BLUETOOTH 4
#define DRIVER_CLASS_CONNECTIVITY_WIFI 5
#define DRIVER_CLASS_STORAGE  6
#define DRIVER_CLASS_USB 7
#define DRIVER_CLASS_HID 8
#define DRIVER_CLASS_UNKNOWN 9

#define DEVICE_CLASS_ETHERNET 1
#define DEVICE_CLASS_HD_AUDIO 2
#define DEVICE_CLASS_USB3     3

#define AURORA_MAX_DRIVERS  256

typedef int(*au_drv_entry)(void* drv);
typedef int(*au_drv_unload)(void* drv);

/* A driver opts in to runtime unloading by exporting `uint32_t AuDriverFlags`
 * with AU_DRV_FLAG_UNLOADABLE set AND an AuDriverUnload() that undoes every
 * kernel-visible registration it made (devices, VFS nodes, callbacks, hooks).
 * Drivers without the flag are never unloaded. */
#define AU_DRV_FLAG_UNLOADABLE  0x1

/* In a driver:  AU_DRIVER_FLAGS(AU_DRV_FLAG_UNLOADABLE);  exports AuDriverFlags */
#ifdef __GNUC__
#define AU_DRIVER_FLAGS(f) AU_EXTERN AU_EXPORT uint32_t AuDriverFlags = (f)
#else
#define AU_DRIVER_FLAGS(f) AU_EXTERN __declspec(dllexport) uint32_t AuDriverFlags = (f)
#endif


#pragma pack(push,1)
typedef struct _aurora_driver_ {
	uint8_t id;
	uint8_t drv_type;
	char name[32];
	bool present;
	uint64_t base;
	uint64_t end;
	uint16_t vendorID;
	uint16_t deviceID;
	uint8_t classCode;
	uint8_t subClassCode;
	int bus;
	int func;
	int dev;
	uint64_t device;
	au_drv_entry entry;
	au_drv_unload unload;
	uint64_t original_load_base;
	uint64_t new_load_base;
	size_t image_sz;
	/* component model: set by the driver manager, never by drivers */
	uint32_t flags;            /* AU_DRV_FLAG_* , read from the driver's AuDriverFlags export */
	int32_t  refcount;         /* >0 blocks unload */
	size_t   region_pages;     /* pages reserved in the driver region */
	void*    resources;        /* AuResource list, see aucomponent.h */
}AuDriver;
#pragma pack(pop)



#pragma pack(push,1)

typedef struct _aurora_device_ {
	uint16_t classCode;
	uint16_t subClassCode;
	uint8_t progIf;
	bool initialized;
	uint8_t aurora_dev_class;
	uint8_t aurora_driver_class;
}AuDevice;

#pragma pack(pop)


/*
* AuDrvMngrInitialize -- Initialize the driver manager
* @param info -- kernel boot info
*/
extern void AuDrvMngrInitialize(KERNEL_BOOT_INFO *info);

/*
* AuRegisterDevice -- register a new device to
* aurora system
* @param dev -- Pointer to device to add
*/
AU_EXTERN AU_EXPORT void AuRegisterDevice(AuDevice* dev);

/*
* AuCheckDevice -- checks an aurora device if it's
* already present
* @param classC -- class code of the device to check
* @param subclassC -- sub class code of the device to check
* @param progIF -- programming interface of the device
*/
AU_EXTERN AU_EXPORT bool AuCheckDevice(uint16_t classC, uint16_t subclassC, uint8_t progIF);

/*
 * AuBootDriverInitialise -- Initialise and load all boot time drivers
 * @param info -- Kernel boot information passed by XNLDR
 * [TODO] : Everything is hard coded for now
 */
AU_EXTERN AU_EXPORT void AuBootDriverInitialise(KERNEL_BOOT_INFO* info);

/*
 * AuDrvMgrGetBaseAddress -- returns the current
 * driver load base address
 */
extern uint64_t AuDrvMgrGetBaseAddress();

/*
 * AuDrvMgrSetBaseAddress -- sets a new base
 * address for driver to load
 * it's highly risky because, if we set it to
 * kernel stack location, kernel will crash
 */
extern void AuDrvMgrSetBaseAddress(uint64_t base_address);

/*
* AuGetConfEntry -- Get an entry offset in the file for required device
* @param vendor_id -- vendor id of the product
* @param device_id -- device id of the product
* @param buffer -- configuration file buffer
* @param entryoff -- entry offset from where search begins
*/
extern char* AuGetConfEntry(uint32_t vendor_id, uint32_t device_id, uint8_t* buffer, int entryoff);

/*
* AuGetDriverName -- Extract the driver path from its entry offset
* @param vendor_id -- vendor id of the product
* @param device_id -- device id of the product
* @param buffer -- configuration file buffer
* @param entryoff -- entry offset from where search begins
*/
extern AuDriver* AuGetDriverName(uint32_t vendor_id, uint32_t device_id, uint8_t* buffer, int entryoff);

/**
 * @brief AuDrvManagerCheckFault -- check if this driver contain the fault
 * address, helpful for debugging
 * @param fault_addr -- address of fault
 */
extern AuDriver* AuDrvManagerCheckFault(uint64_t fault_addr);

/**
 * @brief AuDrvCatchFault -- resolves original virtual address of the driver
 * and relocate its symbol name
 * @param drv -- Pointer to driver
 * @param fault_addr -- istruction pointer address
 */
extern void AuDrvCatchFault(AuDriver* drv, uint64_t fault_addr);

/**
 * @brief AuDrvUnloadAll -- call unload handler of all registered drivers
 */
extern void AuDrvUnloadAll();


#endif