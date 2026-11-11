/**
* @file pcie.c
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
#include <string.h>
#include <stdint.h>
#include <dtb.h>
#include <aucon.h>
#include <Hal/AA64/qemu.h>
#include <Hal/AA64/gic.h>
#include <Mm/vmmngr.h>
#include <kernelAA64.h>
#include <Hal/basicacpi.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>

#define QEMU_VIRT_ECAM_BASE 0x3f000000

static uint64_t _ecamAddress;
static uint32_t _ecamBuses;	/* ECAM window size in 1 MiB bus strides, 0 = unknown */
static bool _pcieInitialized;

static int _pcie_check_and_map_dtb() {
	AuTextOut("[aurora]: pcie getting ecam from dtb \r\n");
	uint32_t* pcie = AuDeviceTreeGetNode("pcie");
	if (!pcie) {
		AuTextOut("[aurora]: pcie not found \r\n");
		return 1;
	}
	AuTextOut("[aurora]: pcie found \r\n");
#ifdef __TARGET_BOARD_QEMU_VIRT__
	/* else need to parse the DTB for */
	uint32_t addressCell = AuDeviceTreeGetAddressCells(pcie);
	uint32_t sizeCell = AuDeviceTreeGetSizeCells(pcie);
	AuTextOut("[aurora]: pcie address cell : %d, size cell : %d \n", addressCell, sizeCell);
	uint64_t ecamAddr = AuDeviceTreeGetRegAddress(pcie, 2);
	uint64_t sizeValue = AuDeviceTreeGetRegSize(pcie, 2, 2);
	AuTextOut("[aurora]: ecam address : %x, size value : %x \n", ecamAddr, sizeValue);

	/** in qemu, ecam address is 256 mb so 0x10000 number of pages */
	if (ecamAddr != 0)
		_ecamAddress = (uint64_t)AuMapMMIO(ecamAddr, 0x10000);

	/* the reg's size cell: the window the scan is allowed to walk */
	_ecamBuses = (uint32_t)(sizeValue >> 20);

	_pcieInitialized = 1;
#endif
	return 0;
}

static bool _pcie_use_hard_code_ecam() {
	AuTextOut("[aurora]: pcie getting hard code ecam address \r\n");
#ifdef __TARGET_BOARD_QEMU_VIRT__
	_ecamAddress = (uint64_t)AuMapMMIO(QEMU_VIRT_ECAM_BASE, 0x10000000 / 0x1000);
	/*
	 * mach-virt's pcie-mmcfg-mmio is a 256 MiB container, but only the
	 * first 16 MiB are aliased into the physical map (0x3f000000 to
	 * 0x3fffffff); 0x40000000 is RAM. Sixteen is the window, highmem=off
	 * being the base this hard code names.
	 */
	_ecamBuses = 0x1000000 >> 20;
#elif __TARGET_BOARD_RPI3__
	_ecamAddress = 0;
#elif __TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__
	_ecamAddress = 0;
#endif

	if (_ecamAddress != 0) {
		_pcieInitialized = 1;
		return 0;
	} else
		return 1;
}
/**
 * @brief AA64PCIeInitialize -- intialize pcie subsystem
 */
void AA64PCIeInitialize() {
	bool _use_hard_code_ = false;
	bool _use_acpi = false;

	if (_pcie_check_and_map_dtb()) {
		AuTextOut("[aurora]: could not get pcie ecam from device tree blob \r\n");
		_use_acpi = true;
	} else
		return;

	if (_use_acpi) {
		if (AuACPIPCIESupported()) {
			AuTextOut("[aurora]: ACPI PCIe is supported \n");
			acpiMcfg* mcfg = AuACPIGetMCFG();
			acpiMcfgAlloc* allocs = MEM_AFTER(acpiMcfgAlloc*, mcfg);
			//_ecamAddress = AuMapMMIO(allocs->baseAddress, 0x10000000/0x1000);
			_pcieInitialized = 1;
			return;
		}
	}

	if (_pcie_use_hard_code_ecam()) {
		AuTextOut("[aurora]: no hard coded value for PCIe \r\n");
	} else
		return;

	AuTextOut("[aurora]: kernel can't continue boot, no device discovery mechanism supported \r\n");
	AuTextOut("[aurora]: Ki koriba aru !! Eku dekhun device discovery mechanism support nokore !! "
			  "Baad diya \r\n");
	AuTextOut("[aurora]: Ponta Bhaat khuwa ge \r\n");
	_pcieInitialized = 0;
	return;
}

bool AuIsPCIeInitialized() {
	return _pcieInitialized;
}

/*
 * AuPCIEBusCount -- how many bus numbers the located ECAM window actually
 * covers, counted in the 1 MiB stride ECAM uses per bus. Zero means the
 * window is unknown -- the ACPI path sets no size -- and a caller falls
 * back to its own ceiling.
 */
int AuPCIEBusCount() {
	return (int)_ecamBuses;
}

/**
 * @brief AuPCIEGetDevice -- gets a device address from its bus
 * dev and func
 * @param seg -- device segment
 * @param bus -- bus of the device
 * @param dev -- device number
 * @param func -- function number
 * @return return the composited device address
 */
uint64_t AuPCIEGetDevice(uint16_t seg, int bus, int dev, int func) {
	if (bus > 255)
		return 0;
	if (dev > 31)
		return 0;
	if (func > 8)
		return 0;
	uint64_t addr = 0;
	if (_ecamAddress != 0) {
		addr = _ecamAddress + ((bus << 20) | (dev << 15) | (func << 12));
	} else {
		/* no ECAM from the DTB or a hard coded window, and no ACPI MCFG
		 * (Raspberry Pi 3: no PCIe at all): there is no device. Reading a
		 * NULL table here used to take low firmware memory for a bus range
		 * and base address. */
		acpiMcfg* mcfg = _pcieInitialized ? AuACPIGetMCFG() : NULL;
		if (!mcfg)
			return 0;
		acpiMcfgAlloc* allocs = MEM_AFTER(acpiMcfgAlloc*, mcfg);
		if (allocs->startBusNum <= bus && bus <= allocs->endBusNum) {
			addr = allocs->baseAddress + ((bus - allocs->startBusNum) << 20) | (dev << 15) |
				   (func << 12);
		}
	}
	return addr;
}

/**
 * @brief AuPCIERead -- reads a register from pci express
 * @param device -- device address
 * @param reg -- register to read
 * @param bus -- bus number
 * @param dev -- device number
 * @param func -- function number
 * @return return the result in DWORD
 */
uint32_t AuPCIERead(uint64_t device, int reg, int bus, int dev, int func) {
	uint64_t result = 0;
	if (!device)
		return 0xFFFFFFFF;

	int size = 0;
	switch (reg) {
	case PCI_VENDOR_ID:
		size = 2;
		break;
	case PCI_DEVICE_ID:
		size = 2;
		break;
	case PCI_COMMAND:
		size = 2;
		break;
	case PCI_STATUS:
		size = 2;
		break;
	case PCI_REVISION_ID:
		size = 1;
		break;
	case PCI_PROG_IF:
		size = 1;
		break;
	case PCI_SUBCLASS:
		size = 1;
		break;
	case PCI_CLASS:
		size = 1;
		break;
	case PCI_CACHE_LINE_SIZE:
		size = 1;
		break;
	case PCI_LATENCY_TIMER:
		size = 1;
		break;
	case PCI_HEADER_TYPE:
		size = 1;
		break;
	case PCI_BIST:
		size = 1;
		break;
	case PCI_BAR0:
		size = 4;
		break;
	case PCI_BAR1:
		size = 4;
		break;
	case PCI_BAR2:
		size = 4;
		break;
	case PCI_BAR3:
		size = 4;
		break;
	case PCI_BAR4:
		size = 4;
		break;
	case PCI_BAR5:
		size = 4;
		break;
	case PCI_CAPABILITIES_PTR:
		size = 1;
		break;
	case PCI_INTERRUPT_LINE:
		size = 1;
		break;
	case PCI_INTERRUPT_PIN:
		size = 1;
		break;
	default:
		size = 1;
		break;
	}
	if (size == 1) {
		result = *RAW_OFFSET(volatile uint8_t*, device, reg);
		return result;
	} else if (size == 2) {
		result = *RAW_OFFSET(volatile uint16_t*, device, reg);
		return result;
	} else if (size == 4) {
		result = *RAW_OFFSET(volatile uint32_t*, device, reg);
		return result;
	}

	return 0xFFFFFFFF;
}

/**
 * @brief AuPCIERead64 -- reads in 64
 * @param device -- device address
 * @param reg -- register
 * @param size -- size to read
 * @param bus -- bus num
 * @param dev -- device number
 * @param func -- func number
 * @return return the read result in QWORD
 */
uint64_t AuPCIERead64(uint64_t device, int reg, int size, int bus, int dev, int func) {
	uint64_t result = 0;
	if (!device)
		return 0xFFFFFFFFFFFFFFFF;
	if (size == 1) {
		result = *RAW_OFFSET(volatile uint8_t*, device, reg);
		return result;
	} else if (size == 2) {
		result = *RAW_OFFSET(volatile uint16_t*, device, reg);
		return result;
	} else if (size == 4) {
		result = *RAW_OFFSET(volatile uint32_t*, device, reg);
		return result;
	}

	return 0xFFFFFFFFFFFFFFFF;
}

/**
* @brief AuPCIEWrite -- writes to a register
* @param device -- device address
* @param reg -- register
* @param size -- size to read
* @param bus -- bus num
* @param dev -- device number
* @param func -- func number
*/
void AuPCIEWrite(uint64_t device, int reg, uint32_t val, int bus, int dev, int func) {
	if (!device)
		return;
	reg = reg;
	int size = 0;
	switch (reg) {
	case PCI_VENDOR_ID:
		size = 2;
		break;
	case PCI_DEVICE_ID:
		size = 2;
		break;
	case PCI_COMMAND:
		size = 2;
		break;
	case PCI_STATUS:
		size = 2;
		break;
	case PCI_REVISION_ID:
		size = 1;
		break;
	case PCI_PROG_IF:
		size = 1;
		break;
	case PCI_SUBCLASS:
		size = 1;
		break;
	case PCI_CLASS:
		size = 1;
		break;
	case PCI_CACHE_LINE_SIZE:
		size = 1;
		break;
	case PCI_LATENCY_TIMER:
		size = 1;
		break;
	case PCI_HEADER_TYPE:
		size = 1;
		break;
	case PCI_BIST:
		size = 1;
		break;
	case PCI_BAR0:
		size = 4;
		break;
	case PCI_BAR1:
		size = 4;
		break;
	case PCI_BAR2:
		size = 4;
		break;
	case PCI_BAR3:
		size = 4;
		break;
	case PCI_BAR4:
		size = 4;
		break;
	case PCI_BAR5:
		size = 4;
		break;
	case PCI_CAPABILITIES_PTR:
		size = 1;
		break;
	case PCI_INTERRUPT_LINE:
		size = 1;
		break;
	case PCI_INTERRUPT_PIN:
		size = 1;
		break;
	default:
		size = 4;
		break;
	}

	if (size == 1) {
		*RAW_OFFSET(volatile uint8_t*, device, reg) = (uint8_t)val;
	} else if (size == 2) {
		*RAW_OFFSET(volatile uint16_t*, device, reg) = (uint16_t)val;
	} else if (size == 4) {
		*RAW_OFFSET(volatile uint32_t*, device, reg) = (uint32_t)val;
	}
}

// {@private use|
void AuPCIEWrite64(uint64_t device, int reg, int size, uint64_t val, int bus, int dev, int func) {
	if (!device)
		return;
	if (size == 1) {
		*RAW_OFFSET(volatile uint8_t*, device, reg) = (uint8_t)val;
	} else if (size == 2) {
		*RAW_OFFSET(volatile uint16_t*, device, reg) = (uint16_t)val;
	} else if (size == 4) {
		*RAW_OFFSET(volatile uint32_t*, device, reg) = val;
	}
}

/**
* @brief AuPCIEScanClass -- scans and return pcie device with given class code and sub class code
* @param classCode -- class code
* @param subClassCode -- sub class code
* @param bus -- address, where bus number will be stored
* @param dev -- address, where device number will be stored
* @param func -- address, where function number will be stored
*/
uint64_t
AuPCIEScanClass(uint8_t classCode, uint8_t subClassCode, int* bus_, int* dev_, int* func_) {
	for (int bus = 0; bus < 255; bus++) {
		for (int dev = 0; dev < PCI_DEVICE_PER_BUS; dev++) {
			for (int func = 0; func < PCI_FUNCTION_PER_DEVICE; func++) {
				uint64_t address = AuPCIEGetDevice(0, bus, dev, func);
				if (address == 0)
					continue;
				if (address == 0xFFFFFFFF)
					continue;
				uint8_t class_code = AuPCIERead(address, PCI_CLASS, bus, dev, func);
				uint8_t sub_ClassCode = AuPCIERead(address, PCI_SUBCLASS, bus, dev, func);
				uint16_t vendID = AuPCIERead(address, PCI_VENDOR_ID, bus, dev, func);
				uint16_t devID = AuPCIERead(address, PCI_DEVICE_ID, bus, dev, func);
				if (classCode == 0xFF || sub_ClassCode == 0xFF)
					continue;
				if (class_code == classCode && sub_ClassCode == subClassCode) {
					*bus_ = bus;
					*dev_ = dev;
					*func_ = func;
					return address;
				}
			}
		}
	}

	return 0xFFFFFFFF;
}

/**
* @brief AuPCIEScanVendorDevice -- scans and return pcie device with given vendor id and device id
* @param vendor -- vendor id
* @param device -- device id
* @param bus -- address, where bus number will be stored
* @param dev -- address, where device number will be stored
* @param func -- address, where function number will be stored
*/
uint64_t
AuPCIEScanVendorDevice(uint16_t vendor, uint16_t device, int* bus_, int* dev_, int* func_) {
	for (int bus = 0; bus < 255; bus++) {
		for (int dev = 0; dev < PCI_DEVICE_PER_BUS; dev++) {
			for (int func = 0; func < PCI_FUNCTION_PER_DEVICE; func++) {
				uint64_t address = AuPCIEGetDevice(0, bus, dev, func);
				if (address == 0)
					continue;
				if (address == 0xFFFFFFFF)
					continue;
				uint8_t class_code = AuPCIERead(address, PCI_CLASS, bus, dev, func);
				uint8_t sub_ClassCode = AuPCIERead(address, PCI_SUBCLASS, bus, dev, func);
				uint16_t vendID = AuPCIERead(address, PCI_VENDOR_ID, bus, dev, func);
				uint16_t devID = AuPCIERead(address, PCI_DEVICE_ID, bus, dev, func);
				if (class_code == 0xFF || sub_ClassCode == 0xFF)
					continue;
				if (vendor == vendID && device == devID) {
					*bus_ = bus;
					*dev_ = dev;
					*func_ = func;
					return address;
				}
			}
		}
	}

	return 0xFFFFFFFF;
}

/**
* @brief AuPCIEScanClassIF -- scans and return pcie device with given class code and sub class code
* @param classCode -- class code
* @param subClassCode -- sub class code
* @param bus -- address, where bus number will be stored
* @param dev -- address, where device number will be stored
* @param func -- address, where function number will be stored
* @param progIf -- Programming interface
* @return pcie device address 
*/
uint64_t AuPCIEScanClassIF(
	uint8_t classCode, uint8_t subClassCode, uint8_t progIf, int* bus_, int* dev_, int* func_) {
	for (int bus = 0; bus < 255; bus++) {
		for (int dev = 0; dev < PCI_DEVICE_PER_BUS; dev++) {
			for (int func = 0; func < PCI_FUNCTION_PER_DEVICE; func++) {
				uint64_t address = AuPCIEGetDevice(0, bus, dev, func);

				if (address == 0xFFFFFFFF)
					continue;
				uint8_t class_code = AuPCIERead(address, PCI_CLASS, bus, dev, func);
				uint8_t sub_ClassCode = AuPCIERead(address, PCI_SUBCLASS, bus, dev, func);
				uint8_t prog_if = AuPCIERead(address, PCI_PROG_IF, bus, dev, func);
				if (classCode == 0xFF)
					continue;
				if (class_code == classCode && sub_ClassCode == subClassCode && prog_if == progIf) {
					*bus_ = bus;
					*dev_ = dev;
					*func_ = func;
					return address;
				}
			}
		}
	}

	return 0xFFFFFFFF;
}

#define PCI_MEM_BAR_TYPE(x) ((x >> 1) & 0x3)
#define PCI_IS_IO_BAR(x)	((x & 1) != 0)

enum PCI_MEM_BAR_TYPES { MEMBAR32, RESERVED, MEMBAR64 };
uint64_t AuPCIEReadBAR(
	uint64_t device, uint16_t bus, uint16_t dev, uint16_t func, size_t BAR, size_t* barsz) {
#define INTERNAL_PCI_REG_BAR0 0x4
	uint64_t baseaddr, highbits, headertype;
	uint64_t ret = 0;
	headertype = AuPCIERead64(device, PCI_HEADER_TYPE, 1, bus, dev, func);
	headertype >>= 16;
	headertype &= 0xFF;
	if (((headertype & 0x7F) == 1 && BAR > 1) || ((headertype & 0x7F) == 2))
		goto end;
	if (BAR > 5)
		goto end;
	baseaddr = AuPCIERead64(device, (BAR + (PCI_BAR0 / 4)) * 4, 4, bus, dev, func);
	uint32_t bartype = baseaddr & 0xF;
	if (PCI_IS_IO_BAR(bartype)) {
		ret = baseaddr;
		UARTDebugOut("PCIReadBAR: BAR TYPE is IO\n");
		goto end;
	} else if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR32)
		ret = baseaddr ^ bartype;
	else if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR64) {
		highbits = AuPCIERead64(device, (BAR + 1 + (PCI_BAR0 / 4)) * 4, 4, bus, dev, func);
		baseaddr |= (highbits << 32);
		ret = baseaddr ^ bartype;
	} else
		goto end;

	if (barsz) {
		AuPCIEWrite64(device, (BAR + (PCI_BAR0 / 4)) * 4, 4, UINT32_MAX, bus, dev, func);
		uint64_t reread = AuPCIERead64(device, (BAR + (PCI_BAR0 / 4)) * 4, 4, bus, dev, func);
		if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR64)
			AuPCIEWrite64(device, (BAR + 1 + (PCI_BAR0 / 4)) * 4, 4, UINT32_MAX, bus, dev, func);
		uint64_t szbits, szhighbits = 0;
		szbits = AuPCIERead64(device, (BAR + (PCI_BAR0 / 4)) * 4, 4, bus, dev, func);
		if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR64)
			szhighbits = AuPCIERead64(device, (BAR + 1 + (PCI_BAR0 / 4)) * 4, 4, bus, dev, func);
		AuPCIEWrite64(device, (BAR + (PCI_BAR0 / 4)) * 4, 4, baseaddr, bus, dev, func);
		if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR64) {
			AuPCIEWrite64(device, (BAR + 1 + (PCI_BAR0 / 4)) * 4, 4, highbits, bus, dev, func);
			szbits |= (szhighbits << 32);
		} else
			szbits |= ((uint64_t)UINT32_MAX << 32);
		szbits &= (UINT64_MAX - 0xF);
		szbits = ~szbits;
		++szbits;
		*barsz = szbits;
	}
end:
	return ret;
}

/**
* @brief AuPCIEAllocMSI -- Allocate MSI/MSI-X for interrupt
* @todo -- MSIX not implemented yet
* @param device -- PCIe device address
* @param vector -- interrupt vector
* @param bus -- PCIe device bus number
* @param dev -- PCIe device dev number
* @param func -- PCIe device function number
*/
bool AuPCIEAllocMSI(uint64_t device, size_t vector, int bus, int dev, int func) {
	bool value = false;
	uint64_t status = AuPCIERead64(device, PCI_COMMAND, 4, bus, dev, func);
	status >>= 16;
	if ((status & (1 << 4)) != 0) {
		uint32_t capptr = AuPCIERead64(device, PCI_CAPABILITIES_PTR, 4, bus, dev, func);
		capptr &= 0xFF;
		uint32_t cap_reg = 0;
		uint32_t msi_reg = 0;
		while (capptr != 0) {
			cap_reg = AuPCIERead64(device, capptr, 4, bus, dev, func);
			if ((cap_reg & 0xff) == 0x5) {
				msi_reg = cap_reg;
				uint16_t msctl = msi_reg >> 16;
				bool bit64_cap = (msctl & (1 << 7));
				bool maskcap = (msctl & (1 << 8));

				/* GICv2m SETSPI_NS doorbell: data must be the SPI INTID.
			 * Hard-coding 0 fires SPI0, never the allocated vector,
			 * so used.idx advances with no IRQ on our SPI. */
				uint32_t msi_data = AuGICGetMSIData((int)vector);

				uint64_t msi_addr = AuGICGetMSIAddress((int)vector);

				AuPCIEWrite64(device, capptr + 0x4, 4, msi_addr & UINT32_MAX, bus, dev, func);

				if (bit64_cap) {
					AuPCIEWrite64(device, capptr + 0x8, 4, msi_addr >> 32, bus, dev, func);
					AuPCIEWrite64(device, capptr + 0xC, 2, msi_data & UINT16_MAX, bus, dev, func);
				} else
					AuPCIEWrite64(device, capptr + 0x8, 2, msi_data & UINT16_MAX, bus, dev, func);

				if (maskcap)
					AuPCIEWrite64(device, capptr + 0x10, 4, 0, bus, dev, func);

				msctl |= 1;

				cap_reg = msi_reg & UINT16_MAX | msctl << 16;
				AuPCIEWrite64(device, capptr, 4, cap_reg & UINT32_MAX, bus, dev, func);
				uint32_t cap_reg2 = AuPCIERead64(device, capptr, 4, bus, dev, func);
				value = true; //MSI Allocated
				UARTDebugOut("MSI found \r\n");
				break;
			}

			if ((cap_reg & 0xff) == 0x11) {
				value = true; //MSI-X Allocated: not implemented
				msi_reg = cap_reg;
				uint16_t msixctl = (msi_reg >> 16) & 0xFFFF;
				uint64_t tableOffset = AuPCIERead64(device, capptr + 0x4, 4, bus, dev, func);
				uint64_t pbaOffset = AuPCIERead64(device, capptr + 0x8, 4, bus, dev, func);
				isb_flush();
				dsb_ish();
				uint8_t table_bir = tableOffset & 0x7;
				uint8_t pba_bir = pbaOffset & 0x7;
				size_t barsz = 0;
				uint64_t msi_bar = AuPCIEReadBAR(device, bus, dev, func, table_bir, &barsz);
				uint64_t tablecount = (msixctl & 0x7ff) + 1;
				uint32_t offset = tableOffset & ~0x7;
				/* The table can sit past the first BAR page (qemu-xhci
				 * puts it at 0x3000), so map through its last entry
				 * instead of faulting on the next page. */
				size_t table_pages = (offset + tablecount * 16 + 0xfff) >> 12;
				if (barsz && table_pages > (barsz >> 12))
					table_pages = barsz >> 12;
				if (!table_pages)
					table_pages = 1;
				uint64_t mappedMsiBar = (uint64_t)AuMapMMIO(msi_bar, table_pages);
				uint64_t msi_address = AuGICGetMSIAddress((int)vector);
				uint32_t msi_data = AuGICGetMSIData((int)vector);
				volatile uint32_t* msitab = RAW_OFFSET(volatile uint32_t*, mappedMsiBar, offset);
				for (int n = 0; n < tablecount; ++n) {
					msitab[0 + 4 * n] = (uint32_t)msi_address;
					msitab[1 + 4 * n] = (uint32_t)(msi_address >> 32);
					msitab[2 + 4 * n] = (uint32_t)msi_data;
					msitab[3 + 4 * n] = 0; //vector control
				}
				isb_flush();
				dsb_ish();
				msi_reg |= (1 << 31); //MSX
				msi_reg &= ~(1 << 30);
				AuPCIEWrite64(device, capptr, 4, msi_reg, bus, dev, func);
				isb_flush();
				dsb_ish();
				uint32_t msiv = AuPCIERead64(device, capptr, 4, bus, dev, func);
				uint16_t msicv = (msiv >> 16) & 0xFFFF;
				if ((msicv & (1 << 15)) != 0)
					UARTDebugOut("MSI-X Enabled \n");
				if ((msicv & (1 << 14)) != 0)
					UARTDebugOut("Function mask \n");

				break;
			}
			capptr = ((cap_reg >> 8) & 0xff); //((cap_reg >> 8) & 0xFF) / 4;
		}
	}

	return value;
}