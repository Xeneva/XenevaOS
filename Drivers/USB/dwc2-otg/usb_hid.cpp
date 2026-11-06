/**
* @file usb_hid.cpp
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

#include "usb_hid.h"
#include "dwc2.h"
#include "dwc2_reg.h"
#include "dwc2_usbdev.h"
#include "usb_desc.h"
#include <Mm/dma.h>
#include <Drivers/uart.h>
#include <_null.h>
#include <Fs/Dev/devinput.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <string.h>
#include <timer.h>

static dwc2_usb_device* mouse_dev;
static uint64_t intphysical;
static void* intbuff_;
static int32_t _mx;
static int32_t _my;
static bool _is_nak;
static bool _is_normal;

void USBHidInitialize(dwc2_core_regs* regs, dwc2_usb_device* dev) {

	usb_config_desc_t* config = (usb_config_desc_t*)dev->config_desc;
	usb_if_desc_t* interface_desc = raw_offset<usb_if_desc_t*>(config, config->bLength);
	usb_ep_desc_t* endp = raw_offset<usb_ep_desc_t*>(interface_desc, interface_desc->bLength);
	while (raw_diff(endp, config) < config->wTotalLength) {
		if ((endp->bmAttributes & 0x3) == EP_INTERRUPT && (endp->bEndpointAddress & 0x80)) {
			UARTDebugOut("[USB-HID]: Interrupt IN found \r\n");
			UARTDebugOut("[USB-HID]: address : %d \r\n", endp->bEndpointAddress);
			UARTDebugOut("[USB-HID]: int ep num : %d \r\n", (endp->bEndpointAddress & 0xF));
			UARTDebugOut("[USB-HID]: interval : %d \r\n", endp->bInterval);
			UARTDebugOut("[USB-HID]: Max Packet SZ : %d \r\n", endp->wMaxPacketSize);
			dev->ep.interval = endp->bInterval;
			break;
		}
		endp = raw_offset<usb_ep_desc_t*>(endp, endp->bLength);
	}

	uint64_t hid_phys = 0;
	void* hid = AuDMAGClassAlloc(dwc2_get_dma_class(), sizeof(usb_hid_desc_t), &hid_phys);

	if (dwc2_control_transfer(regs, &dev->ep, 0x81, 0x06, 0x2100, 0x0000, (void*)hid_phys, 9)) {
		UARTDebugOut("[usb hid]: failed to get hid descriptor \r\n");
		return;
	}

	usb_hid_desc_t* hiddesc = (usb_hid_desc_t*)hid;
	UARTDebugOut("hid num descriptor : %d \r\n", hiddesc->bNumDescriptors);
	UARTDebugOut("report len : %d \r\n", hiddesc->wDescriptorLength);
	
	uint16_t report_len = hiddesc->wDescriptorLength;

	uint64_t hid_report_phys = 0;
	void* report_desc = AuDMAGClassAlloc(dwc2_get_dma_class(), report_len, &hid_report_phys);
	if (dwc2_control_transfer(regs, &dev->ep, 0x81, 0x06, 0x2200, 0x0000, (void*)hid_report_phys, report_len)) {
		UARTDebugOut("[usb hid]: failed to get report descriptor \r\n");
		return;
	}

	UARTDebugOut("[USB HID]: Setting simple boot protocol \r\n");
	if (dwc2_control_transfer(regs, &dev->ep, 0x21, 0x0B, 0x0000, 0x0000, NULL, 0)) {
		UARTDebugOut("failed to set boot protocol \r\n");
		return;
	}

	// set idle rate
	UARTDebugOut("[USB HID]: Setting IDLE rate \r\n");
	if (dwc2_control_transfer(regs, &dev->ep, 0x21, 0x0A, 0x0000, 0x0000, NULL, 0)) {
		UARTDebugOut("failed to set boot protocol \r\n");
		return;
	}

	UARTDebugOut("IDLE rate setupped \r\n");

	uint64_t int_phys = 0;
	mouse_dev = dev;
	void* inbuf = AuDMAGClassAlloc(dwc2_get_dma_class(), 32, &int_phys);
	intphysical = int_phys;
	intbuff_ = inbuf;

	_mx = 0;
	_my = 0;

	mouse_dev->ep.ch = 2;
	mouse_dev->ep.ep_num = 1;
	mouse_dev->ep.dir = EP_IN;
	mouse_dev->ep.next_pid = USB_PID_DATA;
	mouse_dev->ep.max_packet_sz = 8;
	mouse_dev->stage = TRANSFER_SSPLT;

	UARTDebugOut("mpid0 : %d \r\n", mouse_dev->ep.next_pid);

	/** do the first transfer in polling mode **/
	mask_irqs();
	dwc2_interrupt_transfer(regs, &mouse_dev->ep, (void*)intphysical, mouse_dev->ep.ch, 1, USB_PID_DATA);
	int result = dwc2_wait_channel_look_bit(regs, mouse_dev->ep.ch, DWC2_HCINT_ACK);
	if (result) {
		UARTDebugOut("failed to get ack bit \r\n");
	}

	dwc2_interrupt_transfer_csplit(regs, &mouse_dev->ep, (void*)intphysical, mouse_dev->ep.ch, 1, USB_PID_DATA);
	result = -1;
	result = dwc2_wait_channel_look_bit(regs, mouse_dev->ep.ch, DWC2_HCINT_XFERCOMP);
	if (result) {
		UARTDebugOut("failed to get ack bit \r\n");
	}

	UARTDebugOut("xfer received \r\n");
	
	dev->kernelTimerHandle = -1;
	enable_irqs();

	uint8_t mpid = mouse_dev->ep.next_pid;
	mpid = (mpid == USB_PID_DATA) ? USB_PID_DATA0 : USB_PID_DATA;
	mouse_dev->ep.next_pid = mpid;
	UARTDebugOut("mpid1 : %d \r\n", mouse_dev->ep.next_pid);
	dwc2_interrupt_transfer(regs, &mouse_dev->ep, (void*)intphysical, mouse_dev->ep.ch, 1, mpid);


	UARTDebugOut("[USB HID]: initialized \r\n");

}

uint64_t usb_hid_get_physical() {
	return intphysical;
}

void* usb_hid_get_interrupt_buf() {
	return intbuff_;
}

dwc2_usb_device* usb_hid_get_dev() {
	return mouse_dev;
}


void HIDRequestInterrupt(void* param) {
	if (_is_normal) {
		//memset((void*)intbuff_, 0, 32);
		//dwc2_core_regs* regs = dwc2_get_core_regs();
		//uint8_t mpid = mouse_dev->ep.next_pid;
		//mpid = (mpid == USB_PID_DATA) ? USB_PID_DATA0 : USB_PID_DATA;
		//mouse_dev->ep.next_pid = mpid;
		//AA64SleepMS(4);
		//dwc2_interrupt_transfer(regs, &mouse_dev->ep, (void*)intphysical, mouse_dev->ep.ch, 1, mpid);
		////UARTDebugOut("xfer completed mpid : %d, core : %x \r\n", mpid, regs);
		//mouse_dev->stage = TRANSFER_SSPLT;
		_is_normal = 0;
	}
	else if (_is_nak) {
		dwc2_core_regs* regs = dwc2_get_core_regs();
		uint8_t mpid = mouse_dev->ep.next_pid;
		AA64SleepMS(4);
		dwc2_interrupt_transfer(regs, &mouse_dev->ep, (void*)intphysical, mouse_dev->ep.ch, 1, mpid);
		_is_nak = 0;
	}
}

void HIDRequestInterrupt_NAK(void* param) {
	dwc2_core_regs* regs = dwc2_get_core_regs();
	uint8_t mpid = mouse_dev->ep.next_pid;
	dwc2_interrupt_transfer(regs, &mouse_dev->ep, (void*)intphysical, mouse_dev->ep.ch, 1, mpid);
	//UARTDebugOut("After interrupt NAK %d\r\n", mouse_dev->ep.next_pid);
	mouse_dev->stage = TRANSFER_SSPLT;
}

void HIDRequestCSplit(void* param) {
	dwc2_core_regs* regs = dwc2_get_core_regs();
	uint8_t mpid = mouse_dev->ep.next_pid;
	dwc2_interrupt_transfer_csplit(regs, &mouse_dev->ep, (void*)intphysical, mouse_dev->ep.ch, 1, mpid);
	mouse_dev->stage = TRANSFER_CSPLT;
}


void USBHIDStartInterrupt(dwc2_core_regs* regs) {
	_is_normal = 1;
	AuroraTimerStartPeriodic(10, HIDRequestInterrupt, NULL);  //6
}


void USBHIDSetMouse(int dx, int dy, int button) {
	_mx += dx / 2;
	_my += dy / 2;

	if (_mx < 0)
		_mx = 0;

	if (_my < 0)
		_my = 0;


	AuInputMessage newmsg;
	memset(&newmsg, 0, sizeof(AuInputMessage));
	newmsg.type = AU_INPUT_MOUSE;
	newmsg.xpos = _mx;
	newmsg.ypos = _my;

	if (button & 0x1)
		newmsg.button_state = 0x1;

	if (button & 0x2)
		newmsg.button_state = 0x2;


	//UARTDebugOut("Mouse X: %d, MouseY: %d \r\n", _mx, _my);
	AuDevWriteMice(&newmsg);
}

void USBHIDSetNAKNormal(bool nak, bool normal) {
	_is_nak = nak;
	_is_normal = normal;
}