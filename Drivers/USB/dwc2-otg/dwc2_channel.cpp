/**
* @file dwc2_channel.cpp
* 
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

#include "dwc2_reg.h"
#include <stdint.h>
#include "dwc2.h"
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <Mm/pmmngr.h>
#include <Drivers/uart.h>
#include "usb_desc.h"
#include "usb_hid.h"
#include <string.h>
#include <Mm/dma.h>
#include <Fs/Dev/devinput.h>
#include <timer.h>

static bool _channel_interrupt_occured = 0;
extern uint64_t dwc2_get_base();

void dwc2_reset_channel(dwc2_core_regs* regs, uint8_t ch) {
	uint32_t hcchar = dwc2_read((uint64_t)&regs->hc_regs[ch].hcchar);
	if (hcchar & DWC2_HCCHAR_CHEN) {
		hcchar |= DWC2_HCCHAR_CHDIS;
		hcchar &= ~DWC2_HCCHAR_EPDIR;
		dwc2_write((uint64_t)&regs->hc_regs[ch].hcchar, hcchar);

		uint32_t timeout = 10000;
		while ((dwc2_read((uint64_t)&regs->hc_regs[ch].hcchar & DWC2_HCCHAR_CHEN)) && --timeout);

		if (!timeout)
			UARTDebugOut("[dwc2_otg]: channel halt timeout \r\n");
	}

	dwc2_write((uint64_t)&regs->hc_regs[ch].hcint, 0x3FFF);
	dwc2_write((uint64_t)&regs->hc_regs[ch].hcintmsk, 0);
	dwc2_write((uint64_t)&regs->hc_regs[ch].hcsplt, 0);
	dwc2_write((uint64_t)&regs->hc_regs[ch].hctsiz, 0);
	dwc2_write((uint64_t)&regs->hc_regs[ch].hcdma, 0);
	dsb_sy_barrier();
	AA64SleepMS(1);

	uint32_t haint = dwc2_read((uint64_t)&regs->host_regs.haintmsk);
	haint &= ~(1u << ch);
	dwc2_write((uint64_t)&regs->host_regs.haintmsk, haint);
	dwc2_write((uint64_t)&regs->host_regs.haint, (1u << ch));

	dsb_sy_barrier();
}

#define CONVERT_TO_BUS(phys) (phys | 0xC0000000UL)
#define HCSPLT_PRTADDR(x)  (((x) & 0x7F) << DWC2_HCSPLT_PRTADDR_OFFSET)
#define HCSPLT_HUBADDR(x)  (((x) & 0x7F) << DWC2_HCSPLT_HUBADDR_OFFSET)
#define HCSPLT_XACTPOS(x)  (((x) & 0x3) << DWC2_HCSPLT_XACTPOS_OFFSET)
#define HCSPLT_COMPSPLT  (1U <<  DWC2_HCSPLT_COMPSPLT_OFFSET)
#define HCSPLT_SPLTENA   (1U << DWC2_HCSPLT_SPLTENA_OFFSET)
#define HCSPLT_XACTPOS_ALL 0x3

/**
 * @brief dwc2_start_channel -- allocates a channel 
 */
void dwc2_start_channel(struct dwc2_core_regs* regs, dwc2_usb_endpoint_t* ep, uint8_t ch, uint8_t is_in, uint8_t ep_type, uint8_t pid,
	void* dma, uint32_t length, uint32_t pack_count, uint8_t is_odd_frame) {
	dwc2_core_regs* reg2 = (dwc2_core_regs*)0x3F980000;

	dwc2_reset_channel(regs, ch);
	dwc2_flush_tx_fifo(regs, 0x10);
	dwc2_flush_rx_fifo(regs);

	struct dwc2_hc_regs* hc = &regs->hc_regs[ch];

	uint32_t hcchar_ = dwc2_read((uint64_t)&hc->hcchar);
	
	//if (hcchar_ & DWC2_HCCHAR_CHEN) {
	//	UARTDebugOut("CHnnel : %d, host charracter is already enabled \r\n");
	//	
	//	hcchar_ |= DWC2_HCCHAR_CHDIS;
	//	hcchar_ &= ~(DWC2_HCCHAR_CHEN);
	//	dwc2_write((uint64_t)&hc->hcchar, hcchar_);

	//	uint32_t hcintmsk_ = dwc2_read((uint64_t)&hc->hcintmsk);
	//	hcintmsk_ |= DWC2_HCINTMSK_CHHLTD;
	//	dwc2_write((uint64_t)&hc->hcintmsk, hcintmsk_);
	//}
	//else if (hcchar_ & DWC2_HCCHAR_CHDIS) {
	//	UARTDebugOut("Chnnel : %d is disabled \r\n");
	//}

	/** clear all pending interrupts */
	dwc2_write((uint64_t)&hc->hcint, 0xFFFFFFFF);

	/** now set transfer size **/
	dwc2_write((uint64_t)&hc->hctsiz, ((pid << 29) & (3 << 29)) | ((pack_count << 19) & (0x3FF << 19)) | (length & 0x7ffff));

	/** now set the dma address **/
	/*aa64_data_cache_clean_range(dma, length);
	dsb_ish();
	dsb_sy_barrier();*/
	dwc2_write((uint64_t)&hc->hcdma, (uint32_t)CONVERT_TO_BUS((uint64_t)dma));
	dwc2_write((uint64_t)&hc->hcdmab, ((uint32_t)CONVERT_TO_BUS((uint64_t)dma) >> 32) & UINT32_MAX);

	/* set the split control to zero*/
	if (!ep->split_enable)
		dwc2_write((uint64_t)&hc->hcsplt, 0);
	else {
		/* enable split transfer for translation between different
		 * speed mode devices 
		 */
		dwc2_write((uint64_t)&hc->hcsplt, 0);
		dwc2_hcsplit_t* split = (dwc2_hcsplit_t*)&hc->hcsplt;
		split->hubaddr = ep->hub_address;
		split->prtaddr = ep->hub_port;
		split->xactpos = 0x3;
		split->compsplt = 0;
		split->spltena = 1;
		dwc2_write((uint64_t)&hc->hcsplt, HCSPLT_SPLTENA | HCSPLT_XACTPOS(0x3) | HCSPLT_HUBADDR(ep->hub_address)|
		HCSPLT_PRTADDR(ep->hub_port));
	}

	uint8_t ls_bit = (ep->speed == USBSpeedLow) ? 1 : 0;

	uint32_t hfnum = dwc2_read((uint64_t)&regs->host_regs.hdnum);
	uint32_t frmnum = hfnum & 0xFFFF;
	uint32_t odd = (frmnum & 1) ? 0 : 1;

	uint32_t hcchar = ((ep->max_packet_sz & 0x7ff) << 0) |
		(ep->ep_num << 11) |
		(is_in << 15) |
		(ls_bit << 17) |
		(ep->type << 18) |
		(1 << 20) |
		(ep->dev_address << 22) |
		(odd << 29);
	//dwc2_write((uint64_t)&hc->hcchar, hcchar);
	/*
	 DWC2_HOST_CHAN_INT_XFER_COMPLETE |
	 DWC2_HOST_CHAN_INT_HALTED |
	 DWC2_HOST_CHAN_INT_ERROR_MASK */
	dwc2_write((uint64_t)&hc->hcintmsk, DWC2_HCINTMSK_XFERCOMPL |
	DWC2_HCINT_CHHLTD | DWC2_HCINT_AHBERR | DWC2_HCINT_STALL | 
	 DWC2_HCINT_XACTERR | DWC2_HCINT_BBLERR | DWC2_HCINT_FRMOVRUN | DWC2_HCINT_DATATGLERR);

	uint32_t haintmsk = dwc2_read((uint64_t)&regs->host_regs.haintmsk);
	haintmsk |= (1U << ch);
	dwc2_write((uint64_t)&regs->host_regs.haintmsk, haintmsk);

	/*uint32_t hcintmsk = (1ULL << 0) |
		(1ULL << 1) |
		(1ULL << 2) |
		(1ULL << 3) |
		(1ULL << 4) |
		(1ULL << 5) | 
		(1ULL << 7) |
		(1ULL << 8) |
		(1ULL << 9) |
		(1ULL << 10);

	dwc2_write((uint64_t)&hc->hcintmsk, hcintmsk);*/

	hcchar |= DWC2_HCCHAR_CHEN;
	hcchar &= ~DWC2_HCCHAR_CHDIS;

	dwc2_write((uint64_t)&hc->hcchar, hcchar);

	
	dsb_sy_barrier();
	isb_flush();
}

/**
 * @brief dwc2_channel_do_csplit -- complete the split transaction here
 * @param regs -- pointer to dwc2 core registers
 * @param ep -- pointer to usb endpoint
 * @param ch -- channel number
 */
void dwc2_channel_do_csplit(struct dwc2_core_regs* regs, dwc2_usb_endpoint_t* ep, uint8_t ch, uint8_t is_in, uint8_t ep_type, uint8_t pid,
	void* dma, uint32_t length, uint32_t pack_count, uint8_t is_odd_frame) {
	
	dwc2_reset_channel(regs, ch);
	/*dwc2_flush_tx_fifo(regs, 0x10);
	dwc2_flush_rx_fifo(regs);*/

	struct dwc2_hc_regs* hc = &regs->hc_regs[ch];

	uint32_t hcchar_ = dwc2_read((uint64_t)&hc->hcchar);


	/** clear all pending interrupts */
	dwc2_write((uint64_t)&hc->hcint, 0xFFFFFFFF);

	/** now set transfer size **/
	dwc2_write((uint64_t)&hc->hctsiz, ((pid << 29) & (3 << 29)) | ((pack_count << 19) & (0x3FF << 19)) | (length & 0x7ffff));

	/** now set the dma address **/
	/*aa64_data_cache_clean_range(dma, length);
	dsb_ish();
	dsb_sy_barrier();*/
	dwc2_write((uint64_t)&hc->hcdma, (uint32_t)CONVERT_TO_BUS((uint64_t)dma));
	dwc2_write((uint64_t)&hc->hcdmab, ((uint32_t)CONVERT_TO_BUS((uint64_t)dma) >> 32) & UINT32_MAX);

	/* set the split control to zero*/
	if (!ep->split_enable)
		dwc2_write((uint64_t)&hc->hcsplt, 0);
	else {
		/* enable split transfer for translation between different
		 * speed mode devices
		 */
		dwc2_write((uint64_t)&hc->hcsplt, HCSPLT_SPLTENA | HCSPLT_COMPSPLT | HCSPLT_XACTPOS(0x3) | HCSPLT_HUBADDR(ep->hub_address) |
			HCSPLT_PRTADDR(ep->hub_port));
	}

	uint8_t ls_bit = (ep->speed == USBSpeedLow) ? 1 : 0;

	uint32_t hfnum = dwc2_read((uint64_t)&regs->host_regs.hdnum);
	uint32_t frmnum = hfnum & 0xFFFF;
	uint32_t odd = (frmnum & 1) ? 0 : 1;

	uint32_t hcchar = ((ep->max_packet_sz & 0x7ff) << 0) |
		(ep->ep_num << 11) |
		(is_in << 15) |
		(ls_bit << 17) |
		(ep->type << 18) |
		(1 << 20) |
		(ep->dev_address << 22) |
		(odd << 29);
	
	dwc2_write((uint64_t)&hc->hcintmsk, DWC2_HCINTMSK_XFERCOMPL |
		DWC2_HCINT_CHHLTD | DWC2_HCINT_AHBERR | DWC2_HCINT_STALL |
		DWC2_HCINT_XACTERR | DWC2_HCINT_BBLERR | DWC2_HCINT_FRMOVRUN | DWC2_HCINT_DATATGLERR);

	uint32_t haintmsk = dwc2_read((uint64_t)&regs->host_regs.haintmsk);
	haintmsk |= (1U << ch);
	dwc2_write((uint64_t)&regs->host_regs.haintmsk, haintmsk);


	hcchar |= DWC2_HCCHAR_CHEN;
	hcchar &= ~DWC2_HCCHAR_CHDIS;

	dwc2_write((uint64_t)&hc->hcchar, hcchar);


	dsb_sy_barrier();
	isb_flush();
}

void dwc2_start_transaction(struct dwc2_core_regs* regs, uint8_t ch, void* dma, uint32_t length, uint32_t pack_count, uint8_t pid,
	uint8_t is_odd_frame) {
	dwc2_hc_regs* hc = &regs->hc_regs[ch];

	aa64_data_cache_clean_range(dma, length);
	dsb_ish();
	dsb_sy_barrier();
	dwc2_write((uint64_t)&hc->hcdma, (uint32_t)dma);

	/** now set transfer size **/
	dwc2_write((uint64_t)&hc->hctsiz, ((pid << 29) & (3<<29)) | ((pack_count << 19) & (0x3FF<<19))| (length & 0x7ffff));


	uint32_t hcchar = dwc2_read((uint64_t)&hc->hcchar);
	hcchar &= ~(1 << 31);
	hcchar &= ~(1 << 30);
	hcchar &= ~(1 << 29);

	if (is_odd_frame) {
		hcchar |= (1 << 29);
	}

	uint32_t hprt = dwc2_read((uint64_t)&regs->hprt0);
	if ((hprt & (1 << 12)))
		UARTDebugOut("dwc2 port is powered already \r\n");
	else {
		hprt |= (1 << 12);
		dwc2_write((uint64_t)&regs->hprt0, hprt);
		
	}
	AA64SleepMS(500);
	dwc2_write(dwc2_get_base() + 0x088, 1);

	/** now enable the channel **/
//	hcchar &= ~(1ULL << 30); //clear disable bit
	hcchar |= (1u << 20);
	hcchar |= (1U << 31); 
	dwc2_write((uint64_t)&hc->hcchar, hcchar);

	uint32_t val = dwc2_read((uint64_t)&hc->hcchar);
}

int dwc2_wait_channel(struct dwc2_core_regs* regs, int ch) {
	struct dwc2_hc_regs* hc = &regs->hc_regs[ch];


	while (1) {
		if (_channel_interrupt_occured) {
			_channel_interrupt_occured = 0;
			break;
		}
	}
	return 0;
}

int dwc2_wait_channel_look_bit(struct dwc2_core_regs* regs, int ch, int bit) {
	bool _found = 0;
	uint32_t hcint_ = 0;
	int timeout = 5000000;
	while (timeout--) {
		uint32_t hcint = dwc2_read((uint64_t)&regs->hc_regs[ch].hcint);
		uint32_t hcintmsk = dwc2_read((uint64_t)&regs->hc_regs[ch].hcintmsk);
		uint32_t hctsz = dwc2_read((uint64_t)&regs->hc_regs[ch].hctsiz);
		uint32_t active = hcint & hcintmsk;
		hcint_ = hcint;
		uint8_t pid = USB_PID_DATA0;
		/** W1C**/
		dwc2_write((uint64_t)&regs->hc_regs[ch].hcint, hcint);

		if (hcint & bit) {
			_found = 1;
			break;
		}

	}
	uint32_t haint = dwc2_read((uint64_t)&regs->host_regs.haint);
	haint |= (1u << ch);
	dwc2_write((uint64_t)&regs->host_regs.haint, haint);
	if (!_found)
		return hcint_;
	else
		return 0;
}

bool dwc2_control_transfer(struct dwc2_core_regs* regs, dwc2_usb_endpoint_t* ep, uint8_t bmRequestType, uint8_t b_request,
	uint16_t wValue, uint16_t wIndex, void* data, uint16_t wLength) {

	uint64_t setup_phys = 0;
	usb_setup_packet_t* setup = (usb_setup_packet_t*)AuDMAGClassAlloc(dwc2_get_dma_class(), sizeof(usb_setup_packet_t), &setup_phys); //P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	//aa64_data_cache_clean_range(setup, 4096);
	setup->bmRequestType = bmRequestType;
	setup->bmRequest = b_request;
	setup->wValue = wValue;
	setup->wIndex = wIndex;
	setup->wLength = wLength;
	aa64_data_cache_clean_range(setup,4096);
	uint8_t ch = ep->ch;
	
	//aa64_dc_cvac_range(dmaphys, 4096);
	dwc2_write((uint64_t)&regs->gahbcfg, 0x27);
	dwc2_start_channel(regs, ep, ch, 0, 0, USB_PID_SETUP,(void*)setup_phys, 8, 1, 0);
	int result = dwc2_wait_channel(regs, ch);
	if (result < 0) {
		UARTDebugOut("[dwc2_otg]: failed in channel \r\n");
		return 1;
	}


	if (ep->split_enable) {
		AA64SleepMS(20);
		dwc2_channel_do_csplit(regs, ep, ch, 0, 0, USB_PID_SETUP, (void*)setup_phys, 8, 1, 0);
		int result = dwc2_wait_channel(regs, ch);
		if (result < 0) {
			UARTDebugOut("[dwc2_otg]: failed in channel \r\n");
			return 1;
		}
	}
	
	/** Handle data stage **/
	if (wLength > 0) {
		uint8_t is_in = (bmRequestType & 0x80) ? 1 : 0;
		void* dma_buf = data;
		uint64_t data_ptr = (uint64_t)dma_buf;
		int need_bounce = ((uint32_t)data & 0x3) != 0;
		uint16_t remaining = wLength;
		uint8_t pid = USB_PID_DATA;
		uint16_t chunk = 0;
		while (remaining > 0) {
			chunk = (remaining > ep->max_packet_sz) ? ep->max_packet_sz : remaining;

			if (need_bounce) {
				/* need to copy the buffer to another buffer, which is 4-byte aligned */
			}

			/* should always be 1 for Low Speed devices */
			uint32_t packet_count = (wLength + ep->max_packet_sz - 1) / ep->max_packet_sz;

			aa64_data_cache_clean_range(dma_buf, wLength);

			dwc2_write((uint64_t)&regs->gahbcfg, 0x27);
			dwc2_start_channel(regs, ep, ch, is_in, 0, pid, dma_buf, chunk, packet_count, 0);


			result = dwc2_wait_channel(regs, ch);
			if (result < 0) {
				UARTDebugOut("[dwc2_otg]: failed in channel \r\n");
				return 1;
			}

			if (ep->split_enable) {
				AA64SleepMS(20);
				dwc2_channel_do_csplit(regs, ep, ch, is_in, 0, pid, dma_buf,chunk, packet_count, 0);
				int result = dwc2_wait_channel(regs, ch);
				if (result < 0) {
					UARTDebugOut("[dwc2_otg]: failed in channel csplit\r\n");
					return 1;
				}
			}
			aa64_data_cache_clean_range(dma_buf,4096);

			data_ptr += chunk;
			dma_buf = (void*)data_ptr;
			remaining -= chunk;
			pid = (pid == USB_PID_DATA0) ? USB_PID_DATA : USB_PID_DATA0;
		}
	}

	/** setup the status stage **/
	uint8_t status_is_in;

	if (wLength == 0)
		status_is_in = 1;
	else
		status_is_in = (bmRequestType & 0x80) ? 0 : 1;

	dwc2_write((uint64_t)&regs->gahbcfg, 0x27);
	dwc2_start_channel(regs, ep, ch, status_is_in, 0, USB_PID_DATA,(void*)setup_phys, 0,1,0);
	//dwc2_start_transaction(regs, ch, 0, 0, 1, 2, 0);

	result = dwc2_wait_channel(regs, ch);
	if (result < 0) {
		UARTDebugOut("[dwc2_otg]: status stage failed \r\n");
		return 1;
	}

	if (ep->split_enable) {
		AA64SleepMS(20);
		dwc2_channel_do_csplit(regs, ep, ch, status_is_in, 0, USB_PID_DATA, (void*)setup_phys, 0, 1, 0);
		int result = dwc2_wait_channel(regs, ch);
		if (result < 0) {
			UARTDebugOut("[dwc2_otg]: failed in channel csplit\r\n");
			return 1;
		}
	}

	AuDMAGClassFree(dwc2_get_dma_class(), setup, setup_phys, sizeof(usb_setup_packet_t));
	//AuPmmngrReleasePage((uint64_t)V2P((uint64_t)setup));

	return 0;
	/** free up the channel **/
}

extern uint8_t port_changed;
/**
 * @brief dwc2_handle_channel_interrupt -- handle channel
 * interrupts here
 * @param regs -- Pointer to dwc2 core registers
 * @param ch -- channel numbers
 */
void dwc2_handle_channel_interrupt(dwc2_core_regs* regs, int ch) {
	uint32_t hcint = dwc2_read((uint64_t)&regs->hc_regs[ch].hcint);
	uint32_t hcintmsk = dwc2_read((uint64_t)&regs->hc_regs[ch].hcintmsk);
	uint32_t hctsz = dwc2_read((uint64_t)&regs->hc_regs[ch].hctsiz);
	uint32_t active = hcint ;

	uint8_t pid = USB_PID_DATA0;
	/** W1C**/
	dwc2_write((uint64_t)&regs->hc_regs[ch].hcint, hcint);

	if (ch == 2) {
	
	}

	if (active & DWC2_HCINT_CHHLTD) {
		if (active & DWC2_HCINT_XFERCOMP) {
			if (ch == 2) {
				uint8_t* buff = (uint8_t*)usb_hid_get_interrupt_buf();
				aa64_data_cache_clean_range(buff, 32);

				int8_t dx = (int8_t)buff[1];
				int8_t dy = (int8_t)buff[3];
				uint8_t button = (uint8_t)buff[0];
				if (dx && buff[0] & (1 << 4))
					dx = dx - 0x100;

				if (dy && buff[0] & (1 << 5))
					dy = dy - 0x100;

				USBHIDSetMouse(dx, dy, button);
			
				uint8_t mpid = usb_hid_get_dev()->ep.next_pid;
				mpid = (mpid == USB_PID_DATA) ? USB_PID_DATA0 : USB_PID_DATA;
				usb_hid_get_dev()->ep.next_pid = mpid;
				
				dwc2_interrupt_transfer(regs, &usb_hid_get_dev()->ep, (void*)usb_hid_get_physical(), usb_hid_get_dev()->ep.ch, 1, mpid);
				//USBHIDSetNAKNormal(false, true);
			}
			//UARTDebugOut("[dwc2-otg]: channel transfer completed \r\n");
			//pid = (hctsz >> 29) & 0x3;
			//uint8_t* buf = (uint8_t*)root_hub_get_interrupt_buffer();
			//uint8_t bitmap = buf[0];
			//for (int p = 1; p <= 4; p++) {
			//	if (bitmap & (1 << p)) {
			//		UARTDebugOut("[dwc2-otg]: root hub port changed : %d bitmap: %x\r\n", p, bitmap);
			//		//root_hub_handle_port_change(regs, p);
			//		port_changed = p;
			//		memset(buf, 0, 4096);
			//	}
			//}
		}
		else if (active & DWC2_HCINT_NAK) {
			//UARTDebugOut("[dwc2-otg]: device not ready -- retry later \r\n");
		}
		else if (active & DWC2_HCINT_NYET) {
			UARTDebugOut("[dwc2-otg]: hs split not ready -- retry later \r\n");
		}
		else if (active & DWC2_HCINT_XACTERR) {
			UARTDebugOut("[dwc2-otg]: transaction error -- retry upto 3 times \r\n");
		}
		else if (active & DWC2_HCINTMSK_BBLERR) {
			UARTDebugOut("[dwc2-otg]: babble -- device sent too much data \r\n");
		}
		else if (active & DWC2_HCINT_AHBERR) {
			UARTDebugOut("[dwc2-otg]: ahb buss error \r\n");
		}
		else if (active & DWC2_HCINT_DATATGLERR) {
			pid = (hctsz >> 29) & 0x3;
			UARTDebugOut("[dwc2-otg]: data toggle mismatch %d \r\n", pid);
			if (ch == 2) {
				pid = (pid == USB_PID_DATA0) ? USB_PID_DATA : USB_PID_DATA0;
				usb_hid_get_dev()->ep.next_pid = pid;
				dwc2_interrupt_transfer(regs, &usb_hid_get_dev()->ep, (void*)usb_hid_get_physical(), usb_hid_get_dev()->ep.ch, 1, pid);
			}
		}
		else if (active & DWC2_HCINT_STALL) {
			UARTDebugOut("[dwc2-otg]: channel stalled \r\n");
			UARTDebugOut("[dwc2-otg]: hcint : %x \r\n", hcint);
			UARTDebugOut("[dwc2-otg]: hctsz : %x \r\n", hctsz);
		}

		if (hcint & DWC2_HCINTMSK_ACK) {
			if (ch == 2) {
				AA64SleepMS(1);
				dwc2_interrupt_transfer_csplit(regs, &usb_hid_get_dev()->ep, (void*)usb_hid_get_physical(), usb_hid_get_dev()->ep.ch, 1,
					usb_hid_get_dev()->ep.next_pid);
				usb_hid_get_dev()->stage = TRANSFER_CSPLT;
			}
		}

		if (hcint & DWC2_HCINTMSK_NAK) {
			if (ch == 2) {
				/*uint8_t mpid = usb_hid_get_dev()->ep.next_pid;
				dwc2_interrupt_transfer(regs, &usb_hid_get_dev()->ep, (void*)usb_hid_get_physical(), usb_hid_get_dev()->ep.ch, 1, mpid);*/
				//USBHIDSetNAKNormal(true, false);
				int idx = AuroraTimerStart(2, HIDRequestInterrupt_NAK, NULL, -1);
			}
		}

		if (hcint & DWC2_HCINTMSK_NYET) {
			if (ch == 2) {
				UARTDebugOut("NYET \r\n");
				dwc2_interrupt_transfer_csplit(regs, &usb_hid_get_dev()->ep, (void*)usb_hid_get_physical(), usb_hid_get_dev()->ep.ch, 1,
					usb_hid_get_dev()->ep.next_pid);
				usb_hid_get_dev()->stage = TRANSFER_CSPLT;
			}
		}
	}

	uint32_t haint = dwc2_read((uint64_t)&regs->host_regs.haint);
	haint |= (1u << ch);
	dwc2_write((uint64_t)&regs->host_regs.haint, haint);
	_channel_interrupt_occured = 1;
	//root_hub_transfer_int(regs, pid);
}

void dwc2_interrupt_transfer(dwc2_core_regs* regs, dwc2_usb_endpoint_t* ep, void* intbuf, uint8_t ch, uint32_t odd,uint8_t pid) {
	dwc2_write((uint64_t)&regs->gahbcfg, 0x27);
	dwc2_start_channel(regs, ep, ch, 1,0x3,pid, intbuf,ep->max_packet_sz, 1, odd);
}

void dwc2_interrupt_transfer_csplit(dwc2_core_regs* regs, dwc2_usb_endpoint_t* ep, void* intbuf, uint8_t ch, uint32_t odd, uint8_t pid) {
	dwc2_write((uint64_t)&regs->gahbcfg, 0x27);
	dwc2_channel_do_csplit(regs, ep, ch, 1, 0x3, pid, intbuf, ep->max_packet_sz, 1, odd);
}

