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

#include <Hal/basicacpi.h>
#include <Hal/x86_64_idt.h>
#include <Hal/ioapic.h>
#include <Hal/hal.h>
#include <string.h>
#include <Hal/x86_64_lowlevel.h>
#include <aucon.h>
#include <Mm/kmalloc.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <_null.h>
#include <pe.h>
#include <aurora.h>
#include <acpi.h>
#include <platform/acxeneva.h>

#define ACPI_POWER_BUTTON_ENABLE (1<<8)
#define ACPI_SCI_EN  (1<<0)
#define ACPI_SLP_EN  (1<<13)

AuroraBasicACPI *__AuroraBasicAcpi;
bool __PCIESupported;
void* __ACPIRSDP;
/*
 * AuACPIPowerButtonEnable -- enables the power button
 */
void AuACPIPowerButtonEnable() {
	__AuroraBasicAcpi->fadt->pm1aEventBlock = (1<<9) |  ACPI_POWER_BUTTON_ENABLE | 1<<5;
}

/*
 * AuACPIShutdown -- power off the system 
 */
void AuACPIShutdown() {
	__AuroraBasicAcpi->fadt->pm1aCtrlBlock = (__AuroraBasicAcpi->slp_typa << 10) | (1 << 13);
}

/*
 * AuACPIEnable -- Enable acpi
 */
void AuACPIEnable() {
	if (!__AuroraBasicAcpi->fadt || !__AuroraBasicAcpi->fadt->pm1aCtrlBlock) {
		AuTextOut("[ACPI]: Data structures incomplete \n");
		return;
	}

	uint16_t word = x64_inportw(__AuroraBasicAcpi->fadt->pm1aCtrlBlock);
	if ((word & ACPI_PMCTRL_SCI_EN) == 1) {
		AuTextOut("[ACPI]: Already enabled \n");
		return;
	}

	x64_outportb(__AuroraBasicAcpi->fadt->sciCmdPort, __AuroraBasicAcpi->fadt->acpiEnable);
	uint16_t word_1 = x64_inportw(__AuroraBasicAcpi->fadt->pm1aCtrlBlock);
	if ((word_1 & ACPI_PMCTRL_SCI_EN) == 1)
		AuTextOut("ACPI Enabled successfully\n");

	if (__AuroraBasicAcpi->fadt->pm1bCtrlBlock) {
		uint16_t word_3 = x64_inportw(__AuroraBasicAcpi->fadt->pm1bCtrlBlock);
		if ((word_3 & ACPI_PMCTRL_SCI_EN) == 1)
			AuTextOut("ACPI Enabled successfully\n");

	}

	AuACPIPowerButtonEnable();
}

uint8_t* search_s5(acpiDsdt* header) {
	uint32_t l;
	uint32_t* S5;

	l = header->header.length - sizeof(acpiDsdt);
	S5 = (uint32_t*)(header + sizeof(acpiDsdt));
	while (l--) {
		if (*S5 == (uint32_t)'_5S_') {
			return (uint8_t*)S5;
		}
		S5 = (uint32_t*)((uint32_t)S5 + 1);
	}

	return (uint8_t*)NULL;
}


void AuFADTHandler(size_t v, void* p) {
	AuTextOut("FADT Handler called \n");
	AuInterruptEnd(__AuroraBasicAcpi->fadt->sciInt);
}


/*
 * AuACPIParseMADT -- Parses the MADT table
 */
void AuACPIParseMADT() {
	acpiApicHeader* apic_header = (acpiApicHeader*)__AuroraBasicAcpi->madt->entry;

	while (raw_diff(apic_header, __AuroraBasicAcpi->madt) < __AuroraBasicAcpi->madt->header.length) {
		switch (apic_header->type) {
		case ACPI_APICTYPE_LAPIC: {
									  acpiLocalApic* lapic = (acpiLocalApic*)apic_header;
									  if (lapic->procId != 0)
										  __AuroraBasicAcpi->num_cpu = lapic->procId;
									 // AuTextOut("acpi cpu count -> %d \n", __AuroraBasicAcpi->num_cpu);
									  break;
		}
		case ACPI_APICTYPE_IOAPIC:{
									  acpiIoApic* io_apic = (acpiIoApic*)apic_header;
									  AuTextOut("acpi ioapic base -> %x, gsi -> %d \n", io_apic->ioApicAddr, io_apic->gsiBase);
									  break;
		}
		case ACPI_APICTYPE_ISOVER:{
									  apic_interrupt_override* over = (apic_interrupt_override*)apic_header;
									  AuTextOut("acpi interrupt source override gsi -> %d, src-> %d \n", over->interrupt, over->source);
									  break;
		}

		default:
			break;
		}

		apic_header = raw_offset<acpiApicHeader*>(apic_header, apic_header->length);
	}
}

/*
 * AuACPIInitialise -- initialise the aurora's basic acpi
 * subsystem
 * @param acpi_base -- acpi base address
 */
void AuACPIInitialise(void* acpi_base) {
	__AuroraBasicAcpi = (AuroraBasicACPI*)kmalloc(sizeof(AuroraBasicACPI));
	memset(__AuroraBasicAcpi, 0, sizeof(AuroraBasicACPI));
	__ACPIRSDP = acpi_base;
	acpiRsdp *rsdp = (acpiRsdp*)acpi_base;
	acpiRsdt* rsdt = (acpiRsdt*)rsdp->rsdtAddr;
	acpiXsdt* xsdt = (acpiXsdt*)rsdp->xsdtAddr;
	char sig[5];
	int entries = (rsdt->header.length - sizeof(rsdt->header)) / 4;
	acpiSysDescHeader* header = NULL;
	__PCIESupported = false;

	for (int count = 0; count < entries; count++) {
		header = (acpiSysDescHeader*)rsdt->entry[count];
		strncpy(sig, header->signature, 4);
		sig[4] = '\0';

		if (!strncmp(sig, ACPI_SIG_FADT, strlen(ACPI_SIG_FADT))) {
			__AuroraBasicAcpi->fadt = (acpiFadt*)header;
			AuTextOut("acpi fadt supported \n");
		}

		else if (!strncmp(sig, ACPI_SIG_APIC, strlen("CIPA"))) {
			__AuroraBasicAcpi->madt = (acpiMadt*)header;
			AuTextOut("acpi madt supported \n");
		}
		else if (!strncmp(sig, ACPI_SIG_SRAT, strlen(ACPI_SIG_SRAT))) {
			AuTextOut("acpi srat supported \n");
			/* here needs to parse the srat table for
			 * numa memory */
		}
		else if (!strncmp(sig, ACPI_SIG_SLIT, strlen(ACPI_SIG_SLIT))) {
			AuTextOut("acpi slit supported \n");
		}
		else if (!strncmp(sig, ACPI_SIG_MCFG, strlen(ACPI_SIG_MCFG))) {
			AuTextOut("acpi mcfg supported \n");
			__AuroraBasicAcpi->mcfg = (acpiMcfg*)header;
			__PCIESupported = true;
		}
		else if (!strncmp(sig, ACPI_SIG_HPET, strlen(ACPI_SIG_HPET))) {
			AuTextOut("acpi hpet supported \n");
		}
		else if (!strncmp(sig, ACPI_SIG_MCHI, strlen(ACPI_SIG_MCHI))) {
			AuTextOut("acpi management controller host interface supported\n");
		}
	}

	if (__AuroraBasicAcpi->fadt && __AuroraBasicAcpi->fadt->facsAddr) 
		__AuroraBasicAcpi->facs = (acpiFacs*)__AuroraBasicAcpi->fadt->facsAddr;

	if (__AuroraBasicAcpi->fadt && __AuroraBasicAcpi->fadt->dsdtAddr) {
		__AuroraBasicAcpi->dsdt = (acpiDsdt*)__AuroraBasicAcpi->fadt->dsdtAddr;
		AuHalRegisterIRQ(__AuroraBasicAcpi->fadt->sciInt, AuFADTHandler, __AuroraBasicAcpi->fadt->sciInt, false);

		uint8_t *S5Block = search_s5(__AuroraBasicAcpi->dsdt);
		if (S5Block) {
			S5Block += 4;
			S5Block += ((*S5Block & 0xC0) >> 6) + 2;

			if (*S5Block == 0x0A)
				S5Block++;
			__AuroraBasicAcpi->slp_typa = *S5Block;
			S5Block++;

			if(*S5Block == 0x0A)
				S5Block++;

			__AuroraBasicAcpi->slp_typb = *S5Block;

		}
	}

	if (__AuroraBasicAcpi->madt)
		AuACPIParseMADT();

	AuACPIEnable();
}

/*
 * AuACPIGetMCFG -- Returns the mcfg table
 * from basic acpi
 */
acpiMcfg *AuACPIGetMCFG() {
	return __AuroraBasicAcpi->mcfg;
}

/*
 * AuACPIPCIESupported -- Checks if pcie is
 * supported or not
 */
bool AuACPIPCIESupported() {
	return __PCIESupported;
}

/*
 * AuACPIGetRSDP -- return the 
 * rsdp pointer
 */
void* AuACPIGetRSDP() {
	return __ACPIRSDP;
}

/*
 * AuInitialiseACPISubsys -- initialise full acpica
 * subsystem
 * @param info -- boot information
 */
void AuInitialiseACPISubsys(KERNEL_BOOT_INFO *info) {
	/* get the image size and base address */
	PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)info->driver_entry1;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dos_header, dos_header->e_lfanew);
	PSECTION_HEADER secthdr = raw_offset<PSECTION_HEADER>(&ntHeaders->OptionalHeader, ntHeaders->FileHeader.SizeOfOptionaHeader);
	size_t imageBase = ntHeaders->OptionalHeader.ImageBase;
	size_t hdrsz = ntHeaders->OptionalHeader.SizeOfHeaders;

	for (size_t i = 0; i < hdrsz / 4096 + 1; i++)
		AuMapPage((uint64_t)AuPmmngrAlloc(), imageBase + i * 4096, 0);
	memcpy((void*)imageBase, info->driver_entry1, hdrsz);

	for (size_t i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
		size_t load_addr = imageBase + secthdr[i].VirtualAddress;
		size_t sectSize = secthdr[i].SizeOfRawData;
		for (int j = 0; j < sectSize / 4096 + 4; j++)
			AuMapPage((uint64_t)AuPmmngrAlloc(), load_addr + static_cast<uint64_t>(j) * 4096, NULL);

		memcpy((void*)load_addr, raw_offset<void*>(info->driver_entry1, secthdr[i].PointerToRawData), secthdr[i].SizeOfRawData);
	}

	AuKernelLinkImports((void*)imageBase);
	AuKernelLinkDLL((void*)imageBase);
	
	//ACPI_STATUS status = AcpiInitializeSubsystem();
	//if (ACPI_FAILURE(status))
	//	AuTextOut("Failed to initialise acpi subsystem \n");
	//status = AcpiLoadTables();
	//if (ACPI_FAILURE(status))
	//	AuTextOut("Failed to load acpi tables \n");
}

/*
 * AuGetCPUCount -- returns the number of cpu
 * in the system
 */
uint8_t AuGetCPUCount() {
	return __AuroraBasicAcpi->num_cpu;
}

