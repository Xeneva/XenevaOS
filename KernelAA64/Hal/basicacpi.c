#include <Hal/basicacpi.h>
#include <stdint.h>
#include <Mm/kmalloc.h>
#include <_null.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/gic.h>
#include <aucon.h>
#include <Drivers/uart.h>
#include <string.h>
#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif

AuroraBasicACPI* __AuroraBasicAcpi;
bool __PCIESupported;
void* __ACPIRSDP;

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

/*
 * AuACPIParseMADT -- Parses the MADT table
 */
void AuACPIParseMADT() {
	acpiApicHeader* apic_header = (acpiApicHeader*)__AuroraBasicAcpi->madt->entry;
	while (RAW_DIFF(apic_header, __AuroraBasicAcpi->madt) <
		   __AuroraBasicAcpi->madt->header.length) {
		switch (apic_header->type) {
		case ACPI_APICTYPE_LAPIC: {
			acpiLocalApic* lapic = (acpiLocalApic*)apic_header;
			if (lapic->procId != 0)
				__AuroraBasicAcpi->num_cpu = lapic->procId;
			// AuTextOut("acpi cpu count -> %d \n", __AuroraBasicAcpi->num_cpu);
			break;
		}
		case ACPI_APICTYPE_IOAPIC: {
			acpiIoApic* io_apic = (acpiIoApic*)apic_header;
			AuTextOut(
				"acpi ioapic base -> %x, gsi -> %d \n", io_apic->ioApicAddr, io_apic->gsiBase);
			break;
		}
		case ACPI_APICTYPE_ISOVER: {
			apic_interrupt_override* over = (apic_interrupt_override*)apic_header;
			AuTextOut("acpi interrupt source override gsi -> %d, src-> %d \n",
					  over->interrupt,
					  over->source);
			break;
		}

		case ACPI_MADTTYPE_GICC: {
			UARTDebugOut("GIC CPU Interface found \n");
			acpiGICCpuInterface* gicc = (acpiGICCpuInterface*)apic_header;
			UARTDebugOut("GIC CPU Interface Phys Base : %x \n", gicc->physicalBaseAddress);
			UARTDebugOut("GICR Phys Base : %x \n", gicc->gicrBaseAddress);
			GIC* gic = AuGetSystemGIC();
			gic->gicCPhys = gicc->physicalBaseAddress;
			break;
		}

		case ACPI_MADTTYPE_GICD: {
			AuTextOut("GIC Distributor found \n");
			acpiGICDistributor* gicd = (acpiGICDistributor*)apic_header;
			UARTDebugOut("GIC Distributor Phys Base : %x \n", gicd->physicalBaseAddress);
			UARTDebugOut("GIC Version : %x , GIC ID -> %d \n", gicd->gicVersion, gicd->gicID);
			GIC* gic = AuGetSystemGIC();
			gic->gicDPhys = gicd->physicalBaseAddress;
			break;
		}
		case ACPI_MADTTYPE_GICMSI: {
			AuTextOut("GIC MSI Frame found \n");
			acpiGICv2MFrame* msiframe = (acpiGICv2MFrame*)apic_header;
			UARTDebugOut("GIC MSI Frame Base : %x \n", msiframe->physicalBaseAddress);
			UARTDebugOut(
				"GIC MSI SPI Base : %d, SPICount : %d \n", msiframe->spiBase, msiframe->spiCount);
			GIC* gic = AuGetSystemGIC();
			gic->gicMSIPhys = msiframe->physicalBaseAddress;
			gic->spiBase = msiframe->spiBase;
			gic->spiCount = msiframe->spiCount;
			break;
		}

		default:
			break;
		}

		apic_header = RAW_OFFSET(acpiApicHeader*, apic_header, apic_header->length);
	}
}

#pragma pack(push, 1)
//! ACPI version 1.0 structures
typedef struct _rsdp2_ {
	char signature[8];
	unsigned char checksum;
	char oemId[6];
	unsigned char revision;
	unsigned rsdtAddr;

	unsigned length;
	uint64_t xsdtAddr;
	unsigned char xChecksum;
	unsigned char res[3];
} acpiRsdp2;
#pragma pack(pop)

/*
 * AuACPIInitialise -- initialise the aurora's basic acpi
 * subsystem
 * @param acpi_base -- acpi base address
 */
void AuACPIInitialise(void* acpi_base) {
	if (!acpi_base) {
		AuTextOut("Could not find ACPI base %x \r\n", acpi_base);
		return;
	}

	/** bypassing acpi on qemu virt **/
#ifdef __TARGET_BOARD_QEMU_VIRT__
	return;
#endif
	/*AuUpdatePageFlags((uint64_t)acpi_base, PTE_VALID | PTE_TABLE |
		PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_NORMAL_MEM);*/
	__AuroraBasicAcpi = (AuroraBasicACPI*)kmalloc(sizeof(AuroraBasicACPI));
	memset(__AuroraBasicAcpi, 0, sizeof(AuroraBasicACPI));
	__ACPIRSDP = acpi_base;
	acpiRsdp2* rsdp = (acpiRsdp2*)acpi_base;
	acpiRsdt* rsdt = (acpiRsdt*)(uintptr_t)rsdp->rsdtAddr;
	acpiXsdt* xsdt = (acpiXsdt*)rsdp->xsdtAddr;
	AuTextOut("Getting physical address of ACPIBASE \r\n");
	//AuFreePages(acpi_base, 0, 4096);
	char sig[5];
	AuTextOut("ACPI initializing -> %x\n", acpi_base);
	for (int i = 0; i < 6; i++)
		AuTextOut("%c", rsdp->oemId[i]);
	AuTextOut("ACPI xsdt -> %x \n", rsdp->xsdtAddr);
	int entries = 0;
	if (rsdt) {
		entries = (rsdt->header.length - sizeof(rsdt->header)) / 4;
	} else if (xsdt) {
		entries = (xsdt->header.length - sizeof(xsdt->header)) / sizeof(uint64_t);
	}
	acpiSysDescHeader* header = NULL;
	__PCIESupported = false;

	for (int count = 0; count < entries; count++) {
		if (rsdt) {
			header = (acpiSysDescHeader*)(uintptr_t)rsdt->entry[count];
		} else {
			header = (acpiSysDescHeader*)xsdt->entry[count];
		}

		strncpy(sig, header->signature, 4);
		sig[4] = '\0';

		if (!strncmp(sig, ACPI_SIG_FADT, strlen(ACPI_SIG_FADT))) {
			__AuroraBasicAcpi->fadt = (acpiFadt*)header;
			AuTextOut("acpi fadt supported \n");
		}

		else if (!strncmp(sig, ACPI_SIG_APIC, strlen("CIPA"))) {
			__AuroraBasicAcpi->madt = (acpiMadt*)header;
			AuTextOut("acpi madt supported \n");
		} else if (!strncmp(sig, ACPI_SIG_SRAT, strlen(ACPI_SIG_SRAT))) {
			AuTextOut("acpi srat supported \n");
			/* here needs to parse the srat table for
			 * numa memory */
		} else if (!strncmp(sig, ACPI_SIG_SLIT, strlen(ACPI_SIG_SLIT))) {
			AuTextOut("acpi slit supported \n");
		} else if (!strncmp(sig, ACPI_SIG_MCFG, strlen(ACPI_SIG_MCFG))) {
			AuTextOut("acpi mcfg supported \n");
			__AuroraBasicAcpi->mcfg = (acpiMcfg*)header;
			__PCIESupported = true;
		} else if (!strncmp(sig, ACPI_SIG_HPET, strlen(ACPI_SIG_HPET))) {
			AuTextOut("acpi hpet supported \n");
		} else if (!strncmp(sig, ACPI_SIG_MCHI, strlen(ACPI_SIG_MCHI))) {
			AuTextOut("acpi management controller host interface supported\n");
		}
	}

	if (__AuroraBasicAcpi->fadt && __AuroraBasicAcpi->fadt->facsAddr)
		__AuroraBasicAcpi->facs = (acpiFacs*)(uintptr_t)__AuroraBasicAcpi->fadt->facsAddr;

	if (__AuroraBasicAcpi->fadt && __AuroraBasicAcpi->fadt->dsdtAddr) {
		__AuroraBasicAcpi->dsdt = (acpiDsdt*)(uintptr_t)__AuroraBasicAcpi->fadt->dsdtAddr;
		//AuHalRegisterIRQ(__AuroraBasicAcpi->fadt->sciInt, AuFADTHandler, __AuroraBasicAcpi->fadt->sciInt, false);

		uint8_t* S5Block = search_s5(__AuroraBasicAcpi->dsdt);
		if (S5Block) {
			S5Block += 4;
			S5Block += ((*S5Block & 0xC0) >> 6) + 2;

			if (*S5Block == 0x0A)
				S5Block++;
			__AuroraBasicAcpi->slp_typa = *S5Block;
			S5Block++;

			if (*S5Block == 0x0A)
				S5Block++;

			__AuroraBasicAcpi->slp_typb = *S5Block;
		}
	}

	if (__AuroraBasicAcpi->madt) {
		AuTextOut("ACPI Madt : %x \n", __AuroraBasicAcpi->madt);
		AuACPIParseMADT();
	}
}

/**
 * AuACPIGetMCFG -- Returns the mcfg table
 * from basic acpi
 */
acpiMcfg* AuACPIGetMCFG() {
	if (!__AuroraBasicAcpi)
		return NULL;
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