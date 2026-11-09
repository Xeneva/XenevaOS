/**
* @file gic.c
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

#include <Hal/AA64/gic.h>
#include <aucon.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <process.h>
#include <dtb.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64percpu.h>
#include <Board/RPI3bp/rpi3bp.h>

GIC __gic;
volatile uint32_t* gic_regs;
volatile uint32_t* gicc_regs;

typedef void (*irq_callback)(int spi);

/** distributor registers */
#define GICD(n)				  (n.gicDMMIO)
#define GICD_CTLR			  0x0000
#define GICD_TYPER			  0x0004
#define GICD_IIDR			  0x0008
#define GICD_TYPER2			  0x000C
#define GICD_PIDR2			  0xFFE8
#define GICD_PIDR2_ARCHREV(x) (((x) >> 4) & 0xF)
#define GIC_VERSION_1		  0x1
#define GIC_VERSION_2		  0x2
#define GIC_VERSION_3		  0x3
#define GIC_VERSION_4		  0x4
#define GICD_ISENABLER(n)	  (*(volatile uint32_t*)(GICD(__gic) + 0x100 + 4 * (n)))
#define GICD_IPRIORITYR(n)	  (0x400 + (n))
#define GICD_IGROUPR(n)		  (*(volatile uint32_t*)(GICD(__gic) + 0x080 + 4 * (n)))
#define GICD_ICENABLE(n)	  (*(volatile uint32_t*)(GICD(__gic) + 0x180 + (n * 4)))
#define GICD_ICFGR(n)		  (*(volatile uint32_t*)(GICD(__gic) + 0x0C00 + (n * 4)))
#define GICD_ITARGETSR(n)	  (*(volatile uint32_t*)(GICD(__gic) + 0x0800 + (n * 4)))
#define GICD_IROUTER(n)		  (0x6000 + 8 * (n))
#define GICD_ICPENDR(n)		  (0x280 + (n / 32) * 4)
#define ISPENDING0			  0x200

/** cpu registers offsets */
#define GICC_CTLR  0x0000
#define GICC_PMR   0x0004
#define GICC_BPR   0x0008
#define GICC_IAR   0x000C
#define GICC_EOIR  0x0010
#define GICC_HPPIR 0x0018
#define GICC_ABPR  0x001C
#define GICC_IIDR  0x00FC
#define GICC(n)	   (n.gicCMMIO)

#define GICR_CTLR		   0x0000
#define GICR_IIDR		   0x0004
#define GICR_WAKER		   0x0014
#define GICR_TYPER		   0x008
#define GICR_STRIDE		   0x20000UL
#define GICR_IGROUPR0	   0x10080
#define GICR_ISENABLER0	   0x10100
#define GICR_ICENABLER0	   0x10180
#define GICR_IPRIORITYR(n) (0x10400 + (n) * 4)
#define GICR_SGI_BASE	   0x10000

#define GICR_WAKER_PS (1u << 1)
#define GICR_WAKER_CA (1u << 2)
#define GICR_CTLR_RWP (1u << 3)

#define GICR(n) (n.gicRMMIO)

/* GICR_TYPER[63:32] is the frame's Aff3.Aff2.Aff1.Aff0, bit 4 marks the
 * last frame of the redistributor region */
#define GICR_TYPER_LAST	 (1ULL << 4)
#define GICR_MAX_FRAMES	 AA64_SMP_MAX_CPUS
#define GICR_FRAME_PAGES (GICR_STRIDE / PAGE_SIZE)

/* redistributor RD_base of each logical cpu, filled by gicrInitFrame */
static uint64_t _gicr_cpu_base[AA64_SMP_MAX_CPUS];

uint64_t* gic_dist_mmio;
uint64_t* gic_redist_mmio;

#define MAX_SPIS 1024

static uint8_t spiBitMap[MAX_SPIS];
static irq_callback callbacks[MAX_SPIS];

/* forward: GICv2 SPIs need an explicit CPU target, see GICEnableSPIIRQ */
void GICSetTargetCPU(int spi);
static void gicRecordTargetMask(void);

/**
 * @brief gic_outqw -- writes a value to mmio registers in qword
 * @param reg -- register
 * @param value -- value to write
 */
void gic_outqw(uint64_t* mmio_, int reg, uint64_t value) {
	volatile uint64_t* mmio = (volatile uint64_t*)((uint64_t)mmio_ + reg);
	*mmio = value;
}
/**
 * @brief gic_outl_ -- writes a value to mmio register in dword
 * @param reg -- register
 * @param value -- value to write
 */
void gic_outl_(uint64_t* mmio_, int reg, uint32_t value) {
	volatile uint32_t* mmio = (volatile uint32_t*)((uint64_t)mmio_ + reg);
	*mmio = value;
}

/**
 * @brief gic_inl_ -- reads a value from mmio register in dword
 * @param reg -- register
 * @return value read from mmio address in DWORD
 */
uint32_t gic_inl_(uint64_t* mmio_, int reg) {
	volatile uint32_t* mmio = (volatile uint32_t*)((uint64_t)mmio_ + reg);
	return *mmio;
}

/**
 * @brief gic_outw_ -- writes a value to mmio register in word
 * @param reg -- register
 * @param value -- value to write
 */
void gic_outw_(uint64_t* mmio_, int reg, uint16_t value) {
	volatile uint16_t* mmio = (volatile uint16_t*)(mmio_ + reg);
	*mmio = value;
}

/**
 * @brief gic_inw_ -- reads a value from mmio register in word
 * @param reg -- register
 * @return value read from the mmio address in WORD 
 */
uint16_t gic_inw_(uint64_t* mmio_, int reg) {
	volatile uint16_t* mmio = (volatile uint16_t*)(mmio_ + reg);
	return *mmio;
}

/**
 * @brief gic_outb_ -- writes a value to mmio register in byte
 * @param reg -- register
 * @param value -- value to write
 */
void gic_outb_(uint64_t* mmio_, int reg, uint8_t value) {
	volatile uint8_t* mmio = (volatile uint8_t*)((uint64_t)mmio_ + reg);
	*mmio = value;
}

/**
* @brief gic_inb_ -- reads a value from mmio register in byte
* @param reg -- register
* @return value read from mmio address in BYTE
*/
uint8_t gic_inb_(uint64_t* mmio_, int reg) {
	volatile uint8_t* mmio = (volatile uint8_t*)((uint64_t)mmio_ + reg);
	return *mmio;
}

GIC* AuGetSystemGIC() {
	return &__gic;
}

#define SET_SPI_NS 0x040
#define SET_SPI_S  0x080
/**
 * @brief AuGICGetMSIAddress -- calculate and return MSI address
 * for given spi offset
 * @param interruptID -- spi offset
 * @return composited MSI address
 */
uint64_t AuGICGetMSIAddress(int interruptID) {
	if (__gic.gicMSIPhys == 0)
		return 0;
	UARTDebugOut("MSI Address : %x \n", __gic.gicMSIPhys + 0x040);
	return (__gic.gicMSIPhys + SET_SPI_NS);
}

uint32_t AuGICGetMSIData(int interruptID) {
	UARTDebugOut("GICGetMSIData: %d \n", interruptID);
	return interruptID;
}

/**
 * @brief AuGICAllocateSPI -- allocates Shared Peripheral 
 * interrupt ID 
 */
int AuGICAllocateSPI() {
	for (uint32_t i = 0; i < __gic.spiCount; i++) {
		if (spiBitMap[i] == 0) {
			spiBitMap[i] = 1;
			UARTDebugOut("[GIC]: Allocating SPI : %d , SPI_BASE : %d \n", i, __gic.spiBase);
			return __gic.spiBase + i;
		}
	}
	return -1;
}

/**
 *@brief AuGICDeallocateSPI -- free up an used SPI id 
 *@param spiID -- target spi id
 */
void AuGICDeallocateSPI(int spiID) {
	if (spiID < __gic.spiBase || spiID >= (__gic.spiBase + __gic.spiCount))
		return;
	uint32_t index = spiID - __gic.spiBase;
	spiBitMap[index] = 0;
}

#define GICV2M_MSI_TYPER	 0x008
#define GICV2M_MSI_SETSPI_NS 0x040
#define GICV2M_MSI_IIDR		 0xFCC
void GICv2MInitialize() {
	uint32_t typer = *(volatile uint32_t*)__gic.gicMSIPhys;
}

/**
 * @brief GICVerifyVersion -- getting the version detail
 * inorder to control initialization sequence
 */
uint32_t GICVerifyVersion() {
#ifdef __TARGET_BOARD_QEMU_VIRT__
	return GIC_VERSION_2;
#else
	uint32_t pidr2 = gic_inl_(GICD(__gic), GICD_PIDR2);
	uint32_t archrev = GICD_PIDR2_ARCHREV(pidr2);
	if (archrev == GIC_VERSION_3)
		UARTDebugOut("[aurora]: using GICv3 \r\n");
	else if (archrev == GIC_VERSION_4)
		UARTDebugOut("[aurora]: using GICv4 \r\n");
	else if (archrev == GIC_VERSION_2)
		UARTDebugOut("[aurora]: using GICv2 \r\n");
	else if (archrev == GIC_VERSION_1)
		UARTDebugOut("[aurora]: using GICv1 \r\n");
	return archrev;
#endif
}

/**
 * @brief gicrFindFrame -- find the redistributor frame that belongs to
 * the core with the given affinity
 * @return RD_base of the frame, 0 if no frame matches
 */
static uint64_t gicrFindFrame(uint64_t mpidr) {
	uint32_t want = (uint32_t)(((mpidr >> 8) & 0xFF000000ULL) | (mpidr & 0xFFFFFFULL));
	for (uint32_t i = 0; i < GICR_MAX_FRAMES; i++) {
		uint64_t frame = GICR(__gic) + (uint64_t)i * GICR_STRIDE;
		uint64_t typer = *(volatile uint64_t*)(frame + GICR_TYPER);
		if ((uint32_t)(typer >> 32) == want)
			return frame;
		if (typer & GICR_TYPER_LAST)
			break;
	}
	return 0;
}

/**
 * @brief gicrInitFrame -- wake and configure the calling core's
 * redistributor, then its ICC system register interface
 * @param cpu_num -- logical number of the calling core
 * @param mpidr -- affinity of the calling core
 * @param verbose -- print progress (boot core only, the console is not
 * safe to use from several cores yet)
 */
static void gicrInitFrame(uint32_t cpu_num, uint64_t mpidr, bool verbose) {
	uint64_t frame = gicrFindFrame(mpidr);
	if (!frame) {
		/* no affinity match, fall back to frame order */
		frame = GICR(__gic) + (uint64_t)cpu_num * GICR_STRIDE;
	}
	_gicr_cpu_base[cpu_num] = frame;
	uint64_t* gicr = (uint64_t*)frame;
	uint64_t* sgi = (uint64_t*)(frame + GICR_SGI_BASE);

	if (verbose) {
		uint64_t gicr_typer = gic_inl_(gicr, GICR_TYPER);
		uint32_t proc_num = (uint32_t)((gicr_typer >> 8) & 0xFFFF);
		AuTextOut("GICR : %x , cpu num : %d \r\n", gicr, proc_num);
	}
	uint32_t waker = gic_inl_(gicr, GICR_WAKER);
	waker &= ~GICR_WAKER_PS;
	gic_outl_(gicr, GICR_WAKER, waker);
	while (gic_inl_(gicr, GICR_WAKER) & GICR_WAKER_CA)
		;

	gic_outl_(gicr, GICR_IGROUPR0, 0xFFFFFFFF);

	gic_outl_(sgi, 0x0D00, 0x00000000);

	gic_outl_(gicr, GICR_ICENABLER0, 0xFFFF0000);

	while (gic_inl_(gicr, GICR_CTLR) & GICR_CTLR_RWP)
		;

	/* GICR_IPRIORITYR already includes the SGI frame offset, so it is
	 * relative to RD_base, not to sgi (that landed in the next frame) */
	for (int i = 0; i < 8; i++)
		gic_outl_(gicr, GICR_IPRIORITYR(i), 0xA0A0A0A0);

	/** initialize the cpu interface **/
	if (gic_cpu_init() && verbose)
		AuTextOut("Failed to initialize GIC Cpu interface \r\n");
}

/**
 * @brief GICRInitialize -- initialize GIC redistributor
 * @param cpu_num -- desired cpu core number
 */
void GICRInitialize(int cpu_num) {
	AuTextOut("Initializing GICR \r\n");
	gicrInitFrame((uint32_t)cpu_num, read_mpidr_el1() & AA64_MPIDR_AFF_MASK, true);
	AuTextOut("GIC-R initialized \r\n");
}

/**
 * @brief GICInitializeCPU -- bring up the calling core's GIC CPU interface
 * (GICv2) or its redistributor and system register interface (GICv3).
 * The boot core does this inside GICInitialize, secondary cores call it
 * from AA64SMPAPEntry.
 * @param cpu_num -- logical number of the calling core
 * @param mpidr -- affinity of the calling core
 */
void GICInitializeCPU(uint32_t cpu_num, uint64_t mpidr) {
#ifdef __TARGET_BOARD_RPI3__
	(void)cpu_num;
	(void)mpidr;
#else
	if (cpu_num >= AA64_SMP_MAX_CPUS)
		return;
	if (__gic.version >= GIC_VERSION_3) {
		gicrInitFrame(cpu_num, mpidr, false);
		return;
	}
	if (!__gic.gicCMMIO)
		return;
	/* GICC and the SGI/PPI half of GICD are banked per core */
	gic_outl_((uint64_t*)__gic.gicCMMIO, GICC_PMR, 0xFF);
	gic_outl_((uint64_t*)__gic.gicCMMIO, GICC_CTLR, 0x3);
	dsb_sy_barrier();
	isb_flush();
	gicRecordTargetMask();
#endif
}

/**
 * @brief GICInitialize -- initialize the gic 
 * controller, it can be parsed through ACPI MADT 
 * or hard-coding
 */
void GICInitialize() {
	/* Firstly rely upon UEFI + ACPI for GIC mmio values */
	AuTextOut("Initializing GIC %x %x \r\n", __gic.gicCPhys, __gic.gicDPhys);

#ifdef __TARGET_BOARD_QEMU_VIRT__
	__gic.gicCPhys = 0x08010000ULL;
	__gic.gicDPhys = 0x08000000ULL;
	__gic.gicMSIPhys = 0x08020000ULL;
	__gic.gicRPhys = 0x080A0000ULL;
	__gic.spiBase = 80;
	__gic.spiCount = 64;
#endif

	/* if not found, go for device tree , because the kernel might get booted
	 * from LittleBoot 
	 */
	if (__gic.gicCPhys == 0 && __gic.gicDPhys == 0) {
#ifdef __TARGET_BOARD_RPI3__
		AuTextOut("[aurora]: Raspberry Pi 3b+ doesn't use traditional GIC \r\n");
		/* Initialize BCM2836-ARMCTL-IC */
		AuRPI3ICInit();
		AuTextOut("[aurora]: Raspberry Pi 3b+ Interrupt Controller initialized \r\n");
		return;
#endif
		//Need to fall to Device Tree
		uint32_t* ic = AuDeviceTreeGetNode("ic@");
		if (!ic) {
			AuTextOut("Interrupt controller node not found \r\n");
			ic = AuDeviceTreeGetNode("interrupt-controller@");
			if (!ic) {
				AuTextOut("Interrupt controller node not found on second fallback \r\n");
				return;
			}
		}

		uint32_t* compat = AuDeviceTreeFindProperty(ic, "compatible");
		fdt_property_t* prop = (fdt_property_t*)compat;
		if (compat) {
			AuTextOut("Interrupt controller found: %s \r\n", prop->value);
		}
		uint32_t* addressSz = AuDeviceTreeFindProperty(ic, "#address-cells");
		uint32_t* reg = AuDeviceTreeFindProperty(ic, "reg");
		prop = (fdt_property_t*)reg;
		if (reg) {
			/* suspecting, this would only work with QEMU-aarch64-virt board */
			uint32_t gicd = (uint64_t)AuDTBSwap32(reg[3]) | ((uint64_t)AuDTBSwap32(reg[4]) << 32UL);
			uint32_t gicc = (uint64_t)AuDTBSwap32(reg[7]) | ((uint64_t)AuDTBSwap32(reg[9]) << 32UL);
			uint32_t addrS = AuDeviceTreeGetAddressCells(ic);
			uint32_t sizeS = AuDeviceTreeGetSizeCells(ic);
			AuTextOut("GIC ADr: %x \r\n", AuDeviceTreeGetRegAddress(ic, addrS));
			AuTextOut("GIC Szs: %x \r\n", AuDeviceTreeGetRegSize(ic, addrS, sizeS));
			AuTextOut("GICD : %x, GICC : %x \r\n", gicd, gicc);
			AuTextOut("GIC Addrsz : %d, sizesz : %d \r\n", addrS, sizeS);

			/**
			 * @TODO : we need to rely on DTB, here we're doing it manually according
			 * to SOC_TARGET build
			 */
			__gic.gicDPhys = GIC_DIST; // gicd;
			__gic.gicCPhys = gicc;
			__gic.gicRPhys = GIC_REDIST;
		}
	}
skip_:

	if (__gic.gicCPhys == 0 && __gic.gicDPhys == 0) {
		AuTextOut("[aurora]: No GIC MMIO found \r\n");
		return;
	}

	__gic.gicDMMIO = (uint64_t)AuMapMMIO(__gic.gicDPhys, 16);

	uint32_t version = GICVerifyVersion();
	__gic.version = version;
	bool _need_cpu_interface = true;
	if (version >= GIC_VERSION_3)
		_need_cpu_interface = false;

	if (_need_cpu_interface)
		__gic.gicCMMIO = (uint64_t)AuMapMMIO(__gic.gicCPhys, 2);

	if (__gic.gicMSIPhys)
		__gic.gicMSIMMIO = (uint64_t)AuMapMMIO(__gic.gicMSIPhys, 2);

	/** map Redist to MMIO, one redistributor frame is 128 KiB,
	 * map one frame per possible cpu (AA64_SMP_MAX_CPUS)
	 */
	if (version >= GIC_VERSION_3)
		__gic.gicRMMIO =
			(uint64_t)AuMapMMIO(__gic.gicRPhys, GICR_MAX_FRAMES * GICR_FRAME_PAGES);

	gic_regs = (volatile uint32_t*)__gic.gicDMMIO;
	gicc_regs = (volatile uint32_t*)__gic.gicCMMIO;

	uint32_t typer = gic_inl_((uint64_t*)GICD(__gic), GICD_TYPER);
	uint32_t numirq = ((typer & 0x1f) + 1) * 32;
	AuTextOut("GIC Number of supported IRQ -> %d \r\n", numirq);

	uint32_t icc_iidr = gic_inl_((uint64_t*)GICD(__gic), GICD_IIDR);
	uint32_t imp = (icc_iidr & 0xFFF);
	uint32_t rev = (icc_iidr >> 16) & 0xFF;

	for (int i = 0; i < (numirq / 32); i++)
		GICD_ICENABLE(i) = 0xFFFFFFFF;

	uint32_t val = gic_inl_((uint64_t*)GICD(__gic), GICD_CTLR);
	if ((val & 1))
		AuTextOut("GIC already enabled \r\n");

	AuTextOut("[aurora]: Implementer : %x \r\n", imp);
	AuTextOut("[aurora]: Revision : %x \r\n", rev);
	/* enable the cpu interface*/
	/* writing 0xff means accepting all types of priority 0x0 -- 0xFF */
	if (_need_cpu_interface) {
		gic_outl_((uint64_t*)__gic.gicCMMIO, GICC_PMR, 0x1ff);
		gic_outl_((uint64_t*)__gic.gicCMMIO, GICC_CTLR, 0x3);
		isb_flush();
		gicRecordTargetMask();
	}

	/* enable the distributor interface */
	if (__gic.version >= GIC_VERSION_3) {
		gic_outl_((uint64_t*)__gic.gicDMMIO, GICD_CTLR, (1u << 5) | (1u << 1));
	} else {
		gic_outl_((uint64_t*)__gic.gicDMMIO, GICD_CTLR, 0x3);
		isb_flush();
	}

	for (int i = 0; i < MAX_SPIS; i++)
		spiBitMap[i] = 0;

	for (int i = 0; i < MAX_SPIS; i++)
		callbacks[i] = 0;

	if (version >= GIC_VERSION_3)
		GICRInitialize(0);

	AuTextOut("GIC Initialized \r\n");
}

/* GICv2: banked GICD_ITARGETSR0 reads back the calling core's own CPU
 * interface bit, which is what GICD_SGIR wants as target */
static void gicRecordTargetMask(void) {
	AA64PerCPU* p = AA64GetPerCPU();
	if (!p || __gic.version >= GIC_VERSION_3)
		return;
	p->gic_target_mask = *(volatile uint8_t*)(GICD(__gic) + 0x800);
}

/* RD_base of a logical cpu, falling back to frame order before
 * gicrInitFrame ran for it */
static uint64_t gicrCPUBase(uint32_t cpu) {
	if (cpu < AA64_SMP_MAX_CPUS && _gicr_cpu_base[cpu])
		return _gicr_cpu_base[cpu];
	return GICR(__gic) + (uint64_t)cpu * GICR_STRIDE;
}

void GICREnablePPI(uint32_t cpu, uint32_t intid) {
	size_t sgi = gicrCPUBase(cpu) + GICR_SGI_BASE;
	gic_outl_((uint64_t*)sgi, 0x100, (1u << intid));
}

void GICRSetPPIPriority(uint32_t cpu, uint32_t intid, uint8_t prio) {
	size_t sgi = gicrCPUBase(cpu) + GICR_SGI_BASE;
	uint32_t reg = intid / 4;
	uint32_t shift = (intid % 4) * 8;
	uint32_t val = gic_inl_((uint64_t*)sgi, 0x400 + reg * 4);
	val &= ~(0xFFu << shift);
	val |= ((uint32_t)prio << shift);
	gic_outl_((uint64_t*)sgi, 0x400 + reg * 4, val);
}

/**
 * @brief GICEnablePPILocal -- enable an SGI/PPI on the calling core only
 * @param irq -- interrupt id, below 32
 */
void GICEnablePPILocal(uint32_t irq) {
	if (irq >= 32)
		return;
	if (__gic.version >= GIC_VERSION_3) {
		uint32_t cpu = AA64GetCPUNum();
		GICREnablePPI(cpu, irq);
		GICRSetPPIPriority(cpu, irq, 0xA0);
		return;
	}
	/* GICv2: IPRIORITYR0-7 and ISENABLER0 are banked per core */
	*(volatile uint8_t*)(GICD(__gic) + GICD_IPRIORITYR(irq)) = 0xA0;
	GICD_ISENABLER(0) = (1u << irq);
	dsb_sy_barrier();
	isb_flush();
}

/** @briefGICEnableIRQ -- enable an IRQ
 * @param irq -- IRQ number
 */
void GICEnableIRQ(uint32_t irq) {
	if (__gic.version >= GIC_VERSION_3) {
		if (irq < 32) {
			gic_outl_((uint64_t*)GICD(__gic), 0x6000 + irq * 8, 0ULL);
			GICREnablePPI(0, irq);
			GICRSetPPIPriority(0, irq, 0xA0);
		}
	} else {
		uint32_t reg = irq / 32;
		uint32_t bit = irq % 32;
		//GICClearPendingIRQ(irq);
		uint32_t icfgr = GICD_ICFGR(irq / 16);
		uint32_t shift = (irq % 16) * 2;
		uint32_t config_bits = (icfgr >> shift) & 0b11;
		if (config_bits == 0b00) {
			UARTDebugOut("IRQ : %d is level-sensitive \r\n", irq);
		} else if (config_bits == 0b10) {
			UARTDebugOut("IRQ : %d is edge-trigggered \r\n", irq);
		} else {
			UARTDebugOut("IRQ: %d is reserved or invalid config : %x \r\n", irq, config_bits);
		}
		//GICD_IGROUPR(reg) |= (1u << bit);
		/*uint8_t* gicd_itargetsr = (uint8_t*)GICD_ITARGETSR(irq);
		*gicd_itargetsr = 0x01;*/
		UARTDebugOut("IGROUP : %d \r\n", GICD_IGROUPR(reg));
		*(volatile uint8_t*)(GICD(__gic) + GICD_IPRIORITYR(irq)) = 0xA0;
		GICD_ISENABLER(reg) |= (1u << bit);
	}
}

void GICSetEdgeTriggered(uint32_t irq) {
	uint32_t reg = irq / 32;
	uint32_t bit = irq % 32;
	volatile uint32_t* icfgr = &GICD_ICFGR(irq / 16);
	uint32_t shift = (irq % 16) * 2;

	uint32_t unsetLevelMask = ~(((uint32_t)0b01) << shift);
	uint32_t setEdgeMask = ((uint32_t)0b10) << shift;

	*icfgr &= unsetLevelMask;
	*icfgr |= setEdgeMask;
}

void GICIsIRQEdgeTriggered(uint32_t irq) {
	uint32_t icfgr = GICD_ICFGR(irq / 16);
	uint32_t shift = (irq % 16) * 2;
	uint32_t config_bits = (icfgr >> shift) & 0b11;
	if (config_bits == 0b00) {
		UARTDebugOut("IRQ : %d is level-sensitive \r\n", irq);
	} else if (config_bits == 0b10) {
		UARTDebugOut("IRQ : %d is edge-trigggered \r\n", irq);
	} else {
		UARTDebugOut("IRQ: %d is reserved or invalid config : %x \r\n", irq, config_bits);
	}
	UARTDebugOut("ICFGR : %x \r\n", icfgr);
}

/** @brief GICEnableIRQ -- enable an IRQ
 * @param irq -- IRQ number
 */
void GICEnableSPIIRQ(uint32_t irq) {
	//if (__gic.version >= GIC_VERSION_3) {
	if (irq < 32) {
		gic_outl_((uint64_t*)GICD(__gic), 0x6000 + irq * 8, 0ULL);
		GICREnablePPI(0, irq);
		GICRSetPPIPriority(0, irq, 0xA0);
	} else {
		uint32_t reg = irq / 32;
		uint32_t bit = irq % 32;
		/* Program a disabled IRQ: distributor forwards only when target,
		 * priority, trigger and group are all valid. Order matters --
		 * disable/clear first, enable last.
		 * QEMU GICv2 ignores ITARGETSR/ICFGR/IPRIORITYR writes while
		 * GICD_CTLR.Enable is set, so drop the distributor around the
		 * config and restore it before ISENABLER. */
		GICD_ICENABLE(reg) = (1u << bit);
		dsb_sy_barrier();
		GICClearPendingIRQ(irq);
		uint32_t saved_ctlr = gic_inl_((uint64_t*)GICD(__gic), GICD_CTLR);
		gic_outl_((uint64_t*)GICD(__gic), GICD_CTLR, saved_ctlr & ~0x3u);
		dsb_sy_barrier();
		isb_flush();
		/* Leave the SPI in Group0: this kernel runs Secure and Secure
		 * IAR reads of Group1 without AckCtl return 1022 (spurious
		 * storm, no delivery). Do NOT move SPIs to Group1. */
		GICSetEdgeTriggered(irq);

		if (__gic.version >= GIC_VERSION_3) {
			/** route it to cpu0 **/
			gic_outqw((uint64_t*)GICD(__gic), 0x6000 + irq * 8, 0ULL);
		} else {
			/* GICv2 SPIs power up with no CPU target: without this
			 * the distributor never forwards them to the CPU IF,
			 * so IAR never reports the SPI even though the device
			 * raises it (used.idx advances, no IRQ). --axiss */
			GICSetTargetCPU((int)irq);
		}

		*(volatile uint8_t*)(GICD(__gic) + GICD_IPRIORITYR(irq)) = 0x80;
		dsb_sy_barrier();
		isb_flush();
		gic_outl_((uint64_t*)GICD(__gic), GICD_CTLR, saved_ctlr);
		dsb_sy_barrier();
		isb_flush();
		GICD_ISENABLER(reg) = (1u << bit);
		dsb_sy_barrier();
		isb_flush();
	}
}

void GICSetTargetCPU(int spi) {
	if (__gic.version >= GIC_VERSION_3)
		return;
	if (spi < 32)
		return;

	/* ITARGETSR is byte-accessible: one byte per INTID at 0x800+INTID.
	 * The old word RMW (0xFF << (byte*8)) is signed-overflow UB for
	 * byte 3 and races with concurrent distributor updates. A single
	 * byte store to CPU0 mask (0x01) is exact and readback-verifiable.
	 * Must program while the SPI is disabled; the caller
	 * (GICEnableSPIIRQ) disables first, but double-ensure here.
	 * NOTE: on uniprocessor QEMU GICv2 this register is RAZ/WI by
	 * design (internal target is hardwired to CPU0, reads 0) -- a 0
	 * readback is NORMAL, not an error. Keep the write for SMP. --axiss */
	uint32_t reg = (uint32_t)spi / 32u;
	uint32_t bit = (uint32_t)spi % 32u;
	GICD_ICENABLE(reg) = (1u << bit);
	dsb_sy_barrier();
	*(volatile uint8_t*)(GICD(__gic) + 0x0800u + (uint32_t)spi) = 0x01u;
	dsb_sy_barrier();
	isb_flush();
}
void GICClearPendingIRQ(uint32_t irq) {
	if (__gic.version >= GIC_VERSION_3)
		return;

	uint32_t bit = irq % 32;
	volatile uint32_t* reg = (volatile uint32_t*)(GICD(__gic) + GICD_ICPENDR(irq));
	/* ICPENDR is write-1-to-clear: plain store, never RMW. */
	*reg = (1U << bit);
	dsb_sy_barrier();
}

void GICCheckPending(uint32_t irq) {
	/* ISPENDR0 only covers IDs 0-31. SPIs live in ISPENDR1+.
	 * The old code always read bank 0 and shifted by the full ID,
	 * so any SPI >= 32 reported the wrong state. --axiss */
	uint32_t reg = irq / 32;
	uint32_t bit = irq % 32;
	uint32_t pend = *(volatile uint32_t*)(GICD(__gic) + ISPENDING0 + reg * 4);
	if (pend & (1u << bit)) {
		//UARTDebugOut("IRQ : %d is still pending \n", irq);
	}
}

/**
 * @brief GICDumpSPI -- one-line GICv2/v3 distributor+CPU view for an SPI.
 * Call after AuVirtIOInputInitialize() or from a debugger when an MSI
 * device raises (used.idx advances) but IAR never reports it.
 */
void GICDumpSPI(uint32_t irq) {
	uint32_t reg = irq / 32;
	uint32_t bit = irq % 32;
	uint32_t en = *(volatile uint32_t*)(GICD(__gic) + 0x100 + reg * 4);
	uint32_t pend = *(volatile uint32_t*)(GICD(__gic) + ISPENDING0 + reg * 4);
	uint32_t act = *(volatile uint32_t*)(GICD(__gic) + 0x300 + reg * 4);
	uint8_t pri = *(volatile uint8_t*)(GICD(__gic) + GICD_IPRIORITYR(irq));
	uint8_t tgt = *(volatile uint8_t*)(GICD(__gic) + 0x800 + irq);
	uint32_t cfgr = GICD_ICFGR(irq / 16);
	uint32_t ctlr = gic_inl_((uint64_t*)GICD(__gic), GICD_CTLR);
	uint32_t pmr = *(volatile uint32_t*)(GICC(__gic) + GICC_PMR);
	uint32_t cctlr = *(volatile uint32_t*)(GICC(__gic) + GICC_CTLR);
	UARTDebugOut("GICD_CTLR=%x GICC_CTLR=%x GICC_PMR=%x\n", ctlr, cctlr, pmr);
	UARTDebugOut("SPI%d EN=%d PEND=%d ACT=%d PRI=%x TGT=%x CFG=%x\n", irq, (en >> bit) & 1u,
				  (pend >> bit) & 1u, (act >> bit) & 1u, pri, tgt,
				  (cfgr >> ((irq % 16u) * 2u)) & 3u);
}

/**
 * @brief GICReadIAR -- read interrupt acknowledge
 * register
 */
uint32_t GICReadIAR() {
	uint32_t iar = 0;
	if (__gic.version >= GIC_VERSION_3)
		iar = gic_read_intid();
	else
		iar = *(volatile uint32_t*)(GICC(__gic) + 0x0C);
	return iar;
}

/**
 * @brief GICSendEOI --sends end of interrupt to 
 * GIC cpu interface
 * @param irqnum -- interrupt ID of the device
 */
void GICSendEOI(uint32_t irqnum) {
	if (__gic.version >= GIC_VERSION_3)
		gic_icc_eoi(irqnum);
	else {
#define GICV2_CPUID_SHIFT 10
		//UARTDebugOut("GICEOI REG : %x \n", (GICC(__gic) + 0x10));
		//GICClearPendingIRQ(irqnum);
		*(volatile uint32_t*)(GICC(__gic) + 0x10) = irqnum;
		//UARTDebugOut("3N : %d \n", (*(volatile uint32_t*)(GICC(__gic) + 0x10)));
		dsb_ish();
		isb_flush();
	}
}

void GICSetupTimer() {
	mask_irqs();
	setupTimerIRQ();
	gic_regs[0] = 1;
	gicc_regs[0] = 1;
	gicc_regs[1] = 0x1FF;

	gic_regs[64] = 0xFFFFffff;
	gic_regs[160] = 0xFFFFffff;
	gic_regs[65] = 0xFFFFFFFF;
	gic_regs[66] = 0xFFFFFFFF;
	gic_regs[67] = 0xFFFFFFFF;
	gic_regs[520] = 0x07070707;
	gic_regs[521] = 0x07070707;
	gic_regs[543] = 0x07070707;
}

/**
 * @brief GICRegisterSPIHandler -- register a spi handler
 * to callback list
 * @param fptr -- SPI Callback address
 * @param spi -- SPI number
 */
void GICRegisterSPIHandler(void* fptr, int spi) {
	if (callbacks[spi])
		return;
	callbacks[spi] = fptr;
}

/**
 * @brief GICCallSPIHandler -- jump to a callback handler
 * associated with given spi number
 * @param spi -- SPI number
 */
void GICCallSPIHandler(int spi) {
	if (!callbacks[spi])
		return;
	irq_callback call = callbacks[spi];
	call(spi);
}

/**
 * Read back what is registered for an spi; 0 when the slot is free.
 *
 * Added for DCL's request_irq(): GICRegisterSPIHandler() is first-writer-wins
 * and gives no feedback, so a caller that registers over an occupied slot
 * would be told "yes" while the interrupt still routes to whoever claimed it
 * first. Reading the slot back is the only way for request_irq() to report
 * -EBUSY instead of handing a driver a line that will never arrive.
 */
void* GICGetSPIHandler(int spi) {
	if (spi < 0 || spi >= MAX_SPIS)
		return 0;
	return callbacks[spi];
}

/**
 * Release a slot this caller owns.
 *
 * Only called by free_irq() after GICGetSPIHandler() confirmed the slot holds
 * DCL's own dispatcher, so a native driver's registration is never cleared
 * out from under it.
 */
void GICClearSPIHandler(int spi) {
	if (spi < 0 || spi >= MAX_SPIS)
		return;
	callbacks[spi] = 0;
}

/**
 * @brief GICDisable -- disable the GIC 
 */
void GICDisable() {
	gic_outl_((uint64_t*)__gic.gicDMMIO, GICD_CTLR, 0);

	dsb_sy_barrier();
	isb_flush();

	if (__gic.version < GIC_VERSION_3) {
		gic_outl_((uint64_t*)__gic.gicCMMIO, GICC_CTLR, 0);
		gic_outl_((uint64_t*)__gic.gicCMMIO, GICC_PMR, 0xFF);
		dsb_sy_barrier();
		isb_flush();
		UARTDebugOut("[aurora]: GIC version 2 disabling \r\n");
	}
	UARTDebugOut("[aurora]: GIC disabled \r\n");
}

/**
 * @brief GICSendSGI -- send a software generated interrupt to one core
 * @param cpu -- logical cpu number of the target
 * @param sgi -- SGI id, 0..15
 */
void GICSendSGI(uint32_t cpu, uint32_t sgi) {
#ifdef __TARGET_BOARD_RPI3__
	(void)cpu;
	(void)sgi;
#else
	if (cpu >= AA64_SMP_MAX_CPUS)
		return;
	AA64PerCPU* p = &_aa64_percpu[cpu];
	/* whatever the target is woken up for must be visible first */
	__asm__ volatile("dsb ishst" ::: "memory");
	if (__gic.version >= GIC_VERSION_3) {
		uint64_t m = p->mpidr;
		uint64_t v = (((m >> 32) & 0xFF) << 48) | (((m >> 16) & 0xFF) << 32) |
					 (((m >> 8) & 0xFF) << 16) | ((uint64_t)(sgi & 0xF) << 24) |
					 (1ULL << (m & 0xF));
		__asm__ volatile("msr S3_0_C12_C11_5, %0\n isb" ::"r"(v)); /* ICC_SGI1R_EL1 */
	} else {
		uint32_t mask = p->gic_target_mask & 0xFF;
		if (!mask)
			return;
		/* GICD_SGIR: TargetListFilter 0 (use the list), CPUTargetList, ID */
		*(volatile uint32_t*)(GICD(__gic) + 0xF00) = (mask << 16) | (sgi & 0xF);
	}
#endif
}

/** @brief GICMaskIRQ -- stop the distributor forwarding an SPI */
void GICMaskIRQ(uint32_t irq) {
	if (irq < 32 || irq >= 1020)
		return;
	GICD_ICENABLE(irq / 32) = (1u << (irq % 32));
	dsb_sy_barrier();
}

/** @brief GICUnmaskIRQ -- let the distributor forward an SPI again */
void GICUnmaskIRQ(uint32_t irq) {
	if (irq < 32 || irq >= 1020)
		return;
	GICD_ISENABLER(irq / 32) = (1u << (irq % 32));
	dsb_sy_barrier();
}
