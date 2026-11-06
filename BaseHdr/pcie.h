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

#ifndef __PCIE_H__
#define __PCIE_H__

#include <stdint.h>
#include <stddef.h>
#include <Hal/basicacpi.h>
#include <Hal/hal.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif


#define PCI_VENDOR_ID  0x00
#define PCI_DEVICE_ID  0x02
#define PCI_COMMAND    0x04
#define PCI_STATUS     0x06
#define PCI_REVISION_ID  0x08
#define PCI_PROG_IF      0x09
#define PCI_SUBCLASS     0x0a
#define PCI_CLASS   0x0b
#define PCI_CACHE_LINE_SIZE   0x0c
#define PCI_LATENCY_TIMER   0x0d
#define PCI_HEADER_TYPE  0x0e
#define PCI_BIST  0x0f
#define PCI_BAR0  0x10
#define PCI_BAR1  0x14
#define PCI_BAR2  0x18
#define PCI_BAR3  0x1C
#define PCI_BAR4  0x20
#define PCI_BAR5  0x24
#define PCI_CAPABILITIES_PTR 0x34
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN  0x3D

#define PCI_MAX_BUS 0x20
#define PCI_DEVICE_PER_BUS 32
#define PCI_FUNCTION_PER_DEVICE 8

/*
 * Whether an ECAM window was located and mapped. The DCL PCI fabric
 * (DCL/pci_shim.c) asks before it scans: on a board where no discovery
 * mechanism was found, AuPCIEGetDevice() would fall through to the
 * ACPI-MCFG path with nothing behind it, and the fabric's job is to find
 * no devices there, not to fault looking for them.
 */
bool AuIsPCIeInitialized(void);


/*
 * How many bus numbers the located ECAM window actually covers, in the
 * 1 MiB stride ECAM uses per bus; 0 when the window is unknown. The DCL
 * PCI fabric bounds its scan by this: past the window, the addresses
 * AuPCIEGetDevice() composes are not configuration space, and on a QEMU
 * virt machine a scan that runs off the end enumerates RAM contents as
 * if they were devices.
 */
extern int AuPCIEBusCount(void);


/*
* AuPCIEGetDevice -- gets a device address from its bus
* dev and func
* @param seg -- device segment
* @param bus -- bus of the device
* @param dev -- device number
* @param func -- function number
*/
extern uint64_t AuPCIEGetDevice(uint16_t seg, int bus, int dev, int func);
/*
* AuPCIEAllocMSI -- Allocate MSI/MSI-X for interrupt
* @todo -- MSIX not implemented yet
* @param device -- PCIe device address
* @param vector -- interrupt vector
* @param bus -- PCIe device bus number
* @param dev -- PCIe device dev number
* @param func -- PCIe device function number
*/
AU_EXTERN AU_EXPORT bool AuPCIEAllocMSI(uint64_t device, size_t vector, int bus, int dev, int func);

/*
* AuPCIEScanClassIF -- scans and return pcie device with given class code and sub class code
* @param classCode -- class code
* @param subClassCode -- sub class code
* @param bus -- address, where bus number will be stored
* @param dev -- address, where device number will be stored
* @param func -- address, where function number will be stored
* @param progIf -- Programming interface
*/
AU_EXTERN AU_EXPORT uint64_t AuPCIEScanClassIF(uint8_t classCode, uint8_t subClassCode, uint8_t progIf, int *bus_, int *dev_, int *func_);

/*
* AuPCIEScanVendorDevice -- scans and return pcie device with given vendor id and device id
* @param vendor -- vendor id
* @param device -- device id
* @param bus -- address, where bus number will be stored
* @param dev -- address, where device number will be stored
* @param func -- address, where function number will be stored
*/
AU_EXTERN AU_EXPORT uint64_t AuPCIEScanVendorDevice(uint16_t vendor, uint16_t device, int* bus_, int* dev_, int* func_);

/*
* AuPCIEScanClass -- scans and return pcie device with given class code and sub class code
* @param classCode -- class code
* @param subClassCode -- sub class code
* @param bus -- address, where bus number will be stored
* @param dev -- address, where device number will be stored
* @param func -- address, where function number will be stored
*/
AU_EXTERN AU_EXPORT uint64_t AuPCIEScanClass(uint8_t classCode, uint8_t subClassCode, int *bus_, int *dev_, int *func_);

/*
* AuPCIEWrite -- writes to a register
* @param device -- device address
* @param reg -- register
* @param size -- size to read
* @param bus -- bus num
* @param dev -- device number
* @param func -- func number
*/
AU_EXTERN AU_EXPORT void AuPCIEWrite(uint64_t device, int reg, uint32_t val, int bus, int dev, int func);

/*
* AuPCIERead -- reads a register from pci express
* @param device -- device address
* @param reg -- register to read
* @param bus -- bus number
* @param dev -- device number
* @param func -- function number
*/
AU_EXTERN AU_EXPORT uint32_t AuPCIERead(uint64_t device, int reg, int bus, int dev, int func);

/*
* AuPCIERead64 -- reads in 64
* @param device -- device address
* @param reg -- register
* @param size -- size to read
* @param bus -- bus num
* @param dev -- device number
* @param func -- func number
*/
AU_EXTERN AU_EXPORT uint64_t AuPCIERead64(uint64_t device, int reg, int size, int bus, int dev, int func);


// {@private use|
AU_EXTERN AU_EXPORT void AuPCIEWrite64(uint64_t device, int reg, int size, uint64_t val, int bus, int dev, int func);

/*
* AuPCIEReadBAR -- resolves a BAR index to its mapped base address (handling
* 32-bit, 64-bit and I/O BARs), optionally probing its size
* @param device -- device address
* @param bus -- bus number
* @param dev -- device number
* @param func -- function number
* @param BAR -- BAR index (0-5)
* @param barsz -- if non-null, receives the BAR's size in bytes
*/
AU_EXTERN AU_EXPORT uint64_t AuPCIEReadBAR(uint64_t device, uint16_t bus, uint16_t dev, uint16_t func, size_t BAR, size_t* barsz);
#endif