/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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
#include <Mm/pmmngr.h>
#include <string.h>
#include "dwc2_hw.h"
#include "usb.h"

/*
 * DWC2 host controller, polled, buffer DMA, one host channel.
 *
 * Written from the Synopsys DWC_otg programming model (core soft reset,
 * host-mode programming, host channel transfers in buffer DMA mode), the
 * USB 2.0 specification (split transactions, 11.14 and 11.17-11.18) and
 * the BCM2835 ARM Peripherals manual. Register usage and timings were
 * cross-checked against the Linux dwc2 and U-Boot dwc2 drivers; no code
 * is taken from them.
 *
 * No DWC2 interrupt is used: GAHBCFG.GLBL_INTR_EN stays 0 and completion
 * is detected by polling HCINT.CHHLTD.
 */

#define DWC2_CH 0
#define DWC2_DMA_SIZE 4096
/* FIFO split (in 32-bit words) used on the BCM2835/7 controller, whose
 * GHWCFG3 reports a 4080/4096 word FIFO: 774 RX, 256 non-periodic TX,
 * 512 periodic TX */
#define DWC2_RX_FIFO	774u
#define DWC2_NPTX_FIFO	256u
#define DWC2_PTX_FIFO	512u
#define DWC2_PKT_MAX	511u /* bcm max_packet_count */

static volatile uint8_t* dwc2_regs;
/* how long a channel may run before it is forced to halt: control
 * transfers can be slow during setup, a periodic (interrupt) transaction
 * completes or NAKs within its (micro)frame */
#define DWC2_HALT_TIMEOUT_CTRL_US 100000
#define DWC2_HALT_TIMEOUT_INTR_US 5000
static uint64_t dwc2_halt_timeout_us = DWC2_HALT_TIMEOUT_CTRL_US;
static uint64_t dwc2_dma_phys;
static uint8_t* dwc2_dma;
static uint32_t dwc2_num_channels;

/* ------------------------------------------------------------------ */

static inline uint32_t rd(uint32_t off) {
	return *(volatile uint32_t*)(dwc2_regs + off);
}

static inline void wr(uint32_t off, uint32_t val) {
	*(volatile uint32_t*)(dwc2_regs + off) = val;
	__asm__ volatile("dsb sy" ::: "memory");
}

uint64_t dwc2_now_us() {
	uint64_t cnt, frq;
	__asm__ volatile("isb\n mrs %0, cntpct_el0" : "=r"(cnt));
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	return (cnt / frq) * 1000000ULL + ((cnt % frq) * 1000000ULL) / frq;
}

void dwc2_delay_us(uint64_t us) {
	uint64_t start = dwc2_now_us();
	while (dwc2_now_us() - start < us)
		;
}

void dwc2_delay_ms(uint64_t ms) {
	dwc2_delay_us(ms * 1000ULL);
}

/* wait until (reg & mask) is set (want_set) or clear, up to timeout_us */
static bool wait_bit(uint32_t off, uint32_t mask, bool want_set, uint64_t timeout_us) {
	uint64_t start = dwc2_now_us();
	for (;;) {
		bool set = (rd(off) & mask) != 0;
		if (set == want_set)
			return true;
		if (dwc2_now_us() - start > timeout_us)
			return false;
	}
}

/* DMA buffer cache maintenance: the controller reads/writes memory
 * directly through the VideoCore bus */
static void dma_clean(uint32_t len) {
	for (uint64_t va = (uint64_t)dwc2_dma; va < (uint64_t)dwc2_dma + len; va += 64)
		__asm__ volatile("dc cvac, %0" ::"r"(va) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

static void dma_clean_inval(uint32_t len) {
	for (uint64_t va = (uint64_t)dwc2_dma; va < (uint64_t)dwc2_dma + len; va += 64)
		__asm__ volatile("dc civac, %0" ::"r"(va) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

static void dma_inval(uint32_t len) {
	for (uint64_t va = (uint64_t)dwc2_dma; va < (uint64_t)dwc2_dma + len; va += 64)
		__asm__ volatile("dc ivac, %0" ::"r"(va) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

/* ------------------------------------------------------------------
 * core
 * ------------------------------------------------------------------ */

/* core soft reset: AHB idle, CSFTRST self-clears, AHB idle again */
static bool dwc2_core_reset() {
	if (!wait_bit(GRSTCTL, GRSTCTL_AHBIDLE, true, 100000))
		UARTDebugOut("[dwc2]: AHB not idle before reset \r\n");
	wr(GRSTCTL, rd(GRSTCTL) | GRSTCTL_CSFTRST);
	if (!wait_bit(GRSTCTL, GRSTCTL_CSFTRST, false, 100000)) {
		UARTDebugOut("[dwc2]: core soft reset timed out \r\n");
		return false;
	}
	if (!wait_bit(GRSTCTL, GRSTCTL_AHBIDLE, true, 100000)) {
		UARTDebugOut("[dwc2]: AHB not idle after reset \r\n");
		return false;
	}
	/* settle time after the soft reset */
	dwc2_delay_ms(100);
	return true;
}

/* GRSTCTL.TXFFLSH with TXFNUM; 3 PHY clocks afterwards (1 us) */
static void dwc2_flush_tx_fifo(uint32_t num) {
	wait_bit(GRSTCTL, GRSTCTL_AHBIDLE, true, 10000);
	wr(GRSTCTL, GRSTCTL_TXFFLSH | (num << GRSTCTL_TXFNUM_SHIFT));
	if (!wait_bit(GRSTCTL, GRSTCTL_TXFFLSH, false, 10000))
		UARTDebugOut("[dwc2]: tx fifo flush timed out \r\n");
	dwc2_delay_us(1);
}

static void dwc2_flush_rx_fifo() {
	wait_bit(GRSTCTL, GRSTCTL_AHBIDLE, true, 10000);
	wr(GRSTCTL, GRSTCTL_RXFFLSH);
	if (!wait_bit(GRSTCTL, GRSTCTL_RXFFLSH, false, 10000))
		UARTDebugOut("[dwc2]: rx fifo flush timed out \r\n");
	dwc2_delay_us(1);
}

static bool dwc2_core_init() {
	uint32_t usbcfg = rd(GUSBCFG);
	usbcfg &= ~(GUSBCFG_ULPI_EXT_VBUS_DRV | GUSBCFG_TERMSELDLPULSE);
	wr(GUSBCFG, usbcfg);
	if (!dwc2_core_reset())
		return false;

	/* the BCM2837 has an internal UTMI+ PHY, 8 bit interface */
	usbcfg = rd(GUSBCFG);
	usbcfg &= ~(GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16);
	wr(GUSBCFG, usbcfg);
	/* a PHY selection change takes effect with another soft reset */
	if (!dwc2_core_reset())
		return false;

	usbcfg = rd(GUSBCFG);
	usbcfg &= ~(GUSBCFG_ULPI_FS_LS | GUSBCFG_ULPI_CLK_SUSP_M);
	wr(GUSBCFG, usbcfg);

	/* internal DMA, BCM burst setting; global interrupt stays off */
	uint32_t hwcfg2 = rd(GHWCFG2);
	if (((hwcfg2 & GHWCFG2_ARCH_MASK) >> GHWCFG2_ARCH_SHIFT) != GHWCFG2_INT_DMA_ARCH) {
		UARTDebugOut("[dwc2]: controller has no internal DMA (GHWCFG2 %x) \r\n", hwcfg2);
		return false;
	}
	wr(GAHBCFG, GAHBCFG_BCM2835 | GAHBCFG_DMA_EN);

	/* host only: no HNP/SRP, force host mode, then wait for
	 * GINTSTS.CURMOD to report it */
	usbcfg = rd(GUSBCFG);
	usbcfg &= ~(GUSBCFG_HNPCAP | GUSBCFG_SRPCAP | GUSBCFG_FORCEDEVMODE);
	usbcfg |= GUSBCFG_FORCEHOSTMODE;
	wr(GUSBCFG, usbcfg);
	if (!wait_bit(GINTSTS, GINTSTS_CURMODE_HOST, true, 110000)) {
		UARTDebugOut("[dwc2]: core did not enter host mode \r\n");
		return false;
	}

	wr(GINTMSK, 0);
	wr(GINTSTS, 0xFFFFFFFF);
	return true;
}

static void dwc2_host_init() {
	/* restart the PHY clock */
	wr(PCGCCTL, 0);

	/* UTMI+ PHY: FS/LS PHY clock is 30/60 MHz */
	uint32_t hcfg = rd(HCFG);
	hcfg &= ~HCFG_FSLSPCLKSEL_MASK;
	hcfg |= HCFG_FSLSPCLKSEL_30_60_MHZ;
	wr(HCFG, hcfg);

	if (rd(GHWCFG2) & GHWCFG2_DYNAMIC_FIFO) {
		wr(GRXFSIZ, DWC2_RX_FIFO);
		wr(GNPTXFSIZ, (DWC2_NPTX_FIFO << 16) | DWC2_RX_FIFO);
		wr(HPTXFSIZ, (DWC2_PTX_FIFO << 16) | (DWC2_RX_FIFO + DWC2_NPTX_FIFO));
	}

	wr(GOTGCTL, rd(GOTGCTL) & ~GOTGCTL_HSTSETHNPEN);

	dwc2_flush_tx_fifo(GRSTCTL_TXFNUM_ALL);
	dwc2_flush_rx_fifo();

	/* drop leftover requests, then halt every channel so all start from
	 * a known state */
	for (uint32_t ch = 0; ch < dwc2_num_channels; ch++) {
		uint32_t hcchar = rd(HCCHAR(ch));
		hcchar &= ~(HCCHAR_CHENA | HCCHAR_EPDIR);
		hcchar |= HCCHAR_CHDIS;
		wr(HCCHAR(ch), hcchar);
	}
	for (uint32_t ch = 0; ch < dwc2_num_channels; ch++) {
		uint32_t hcchar = rd(HCCHAR(ch));
		hcchar &= ~HCCHAR_EPDIR;
		hcchar |= HCCHAR_CHENA | HCCHAR_CHDIS;
		wr(HCCHAR(ch), hcchar);
		if (!wait_bit(HCCHAR(ch), HCCHAR_CHENA, false, 10000))
			UARTDebugOut("[dwc2]: channel %d did not halt \r\n", ch);
	}
	wr(HAINTMSK, 0);
	wr(HAINT, 0xFFFFFFFF);

	/* VBUS on */
	uint32_t hprt = rd(HPRT0) & ~HPRT0_W1C_MASK;
	if (!(hprt & HPRT0_PWR))
		wr(HPRT0, hprt | HPRT0_PWR);
}

/**
 * @brief dwc2_hcd_init -- map, reset and initialize the controller
 */
int dwc2_hcd_init() {
	if (!dwc2_regs)
		dwc2_regs = (volatile uint8_t*)AuMapMMIO(DWC2_PHYS_BASE, 1);

	uint32_t snpsid = rd(GSNPSID);
	/* "OT" followed by the core version */
	if ((snpsid & 0xFFFF0000u) != 0x4F540000u) {
		UARTDebugOut("[dwc2]: no DWC2 core (GSNPSID %x) \r\n", snpsid);
		return USB_ENODEV;
	}
	dwc2_num_channels = ((rd(GHWCFG2) & GHWCFG2_NUM_HOST_CHAN_MASK) >> GHWCFG2_NUM_HOST_CHAN_SHIFT) + 1;
	UARTDebugOut("[dwc2]: core %x, %d host channels, fifo %d words \r\n", snpsid,
				 dwc2_num_channels, rd(GHWCFG3) >> GHWCFG3_DFIFO_DEPTH_SHIFT);

	if (!dwc2_dma) {
		dwc2_dma_phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_KERNEL);
		if (dwc2_dma_phys == PMM_INVALID_PHYS)
			return USB_EIO;
		dwc2_dma = (uint8_t*)P2V(dwc2_dma_phys);
	}

	if (!dwc2_core_init())
		return USB_EIO;
	dwc2_host_init();
	return USB_OK;
}

/* ------------------------------------------------------------------
 * root port
 * ------------------------------------------------------------------ */

uint8_t dwc2_root_port_speed() {
	return (uint8_t)((rd(HPRT0) & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT);
}

/**
 * @brief dwc2_root_port_reset -- wait for a device, reset and enable the
 * root port (USB 2.0 timings: 100 ms connect
 * debounce, 50 ms root port reset, 10 ms reset recovery)
 * @param speed -- out, DWC2_SPEED_* of the attached device
 */
int dwc2_root_port_reset(uint8_t* speed) {
	if (!wait_bit(HPRT0, HPRT0_CONNSTS, true, 2000000)) {
		UARTDebugOut("[dwc2]: nothing connected to the root port \r\n");
		return USB_ENODEV;
	}
	dwc2_delay_ms(100);

	uint32_t hprt = rd(HPRT0) & ~HPRT0_W1C_MASK;
	wr(HPRT0, hprt | HPRT0_RST);
	dwc2_delay_ms(50);
	hprt = rd(HPRT0) & ~(HPRT0_W1C_MASK | HPRT0_RST);
	wr(HPRT0, hprt);
	dwc2_delay_ms(20);

	if (!wait_bit(HPRT0, HPRT0_ENA, true, 200000)) {
		UARTDebugOut("[dwc2]: root port did not enable (HPRT0 %x) \r\n", rd(HPRT0));
		return USB_EIO;
	}
	/* acknowledge the connect / enable change events */
	hprt = rd(HPRT0);
	wr(HPRT0, (hprt & ~HPRT0_W1C_MASK) | (hprt & (HPRT0_CONNDET | HPRT0_ENACHG | HPRT0_OVRCURRCHG)));

	*speed = dwc2_root_port_speed();
	return USB_OK;
}

/* ------------------------------------------------------------------
 * transfers: one host channel, buffer DMA, completion by CHHLTD
 * ------------------------------------------------------------------ */

static void dwc2_channel_setup(usb_device_t* dev, uint8_t ep, int in, uint8_t eptype, uint16_t mps) {
	uint32_t hcchar = ((uint32_t)dev->address << HCCHAR_DEVADDR_SHIFT) |
					  ((uint32_t)(ep & 0xF) << HCCHAR_EPNUM_SHIFT) |
					  (in ? HCCHAR_EPDIR : 0) |
					  ((uint32_t)eptype << HCCHAR_EPTYPE_SHIFT) |
					  (mps & HCCHAR_MPS_MASK);
	if (dev->speed == DWC2_SPEED_LOW)
		hcchar |= HCCHAR_LSPDDEV;
	wr(HCINT(DWC2_CH), 0x3FFF);
	wr(HCINTMSK(DWC2_CH), 0);
	wr(HCCHAR(DWC2_CH), hcchar);
	if (dev->split)
		wr(HCSPLT(DWC2_CH), HCSPLT_SPLTENA | (HCSPLT_XACTPOS_ALL << HCSPLT_XACTPOS_SHIFT) |
								((uint32_t)dev->tt_hub << HCSPLT_HUBADDR_SHIFT) |
								((uint32_t)dev->tt_port << HCSPLT_PRTADDR_SHIFT));
	else
		wr(HCSPLT(DWC2_CH), 0);
}

/* stop a channel that never halted by itself */
static void dwc2_channel_halt() {
	uint32_t hcchar = rd(HCCHAR(DWC2_CH));
	if (hcchar & HCCHAR_CHENA) {
		wr(HCCHAR(DWC2_CH), (hcchar & ~HCCHAR_EPDIR) | HCCHAR_CHDIS | HCCHAR_CHENA);
		wait_bit(HCINT(DWC2_CH), HCINT_CHHLTD, true, 10000);
	}
	wr(HCINT(DWC2_CH), 0x3FFF);
}

/* wait for the channel to halt and classify why it did */
static int dwc2_channel_wait(uint32_t* remaining, uint8_t* pid) {
	if (!wait_bit(HCINT(DWC2_CH), HCINT_CHHLTD, true, dwc2_halt_timeout_us)) {
		dwc2_channel_halt();
		return USB_ETIME;
	}
	uint32_t hcint = rd(HCINT(DWC2_CH));
	uint32_t hctsiz = rd(HCTSIZ(DWC2_CH));
	*remaining = hctsiz & HCTSIZ_XFERSIZE_MASK;
	*pid = (uint8_t)((hctsiz & HCTSIZ_PID_MASK) >> HCTSIZ_PID_SHIFT);
	if (hcint & HCINT_XFERCOMPL)
		return USB_OK;
	if (hcint & (HCINT_NAK | HCINT_FRMOVRUN))
		return USB_EAGAIN;
	if (hcint & HCINT_STALL)
		return USB_ESTALL;
	return USB_EIO;
}

static int dwc2_channel_run(uint8_t* pid, int in, uint8_t* buffer, uint32_t num_packets,
						  uint32_t xfer_len, uint32_t* actual, int odd_frame) {
	wr(HCTSIZ(DWC2_CH), (xfer_len & HCTSIZ_XFERSIZE_MASK) |
							(num_packets << HCTSIZ_PKTCNT_SHIFT) |
							((uint32_t)*pid << HCTSIZ_PID_SHIFT));
	uint32_t span = (xfer_len + 63) & ~63u;
	if (xfer_len) {
		if (in) {
			dma_clean_inval(span);
		} else {
			memcpy(dwc2_dma, buffer, xfer_len);
			dma_clean(span);
		}
	}
	wr(HCDMA(DWC2_CH), (uint32_t)dwc2_dma_phys | DWC2_BUS_ALIAS);
	wr(HCINT(DWC2_CH), 0x3FFF);

	uint32_t hcchar = rd(HCCHAR(DWC2_CH));
	hcchar &= ~(HCCHAR_MULTICNT_MASK | HCCHAR_CHENA | HCCHAR_CHDIS | HCCHAR_ODDFRM);
	hcchar |= (1u << HCCHAR_MULTICNT_SHIFT) | (odd_frame ? HCCHAR_ODDFRM : 0) | HCCHAR_CHENA;
	wr(HCCHAR(DWC2_CH), hcchar);

	uint32_t remaining = 0;
	int ret = dwc2_channel_wait(&remaining, pid);
	if (ret != USB_OK && ret != USB_EAGAIN) {
		*actual = 0;
		return ret;
	}
	if (in && xfer_len) {
		uint32_t got = xfer_len - remaining;
		dma_inval(span);
		memcpy(buffer, dwc2_dma, got);
		*actual = got;
	} else {
		*actual = (ret == USB_OK) ? xfer_len : 0;
	}
	return ret;
}

/*
 * chunk_msg -- one stage of a transfer on one endpoint. Split devices move
 * one packet at a time: SSPLIT until the hub ACKs it, then CSPLIT until the
 * data or a handshake comes back, retrying CSPLIT on NYET for up to 4
 * (micro)frames.
 */
static int dwc2_endpoint_xfer(usb_device_t* dev, uint8_t ep, int in, uint8_t eptype, uint16_t mps,
					 uint8_t* pid, uint8_t* buffer, uint32_t len, uint32_t* done_out) {
	uint32_t max_xfer = DWC2_PKT_MAX * mps;
	if (max_xfer > DWC2_DMA_SIZE)
		max_xfer = DWC2_DMA_SIZE;
	uint32_t num_packets = max_xfer / mps;
	max_xfer = num_packets * mps;

	dwc2_channel_setup(dev, ep, in, eptype, mps);
	dwc2_halt_timeout_us =
		(eptype == DWC2_EP_INTR) ? DWC2_HALT_TIMEOUT_INTR_US : DWC2_HALT_TIMEOUT_CTRL_US;
	bool do_split = dev->split;
	if (do_split) {
		num_packets = 1;
		max_xfer = mps;
	}

	uint32_t done = 0;
	int ret = USB_OK;
	bool complete_split = false;
	bool stop = false;
	uint32_t ssplit_frame = 0;
	do {
		uint32_t xfer_len = len - done;
		if (xfer_len > max_xfer)
			xfer_len = max_xfer;
		else if (xfer_len > mps)
			num_packets = (xfer_len + mps - 1) / mps;
		else
			num_packets = 1;

		if (do_split) {
			uint32_t hcsplt = rd(HCSPLT(DWC2_CH));
			wr(HCSPLT(DWC2_CH), complete_split ? (hcsplt | HCSPLT_COMPSPLT)
											   : (hcsplt & ~HCSPLT_COMPSPLT));
		}

		int odd_frame = 0;
		if (eptype == DWC2_EP_INTR && !(rd(HFNUM) & 1))
			odd_frame = 1;

		uint32_t actual = 0;
		ret = dwc2_channel_run(pid, in, buffer + done, num_packets, xfer_len, &actual, odd_frame);
		uint32_t hcint = rd(HCINT(DWC2_CH));

		if (complete_split) {
			stop = false;
			if (hcint & HCINT_NYET) {
				ret = USB_OK;
				uint32_t frame = rd(HFNUM) & HFNUM_MAX_FRNUM;
				if (((frame - ssplit_frame) & HFNUM_MAX_FRNUM) > 4)
					ret = USB_EAGAIN;
			} else {
				complete_split = false;
			}
		} else if (do_split) {
			if (hcint & HCINT_ACK) {
				ssplit_frame = rd(HFNUM) & HFNUM_MAX_FRNUM;
				ret = USB_OK;
				complete_split = true;
				continue;
			}
		}

		if (ret != USB_OK)
			break;
		if (actual < xfer_len)
			stop = true;
		done += actual;
	} while (((done < len) && !stop) || complete_split);

	wr(HCINTMSK(DWC2_CH), 0);
	wr(HCINT(DWC2_CH), 0xFFFFFFFF);
	*done_out = done;
	return ret;
}

/* NAKs on a control stage are retried for this long */
#define USB_CTRL_TIMEOUT_US 1000000

/*
 * ctrl_stage -- run one control stage to completion. A NAK leaves *pid at
 * the toggle the hardware reached, and the stage resumes after the bytes
 * already moved
 */
static int ctrl_stage(usb_device_t* dev, int in, uint8_t* pid, uint8_t* buf, uint32_t len,
					  uint32_t* total) {
	uint64_t start = dwc2_now_us();
	uint32_t acc = 0;
	for (;;) {
		uint32_t done = 0;
		int ret = dwc2_endpoint_xfer(dev, 0, in, DWC2_EP_CONTROL, dev->mps0, pid, buf + acc, len - acc, &done);
		acc += done;
		if (ret != USB_EAGAIN) {
			*total = acc;
			return ret;
		}
		if (dwc2_now_us() - start > USB_CTRL_TIMEOUT_US) {
			*total = acc;
			return USB_ETIME;
		}
		dwc2_delay_us(100);
	}
}

/**
 * @brief usb_control_msg -- SETUP, optional DATA, STATUS (USB 2.0 8.5.3)
 * @return bytes moved in the data stage, or a negative error
 */
int usb_control_msg(usb_device_t* dev, uint8_t request_type, uint8_t request, uint16_t value,
					uint16_t index, void* data, uint16_t length) {
	usb_setup_t setup;
	setup.bmRequestType = request_type;
	setup.bRequest = request;
	setup.wValue = value;
	setup.wIndex = index;
	setup.wLength = length;

	uint32_t done = 0;
	uint8_t pid = DWC2_PID_SETUP;
	int ret = ctrl_stage(dev, 0, &pid, (uint8_t*)&setup, sizeof(setup), &done);
	if (ret != USB_OK)
		return ret;

	uint32_t moved = 0;
	int status_in = 1; /* no data stage: the status stage is IN */
	if (length && data) {
		int in = (request_type & USB_DIR_IN) != 0;
		pid = DWC2_PID_DATA1;
		ret = ctrl_stage(dev, in, &pid, (uint8_t*)data, length, &moved);
		if (ret != USB_OK)
			return ret;
		status_in = !in;
	}

	pid = DWC2_PID_DATA1;
	ret = ctrl_stage(dev, status_in, &pid, (uint8_t*)&setup, 0, &done);
	if (ret != USB_OK)
		return ret;
	return (int)moved;
}

/**
 * @brief usb_interrupt_in -- one poll of an interrupt IN endpoint
 * @return bytes received, 0 if the device NAKed (nothing new), or a
 * negative error
 */
int usb_interrupt_in(usb_device_t* dev, uint8_t ep, uint16_t mps, void* data, int length) {
	uint8_t* pid = &dev->toggle_in[ep & 0xF];
	uint32_t done = 0;
	int ret = dwc2_endpoint_xfer(dev, ep, 1, DWC2_EP_INTR, mps, pid, (uint8_t*)data, (uint32_t)length, &done);
	if (ret == USB_EAGAIN)
		return 0;
	if (ret != USB_OK)
		return ret;
	return (int)done;
}
