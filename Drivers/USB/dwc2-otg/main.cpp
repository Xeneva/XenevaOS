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

#include <aurora.h>
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>
#include <Mm/dma.h>
#include "dwc2_reg.h"
#include <Hal/AA64/aa64lowlevel.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/gic.h>
#include "dwc2.h"
#include "usb_hid.h"
#include <Mm/pmmngr.h>
#include <list.h>
#include <string.h>

#define DWC2_BASE 0x3F980000
uint64_t _base;
bool _root_port_ready;
bool _enable_root_port;
static int num_channel;
uint64_t* dmaAddress;
uint64_t* dmaAddressPhys;
static int dmaAddress_Offset;
#define DWC2_GSNPSID 0x40
#define HPRT_DEFAULT_MASK  ((1ULL << 1) | (1ULL <<  2) | (1ULL << 3) | (1ULL << 5))

#define DWC2_SETUP_SLOT 1
static uint32_t setup_slot;
static uint8_t _usb_address;
static dwc2_core_regs* _regs;

static AuDMAGlobalClass _dwc2dma;


#pragma pack(push,1)
typedef struct {
	uint8_t data[4096];
}dwc2_dma_slot;
#pragma pack(pop)

static dwc2_dma_slot* setup_pages[DWC2_SETUP_SLOT];

/**
 * REFERENCE USED : Linux open source project, U-Boot and USPi from Circle
 * DWC2 regsiter definitions are from mixed sources
 */

/*
* AuDriverUnload -- deattach the driver from
* aurora system
*/
AU_EXTERN AU_EXPORT int AuDriverUnload() {

	return 0;
}

/**
 * @brief dwc2_assign_address -- assigns address
 * to a usb device
 */
uint8_t dwc2_assign_address() {
	uint8_t addr = _usb_address;
	_usb_address++;
	return addr;
}

/**
 * @brief dwc2_read -- reads a value from dwc2 register
 * @param base -- memory base
 */
uint32_t dwc2_read(uint64_t base) {
	return *(volatile uint32_t*)base;
}

/**
 * @brief dwc2_write -- write a value to dwc2 register
 * @param base -- memory base
 * @param value -- value to write
 */
void dwc2_write(uint64_t base, uint32_t value) {
	*(volatile uint32_t*)base = (value & UINT32_MAX);
	dsb_sy_barrier();
	dsb_ish();
	isb_flush();
}

void dwc2_wait_ahb_idle(struct dwc2_core_regs* regs) {
	uint32_t count = 100000;
	while (!(dwc2_read((uint64_t)&regs->grstctl) & DWC2_GRSTCTL_AHBIDLE)) {
		if (--count == 0) {
			UARTDebugOut("[dwc2_otg]: idle wait timeout! \r\n");
			return;
		}
		for (int i = 0; i < 10000; i++)
			_wfi();
	}
}

/**
 * @brief dwc2_core_reset -- reset the otg controller
 * @param regs -- pointer to system registers
 */
void dwc2_core_reset(struct dwc2_core_regs* regs) {
	dwc2_wait_ahb_idle(regs);

	uint32_t value = dwc2_read((uint64_t)&regs->grstctl);
	value |= DWC2_GRSTCTL_CSFTRST;
	dwc2_write((uint64_t)&regs->grstctl, value);

	uint32_t count = 100000;
	while (dwc2_read((uint64_t)&regs->grstctl) & DWC2_GRSTCTL_CSFTRST) {
		if (--count == 0) {
			UARTDebugOut("[dwc2_otg]: reset timeout! \r\n");
			return;
		}
		for (int i = 0; i < 1000; i++)
			_wfi();
	}

	for (int i = 0; i < 100000; i++)
		_wfi();

	dwc2_wait_ahb_idle(regs);

	UARTDebugOut("[dwc2_otg]: reset completed \r\n");
}

/**
 * dwc2_config_ahb -- configure dwc2 ahb 
 */
void dwc2_config_ahb(struct dwc2_core_regs* regs) {
	/** uboot dwc2 code */
	uint32_t val = 0;
	val |= DWC2_GAHBCFG_DMAENABLE;
	val |= DWC2_GAHBCFG_HBURSTLEN_INCR4;
	val |= DWC2_GAHBCFG_GLBLINTRMSK; //same mask and enable shared (1<<0)
	dwc2_write((uint64_t)&regs->gahbcfg, val);

}


void dwc2_config_usb_phy(struct dwc2_core_regs* regs) {
	uint32_t val = dwc2_read((uint64_t)&regs->gusbcfg);

	val &= ~DWC2_GUSBCFG_ULPI_UTMI_SEL;
	val &= ~(5ull << 10);

	val &= ~DWC2_GUSBCFG_USBTRDTIM_MASK;
	val |= (9 << 10);

	// force host mode
	val &= ~DWC2_GUSBCFG_FORCEDEVMODE;
	val |= DWC2_GUSBCFG_FORCEHOSTMODE;

	val &= ~DWC2_GUSBCFG_HNPCAP;
	val &= ~DWC2_GUSBCFG_SRPCAP;

	dwc2_write((uint64_t)&regs->gusbcfg, val);
	for (int i = 0; i < 10000000; i++)
		_wfi();
}

void dwc2_config_fifos(struct dwc2_core_regs* regs) {
	dwc2_write((uint64_t)&regs->grxfsiz, 256);
	dwc2_write((uint64_t)&regs->gnptxsts, (256 << 16) | 256);
	dwc2_write((uint64_t)&regs->hptxfsiz, (256 << 16) | 256 | 256);
}

void dwc2_init_core(struct dwc2_core_regs* regs) {
	uint32_t usbconfig = dwc2_read((uint64_t)&regs->gusbcfg);
	usbconfig &= ~DWC2_GUSBCFG_ULPI_EXT_VBUS_DRV;
	usbconfig &= ~DWC2_GUSBCFG_TERM_SEL_DL_PULSE;
	dwc2_write((uint64_t)&regs->gusbcfg, usbconfig);

	dwc2_core_reset(regs);

	usbconfig = dwc2_read((uint64_t)&regs->gusbcfg);
	usbconfig &= ~DWC2_GUSBCFG_ULPI_UTMI_SEL;
	usbconfig &= ~DWC2_GUSBCFG_PHYIF;
	dwc2_write((uint64_t)&regs->gusbcfg, usbconfig);

	uint32_t hwcfg2 = dwc2_read((uint64_t)&regs->ghwcfg2);
	uint8_t arch = (hwcfg2 >> 3) & 0x3;
	uint32_t hs_phy_type = (hwcfg2 >> 13) & 0x3;
	uint32_t fs_phy_type = (hwcfg2 >> 16) & 0x3;

	UARTDebugOut("[dwc2_otg]: architecture : %d \r\n", arch);

	usbconfig = dwc2_read((uint64_t)&regs->gusbcfg);
	
	if (hs_phy_type == 2
		&& fs_phy_type == 1) {
		usbconfig |= DWC2_GUSBCFG_ULPI_FSLS;
		usbconfig |= DWC2_GUSBCFG_ULPI_CLK_SUS_M;
	}
	else {
		usbconfig &= ~DWC2_GUSBCFG_ULPI_FSLS;
		usbconfig &= ~DWC2_GUSBCFG_ULPI_CLK_SUS_M;
	}
	dwc2_write((uint64_t)&regs->gusbcfg, usbconfig);

#define DWC2_GAHBCFG_WAIT_AXI_WRITES (1ULL << 4)
#define DWC2_GAHBCFG_MAX_AXI_BURST__MASK (3ULL << 1)
#define DWC2_GAHBCFG_MAX_AXI_BURST__SHIFT 1

	//skipping num channel verification kelalalala
	uint32_t ahbcfg = dwc2_read((uint64_t)&regs->gahbcfg);
	ahbcfg |= DWC2_GAHBCFG_DMAENABLE;
	ahbcfg |= DWC2_GAHBCFG_WAIT_AXI_WRITES;
	ahbcfg &= ~DWC2_GAHBCFG_MAX_AXI_BURST__MASK;
	dwc2_write((uint64_t)&regs->gahbcfg, ahbcfg);

	usbconfig = dwc2_read((uint64_t)&regs->gusbcfg);
	usbconfig &= ~DWC2_GUSBCFG_HNPCAP;
	usbconfig &= ~DWC2_GUSBCFG_SRPCAP;
	dwc2_write((uint64_t)&regs->gusbcfg, usbconfig);

	dwc2_write((uint64_t)&regs->gintsts, 0xFFFFFFFF);
	
}

void dwc2_enable_global_interrupt(struct dwc2_core_regs* regs) {
	uint32_t ahbcfg = dwc2_read((uint64_t)&regs->gahbcfg);
	ahbcfg |= DWC2_GAHBCFG_GLBLINTRMSK; 
//	UARTDebugOut("glbl ahbcfg---- : %x \r\n", ahbcfg);
	dwc2_write((uint64_t)&regs->gahbcfg, ahbcfg);
}

void dwc2_flush_tx_fifo(struct dwc2_core_regs* regs, uint32_t nfifo) {
	uint32_t timeout = 10000;
	while (!(dwc2_read((uint64_t)&regs->grstctl) & (1ULL << 31)))
		if (--timeout == 0) return;
	dwc2_write((uint64_t)&regs->grstctl, ((nfifo & 0x1F) << 6) | (1ULL << 5));

	while(dwc2_read((uint64_t)&regs->grstctl) & (1ULL << 5))
		if (--timeout == 0) return;

	/*for (int i = 0; i < 10000000; i++)
		_wfi();*/

}

void dwc2_flush_rx_fifo(struct dwc2_core_regs* regs) {
	uint32_t timeout = 10000;
	while (!(dwc2_read((uint64_t)&regs->grstctl) & (1ULL << 31))) 
		if (--timeout == 0) return;
	
	dwc2_write((uint64_t)&regs->grstctl, (1ULL << 4));

	timeout = 10000;
	while (!(dwc2_read((uint64_t)&regs->grstctl) & (1ULL << 4)))
		if (--timeout == 0) return;

	/*for (int i = 0; i < 10000000; i++) {
		_wfi();
	}*/
}

void dwc2_enable_host_interrupts(struct dwc2_core_regs* regs) {
	dwc2_write((uint64_t)&regs->gintmsk, 0);

	dwc2_write((uint64_t)&regs->gintsts, 0xFFFFFFFF);

	uint32_t gintmsk = dwc2_read((uint64_t)&regs->gintmsk);
	gintmsk |= (1ULL << 25);
	gintmsk |= (1ULL << 24);
	dwc2_write((uint64_t)&regs->gintmsk, gintmsk);
	for (int i = 0; i < 10000; i++)
		_wfi();
}
void dwc2_init_host(struct dwc2_core_regs* regs) {
	/** restart the phy clock **/
	dwc2_write((uint64_t)_base + 0xE00, 0);

#define DWC2_HWCFG1_FSLS_PCLK_SEL__MASK (3ULL << 0)

	uint32_t hostcfg = dwc2_read((uint64_t)&regs->ghwcfg1);
	hostcfg &= ~DWC2_HWCFG1_FSLS_PCLK_SEL__MASK;

	uint32_t hostcfg2 = dwc2_read((uint64_t)&regs->ghwcfg2);
	uint32_t usbcfg = dwc2_read((uint64_t)&regs->gusbcfg);

	uint32_t hs_phy_type = (hostcfg2 >> 6) & 0x3;
	uint32_t fs_phy_type = (hostcfg2 >> 8) & 0x3;

#define DWC2_HWCFG1_FSLS_PCLK_SEL_48_MHZ 1
#define DWC2_HWCFG1_FSLS_PCLK_SEL_30_60_MHZ 0

	if (hs_phy_type == 2 && fs_phy_type == 1 && (usbcfg & (1ULL << 17)))
		hostcfg |= DWC2_HWCFG1_FSLS_PCLK_SEL_48_MHZ;
	else
		hostcfg |= DWC2_HWCFG1_FSLS_PCLK_SEL_30_60_MHZ;

	dwc2_write((uint64_t)&regs->ghwcfg1, hostcfg);

	dwc2_write((uint64_t)&regs->grxfsiz, 1024);
	dwc2_write((uint64_t)&regs->gnptxfsiz, (1024 << 16) | 1024);
	dwc2_write((uint64_t)&regs->hptxfsiz, (1024 << 16) | (1024 + 1024));

	dwc2_flush_tx_fifo(regs, 0x10);
	dwc2_flush_rx_fifo(regs);

	uint32_t val = *(volatile uint32_t*)&regs->ghwcfg2;
	val &= DWC2_HWCFG2_NUM_HOST_CHAN_MASK;
	val >>= DWC2_HWCFG2_NUM_HOST_CHAN_OFFSET;
	int num_host_channel = val + 1;
	for (int i = 0; i < num_host_channel; i++) {
		uint32_t chan = dwc2_read((uint64_t)&regs->hc_regs[i].hcchar);
		chan &= ~(DWC2_HCCHAR_CHEN | DWC2_HCCHAR_EPDIR);
		chan |= DWC2_HCCHAR_CHDIS;
		dwc2_write((uint64_t)&regs->hc_regs[i].hcchar, chan);
	}

	int timeout = 1000;
	for (int i = 0; i < num_host_channel; i++) {
		uint32_t val = dwc2_read((uint64_t)&regs->hc_regs[i].hcchar);
		val &= ~DWC2_HCCHAR_EPDIR;
		val |= (DWC2_HCCHAR_CHEN | DWC2_HCCHAR_CHDIS);
		dwc2_write((uint64_t)&regs->hc_regs[i].hcchar, val);

		timeout = 1000;
		while (timeout--) {
			if (!(dwc2_read((uint64_t)&regs->hc_regs[i].hcchar) & DWC2_HCCHAR_CHEN))
				break;
			AA64SleepUS(1);
		}

		
		UARTDebugOut("[dwc2-otg]: halting OTG channel %d , timeout : %d \r\n", i, timeout);
	}
	uint32_t hostport = dwc2_read((uint64_t)&regs->hprt0);
	hostport &= ~HPRT_DEFAULT_MASK;
	if (!(hostport & (1ULL << 12))) {
		hostport |= (1ULL << 12);
		dwc2_write((uint64_t)&regs->hprt0, hostport);
	}

	dwc2_write((uint64_t)_base + 0x088, 1ULL);

	dwc2_write((uint64_t)&regs->gahbcfg, 0x30);

	dwc2_enable_host_interrupts(regs);
	dwc2_enable_global_interrupt(regs);
}

bool dwc2_enable_root_port(struct dwc2_core_regs* regs) {
	int timeout = 20000;
	while (!(dwc2_read((uint64_t)&regs->hprt0) & (1ULL << 0))) {
		AA64SleepMS(1);
		if (--timeout == 0) {
			UARTDebugOut("root port didn't get connected \r\n");
			return false;
		}
	}

	AA64SleepMS(500);

	uint32_t hprt = dwc2_read((uint64_t)&regs->hprt0);
	hprt &= ~(1 << 1) | (1 << 3) | (1 << 5);
	hprt |= (1ULL << 8);
	dwc2_write((uint64_t)&regs->hprt0, hprt);
	
	AA64SleepMS(500);
	
	hprt = dwc2_read((uint64_t)&regs->hprt0);
	hprt &= ~(1 << 1) | (1 << 3) | (1 << 5);
	hprt &= ~(1ULL << 8);
	hprt |= (1ULL << 12);
	dwc2_write((uint64_t)&regs->hprt0, hprt);


	UARTDebugOut("[dwc2-otg]: root port enabled \r\n");
	return true;
}

/**
 * @brief dwc2_interrupt_handler -- interrupt handler for
 * dwc2 OTG controller
 * @param spiID -- system passed interrupt id 
 */
void dwc2_interrupt_handler(int spiID) {
	dsb_sy_barrier();
//	UARTDebugOut("dwc2 irq fired++ \r\n");
	struct dwc2_core_regs* regs = (struct dwc2_core_regs*)_base;
	uint32_t gintsts = dwc2_read((uint64_t)&regs->gintsts);
	uint32_t gintmsk = dwc2_read((uint64_t)&regs->gintmsk);
	//gintsts &= gintmsk;

	/** PORT interrupt **/
	if (gintsts & (1ULL << 24)) {
		uint32_t hprt = dwc2_read((uint64_t)&regs->hprt0);
		uint32_t hprt_clear = hprt & ~HPRT_DEFAULT_MASK;
		
		if (hprt & (1ULL << 1)) {
			dwc2_write((uint64_t)&regs->hprt0, hprt_clear | (1ULL << 1));
			
			UARTDebugOut("[dwc2_otg]: device connected \r\n");
			switch (((hprt >> 17) & 0x3)) {
			case 0:
				UARTDebugOut("[dwc2_otg]: detected HIGH-SPEED device \r\n");
				break;
			case 1:
				UARTDebugOut("[dwc2_otg]: detected FULL-SPEED device \r\n");
				break;
			case 2:
				UARTDebugOut("[dwc2_otg]: detected LOW-SPEED device \r\n");
				break;
			}
			dwc2_write((uint64_t)&regs->hprt0, hprt_clear | (1ULL << 1));

			if (hprt & (1ULL << 0)) {
				hprt = dwc2_read((uint64_t)&regs->hprt0);
				UARTDebugOut("[dwc2_otg]: physical device is present \r\n");
				//if (hprt & (1 << 0))
				//for (int i = 0; i < 1000000; i++);
				_enable_root_port = 1;
			}
			else {
				UARTDebugOut("Spurious connect disconnected \r\n");
			}

			//trigger port reset
		}
	
		if (hprt & (1ULL << 3)) {
			UARTDebugOut("Port status changed \r\n");
			if (hprt & (1ULL << 2)) {
				UARTDebugOut("[dwc2_otg]: port enabled \r\n");
				_root_port_ready = 1;
			}
			else {
				UARTDebugOut("[dwc2_otg]: port disabled \r\n");
			}
			dwc2_write((uint64_t)&regs->hprt0, hprt_clear | (1ULL << 3));
		}

		if (hprt & (1ULL << 5)) {
			UARTDebugOut("[dwc2_otg]: iss iss port-overcurrent marisi, shock lagisi uuuu maaa \r\n");
			dwc2_write((uint64_t)&regs->hprt0, hprt_clear | (1ULL << 5));
		}
	}

	/**
	 * handle host channel interrupts here
	 */
	if (gintsts & (1ULL << 25)) {
		uint32_t haint = dwc2_read((uint64_t)&regs->host_regs.haint);
		uint32_t haintmsk = dwc2_read((uint64_t)&regs->host_regs.haintmsk);

		uint32_t pending = haint & haintmsk;

		for (int i = 0; i < num_channel; i++) {
			if (!(pending & (1u << i)))
				continue;
			dwc2_handle_channel_interrupt(regs, i);
		}
	}

	dwc2_write((uint64_t)&regs->gintsts, gintsts);
}

extern uint8_t port_changed;
void dwc2_initialize(struct dwc2_core_regs* regs) {
	/** data memory barrier **/
	dsb_sy_barrier();

	/** vendor id check **/
    //skip maar kelaaaaaa

	/** power on usb hcd via mailbox **/
	AuRPISetPowerState(RPI_POWERUP_USB_HCD);


	/** disable all global interrupt in gahbcfg **/
	uint32_t val = dwc2_read((uint64_t)&regs->gahbcfg);
	val &= ~DWC2_GAHBCFG_GLBLINTRMSK;
	dwc2_write((uint64_t)&regs->gahbcfg, val);


	/** connect irq handler **/
	GICRegisterSPIHandler(dwc2_interrupt_handler, 9);
	AuRPI3PeripheralIRQEnable(9);
	
	/** core init */
	dwc2_init_core(regs);

	UARTDebugOut("[dwc2_otg]: core initialized \r\n");


	/** global irq init **/
	dwc2_enable_global_interrupt(regs);

	/** initialize the host **/
	dwc2_init_host(regs);

	UARTDebugOut("[dwc2_otg]: host initialized \r\n");

	/** root port enumeration **/

	enable_irqs();

	//lets wait for 1s to get device connection
	//interrupt asserted 
	AA64SleepMS(100);

	int timeout = 500000;
	while (1) {
		if (_enable_root_port == 1)
			break;
		if (--timeout == 0)
			break;
	}

	if (_enable_root_port)
		dwc2_enable_root_port(regs);

	dsb_sy_barrier();

	AA64SleepMS(100);
	timeout = 500000;
	while (1) {
		if (_root_port_ready == 1)
			break;
		if (--timeout)
			break;
	}

	if (_root_port_ready) {
		UARTDebugOut("[dwc2-otg]: enumeration thing is work in progress \r\n");
		dwc2_enumerate_root_device(regs);
	}

}

void dwc2_dump_regs(struct dwc2_core_regs* regs) {
	UARTDebugOut("GAHBCFG = %x \r\n", dwc2_read((uint64_t)&regs->gahbcfg));
	UARTDebugOut("GUSBCFG = %x \r\n", dwc2_read((uint64_t)&regs->gusbcfg));
	UARTDebugOut("GRSTCTL = %x \r\n", dwc2_read((uint64_t)&regs->grstctl));
	UARTDebugOut("GINTSTS = %x \r\n", dwc2_read((uint64_t)&regs->gintsts));
	UARTDebugOut("GINTMSK = %x \r\n", dwc2_read((uint64_t)&regs->gintmsk));
	UARTDebugOut("GRXFSIZ = %x \r\n", dwc2_read((uint64_t)&regs->grxfsiz));
	UARTDebugOut("GNPTXFSIZ = %x \r\n", dwc2_read((uint64_t)&regs->gnptxfsiz));
	UARTDebugOut("GHWCFG2 = %x \r\n", dwc2_read((uint64_t)&regs->ghwcfg2));
	UARTDebugOut("PGCTRL = %x \r\n", dwc2_read((uint64_t)&regs->pcgcctl));

	UARTDebugOut("HCFG = %x \r\n", dwc2_read((uint64_t)&regs->ghwcfg1));
	UARTDebugOut("HPRT0 = %x \r\n", dwc2_read((uint64_t)&regs->hprt0));
	UARTDebugOut("VBUS = %x \r\n", dwc2_read(_base + 0x088));
}

#define DMA_ADDRESS 0xFFFFF00000000000
static list_t* setupPacketBuffers;
/*
* AuDriverMain -- Main entry for dwc2 otg driver
*/
AU_EXTERN AU_EXPORT int AuDriverMain() {
	UARTDebugOut("[dwc2_otg]: initializing synopsys dwc2 \r\n");
	_base = (uint64_t)AuMapMMIO(DWC2_BASE, 2);
	_root_port_ready = false;
	_enable_root_port = false;


	AuDMAGlobalClassInitialize(&_dwc2dma, "dwc2");
	_usb_address = 1;

	/**
	 * This driver is only for RPI for now, and before initializing
	 * the driver it is powered up by VideoCore firmware, so life 
	 * is easy, but for other boards, we need to power it up 
	 * from their respective power control block
	 */
	struct dwc2_core_regs* regs = (struct dwc2_core_regs*)_base;
	uint32_t id = regs->gsnpsid;
	UARTDebugOut("[dwc2_otg]: synopsys id: %x \r\n", id);	
	uint32_t val = *(volatile uint32_t*)&regs->ghwcfg2;
	val &= DWC2_HWCFG2_NUM_HOST_CHAN_MASK;
	val >>= DWC2_HWCFG2_NUM_HOST_CHAN_OFFSET;
	int num_host_channel = val + 1;
	num_channel = num_host_channel;
	UARTDebugOut("[dwc2_otg]: number of host channels : %d \r\n", num_host_channel);

	_regs = regs;

	dwc2_initialize(regs);
	UARTDebugOut("[dwc2_org]: successfully initialized \r\n");


	mask_irqs();
	return 0;
}

uint64_t dwc2_get_base() {
	return _base;
}

void* dwc2_get_dma_address() {
	int offset = dmaAddress_Offset * 64;
	return (dmaAddress + offset);
}

void* dwc2_get_dma_address_phys() {
	dwc2_dma_slot* page = setup_pages[setup_slot];
	UARTDebugOut("[[[[Slot Index : %d \r\n", setup_slot);
	setup_slot = (setup_slot + 1) % DWC2_SETUP_SLOT;
	return (void*)page;
}

/**
 * @brief dwc2_add_to_used_dma_list -- 
 * there is a bug i guess within the dwc2 ip 
 * where same buffer can't be used for setup
 * packet
 */
void dwc2_add_to_used_dma_list(void* phys) {
	list_add(setupPacketBuffers, phys);
}

/**
 * @brief dwc2_free_used_dma_list -- free up all used
 * physical memories, this function must be called
 * at the end of the class driver initialization
 */
void dwc2_free_used_dma_list() {
	for (int i = 0; i < setupPacketBuffers->pointer; i++) {
		void* phys = (void*)list_remove(setupPacketBuffers, i);
		AuPmmngrReleasePage((uint64_t)V2P((uint64_t)phys));
	}
}

/**
 * @brief dwc2_get_dma_class -- get global dma class
 */
AuDMAGlobalClass* dwc2_get_dma_class() {
	return &_dwc2dma;
}

dwc2_core_regs* dwc2_get_core_regs() {
	return _regs;
}