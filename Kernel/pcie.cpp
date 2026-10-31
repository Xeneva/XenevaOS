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

#include <pcie.h>
#include <Hal/hal.h>
#include <Hal/basicacpi.h>
#include <Hal/x86_64_cpu.h>
#include <aucon.h>

/*
 * AuPCIEGetDevice -- gets a device address from its bus
 * dev and func
 * @param seg -- device segment
 * @param bus -- bus of the device
 * @param dev -- device number 
 * @param func -- function number
 */
uint64_t AuPCIEGetDevice(uint16_t seg, int bus, int dev, int func) {
	if (bus > 255)
		return 0;
	if (dev > 31)
		return 0;
	if (func > 7)
		return 0;

	uint64_t addr = 0;
	acpiMcfg* mcfg = AuACPIGetMCFG();
	acpiMcfgAlloc* allocs = mem_after<acpiMcfgAlloc*>(mcfg);
	if (allocs->startBusNum <= bus && bus <= allocs->endBusNum){
		addr = allocs->baseAddress + ((static_cast<uint64_t>(bus) - allocs->startBusNum) << 20) |
			(static_cast<uint64_t>(dev) << 15) | (static_cast<uint64_t>(func) << 12);
		return addr;
	}

	return UINT64_MAX;
}

/*
 * AuPCIERead -- reads a register from pci express
 * @param device -- device address
 * @param reg -- register to read
 * @param bus -- bus number
 * @param dev -- device number
 * @param func -- function number
 */
uint32_t AuPCIERead(uint64_t device, int reg, int bus, int dev, int func) {
	if (!AuACPIPCIESupported())
		return 0; //<- return AuPCIRead

	uint32_t address = 0;
	acpiMcfg *mcfg = AuACPIGetMCFG();
	acpiMcfgAlloc* allocs = mem_after<acpiMcfgAlloc*>(mcfg);
	if (allocs->startBusNum <= bus && bus <= allocs->endBusNum)
		address = allocs->baseAddress + ((bus - allocs->startBusNum) << 20) | (dev << 15) |
		(func << 12);

	uint64_t result = 0;
	if (address == 0)
		return 0;

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

	if (size == 1){
		result = *raw_offset<volatile uint8_t*>(device, reg);
		return result;
	}
	else if (size == 2) {
		result = *raw_offset<volatile uint16_t*>(device, reg);
		return result;
	}
	else if (size == 4) {
		result = *raw_offset<volatile uint32_t*>(device, reg);
		return result;
	}

	return UINT32_MAX;
}


/*
 * AuPCIERead64 -- reads in 64
 * @param device -- device address
 * @param reg -- register
 * @param size -- size to read
 * @param bus -- bus num
 * @param dev -- device number
 * @param func -- func number
 */
uint64_t AuPCIERead64(uint64_t device, int reg, int size, int bus, int dev, int func) {
	if (!AuACPIPCIESupported()) {
		return 0;
	}

	size_t address = 0;
	acpiMcfg *mcfg = AuACPIGetMCFG();
	acpiMcfgAlloc *allocs = mem_after<acpiMcfgAlloc*>(mcfg);
	if (allocs->startBusNum <= bus && bus <= allocs->endBusNum)
		address = allocs->baseAddress + ((bus - allocs->startBusNum) << 20) | (device << 15) | (func << 12);

	if (address == 0)
		return 0;

	uint64_t result = 0;

	if (size == 1){
		result = *raw_offset<volatile uint8_t*>(device, reg);
		return result;
	}
	else if (size == 2) {
		result = *raw_offset<volatile uint16_t*>(device, reg);
		return result;
	}
	else if (size == 4) {
		result = *raw_offset<volatile uint32_t*>(device, reg);
		return result;
	}


	return UINT64_MAX;
}


/*
* AuPCIEWrite -- writes to a register
* @param device -- device address
* @param reg -- register
* @param size -- size to read
* @param bus -- bus num
* @param dev -- device number
* @param func -- func number
*/
void AuPCIEWrite(uint64_t device, int reg, uint32_t val, int bus, int dev, int func) {
	if (!AuACPIPCIESupported()) {
		return; //pci_write(device,reg, val);
	}

	size_t address = 0;

	acpiMcfg *mcfg = AuACPIGetMCFG();
	acpiMcfgAlloc *allocs = mem_after<acpiMcfgAlloc*>(mcfg);
	if (allocs->startBusNum <= bus && bus <= allocs->endBusNum)
		address = allocs->baseAddress + ((bus - allocs->startBusNum) << 20) | (device << 15) | (func << 12);


	if (address == 0)
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

	if (size == 1){
		*raw_offset<volatile uint8_t*>(device, reg) = (uint8_t)val;
	}
	else if (size == 2) {
		*raw_offset<volatile uint16_t*>(device, reg) = (uint16_t)val;
	}
	else if (size == 4) {
		*raw_offset<volatile uint32_t*>(device, reg) = (uint32_t)val;
	}
}


// {@private use|
void AuPCIEWrite64(uint64_t device, int reg, int size, uint64_t val, int bus, int dev, int func) {
	if (!AuACPIPCIESupported()) {
		return; //pci_write(device,reg, val);
	}

	size_t address = 0;
	acpiMcfg *mcfg = AuACPIGetMCFG();
	acpiMcfgAlloc *allocs = mem_after<acpiMcfgAlloc*>(mcfg);
	if (allocs->startBusNum <= bus && bus <= allocs->endBusNum)
		address = allocs->baseAddress + ((bus - allocs->startBusNum) << 20) | (device << 15) | (func << 12) | (reg);


	if (size == 1){
		*raw_offset<volatile uint8_t*>(device, reg) = (uint8_t)val;
	}
	else if (size == 2) {
		*raw_offset<volatile uint16_t*>(device, reg) = (uint16_t)val;
	}
	else if (size == 4) {
		*raw_offset<volatile uint32_t*>(device, reg) = val;
	}
}


/*
* AuPCIEScanClass -- scans and return pcie device with given class code and sub class code
* @param classCode -- class code
* @param subClassCode -- sub class code
* @param bus -- address, where bus number will be stored
* @param dev -- address, where device number will be stored
* @param func -- address, where function number will be stored
*/
uint64_t AuPCIEScanClass(uint8_t classCode, uint8_t subClassCode, int *bus_, int *dev_, int *func_) {
	if (!AuACPIPCIESupported()) {
		return 0; //pci_scan_class(classCode, subClassCode);
	}

	acpiMcfg *mcfg = AuACPIGetMCFG();
	acpiMcfgAlloc *allocs = mem_after<acpiMcfgAlloc*>(mcfg);
	uint16_t pciSegment = 0;
	if (allocs->pciSegment <= 65535)
		pciSegment = allocs->pciSegment;
	else
		pciSegment = 0;

	for (int bus = 0; bus < allocs->endBusNum; bus++){
		for (int dev = 0; dev < PCI_DEVICE_PER_BUS; dev++) {
			for (int func = 0; func < PCI_FUNCTION_PER_DEVICE; func++) {
				uint64_t address = AuPCIEGetDevice(pciSegment, bus, dev, func);

				if (address == 0xFFFFFFFF)
					continue;
				uint8_t class_code = AuPCIERead(address, PCI_CLASS, bus, dev, func);
				uint8_t sub_ClassCode = AuPCIERead(address, PCI_SUBCLASS, bus, dev, func);
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


/*
* AuPCIEScanClassIF -- scans and return pcie device with given class code and sub class code
* @param classCode -- class code
* @param subClassCode -- sub class code
* @param bus -- address, where bus number will be stored
* @param dev -- address, where device number will be stored
* @param func -- address, where function number will be stored
* @param progIf -- Programming interface
*/
uint64_t AuPCIEScanClassIF(uint8_t classCode, uint8_t subClassCode, uint8_t progIf, int *bus_, int *dev_, int *func_) {
	if (!AuACPIPCIESupported()) {
		return 0; //pci_scan_class(classCode, subClassCode);
	}

	acpiMcfg *mcfg = AuACPIGetMCFG();
	acpiMcfgAlloc *allocs = mem_after<acpiMcfgAlloc*>(mcfg);
	uint16_t pciSegment = 0;
	if (allocs->pciSegment <= 65535)
		pciSegment = allocs->pciSegment;
	else
		pciSegment = 0;

	for (int bus = 0; bus < allocs->endBusNum; bus++){
		for (int dev = 0; dev < PCI_DEVICE_PER_BUS; dev++) {
			for (int func = 0; func < PCI_FUNCTION_PER_DEVICE; func++) {
				uint64_t address = AuPCIEGetDevice(pciSegment, bus, dev, func);

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



/*
* AuPCIEAllocMSI -- Allocate MSI/MSI-X for interrupt
* @todo -- MSIX not implemented yet
* @param device -- PCIe device address
* @param vector -- interrupt vector
* @param bus -- PCIe device bus number
* @param dev -- PCIe device dev number
* @param func -- PCIe device function number
*/
bool AuPCIEAllocMSI(uint64_t device, size_t vector, int bus, int dev, int func) {
	if (!AuACPIPCIESupported())
		return false;

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

				uint64_t msi_data = 0;

				uint64_t msi_addr = x86_64_cpu_msi_address(&msi_data, vector, 0, 1, 0);
				

				AuPCIEWrite64(device, capptr + 0x4, 4, msi_addr & UINT32_MAX, bus, dev, func);

				if (bit64_cap) {
					AuPCIEWrite64(device, capptr + 0x8, 4, msi_addr >> 32, bus, dev, func);
					AuPCIEWrite64(device, capptr + 0xC, 2, msi_data & UINT16_MAX, bus, dev, func);
				}
				else
					AuPCIEWrite64(device, capptr + 0x8, 2, msi_data & UINT16_MAX, bus, dev, func);


				if (maskcap)
					AuPCIEWrite64(device, capptr + 0x10, 4, 0, bus, dev, func);



				msctl |= 1;

				cap_reg = msi_reg & UINT16_MAX | msctl << 16;
				AuPCIEWrite64(device, capptr, 4, cap_reg & UINT32_MAX, bus, dev, func);
				uint32_t cap_reg2 = AuPCIERead64(device, capptr, 4, bus, dev, func);
				value = true; //MSI Allocated
				break;
			}

			if ((cap_reg & 0xff) == 0x11) {
				value = false; //MSI-X Allocated: not implemented
				AuTextOut("MSI-X found \r\n");
				break;
			}
			capptr = ((cap_reg >> 8) & 0xff);   //((cap_reg >> 8) & 0xFF) / 4;
		}
	}

	return value;
}