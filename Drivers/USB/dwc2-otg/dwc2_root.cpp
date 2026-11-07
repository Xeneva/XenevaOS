/**
* BSD 2-Clause License
*
* @file dwc2_root.cpp
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

#include "dwc2.h"
#include "dwc2_reg.h"
#include <Mm/pmmngr.h>
#include <Drivers/uart.h>
#include <string.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <_null.h>
#include "dwc2_usbdev.h"

#include "usb_desc.h"



#define PORT_STATUS_CONNECTION (1<<0)
#define PORT_CHANGE_CONNECTION (1<<0)



void usb_hub_get_port_status(struct dwc2_core_regs* regs, uint8_t port, void* dma, uint8_t address, uint8_t speed) {
	dwc2_usb_endpoint_t ep;
	ep.dev_address = address; //0 for very beginning 
	ep.ep_num = 0; //control ep
	ep.type = EP_CONTROL; //control
	ep.dir = EP_BIDIR;
	ep.speed = speed;
	ep.max_packet_sz = 8;

	dwc2_control_transfer(regs, &ep, 0xA3, 0x00, 0x0000, port, dma, 4);

}

static bool _all_init = false;
void* intb;
static uint8_t speed_;
static dwc2_usb_endpoint_t ep;
uint8_t port_changed;
/**
 * dwc2_enumerate_root_device -- enumerate the root device
 * @param regs -- pointer to system regs
 */
void dwc2_enumerate_root_device(struct dwc2_core_regs* regs) {
	uint32_t hprt = dwc2_read((uint64_t)&regs->hprt0);
	uint8_t speed = DWC2_GET_PORT_SPEED(hprt);
	if (hprt & (1 << 12))
		UARTDebugOut("dwc2 port is powered already \r\n");
	else {
		hprt |= (1 << 12);
		dwc2_write((uint64_t)&regs->hprt0, hprt);
	}

	if (dwc2_usbdev_initialize(regs, 0, 0, 0, speed)) {
		UARTDebugOut("[dwc2-otg]: failed to initialize usb device at root port \r\n");
	}
	UARTDebugOut("enumeration successfull \r\n");
	//speed_ = speed;
	//port_changed = 0;
	//ep.dev_address = 0; //0 for very beginning 
	//ep.ep_num = 0; //control ep
	//ep.type = EP_CONTROL; //control
	//ep.dir = EP_BIDIR;
	//ep.speed = speed;
	//ep.max_packet_sz = 8;

	//void* desc = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	//memset(desc, 0, 4096);
	//UARTDebugOut("Desc addr : %x \r\n", desc);
	//dwc2_control_transfer(regs, &ep, 0x80, 0x06, 0x0100, 0, desc, 0x0008);

	//usb_dev_desc_t* desc_ = (usb_dev_desc_t*)desc;
	//UARTDebugOut("[dwc2_otg]: usb device descriptor received len : %d, maxpack : %d\r\n", desc_->bLength, desc_->bMaxPacketSize0);
	//UARTDebugOut("[dwc2_otg]: string blength : %d \r\n", desc_->bLength);
	//ep.max_packet_sz = desc_->bMaxPacketSize0;

	//dwc2_control_transfer(regs, &ep, 0x80, 0x06, 0x0100, 0, desc, sizeof(usb_dev_desc_t));
	//desc_ = (usb_dev_desc_t*)desc;
	//UARTDebugOut("[dwc2_otg]: product id : %d \r\n", desc_->iProduct);
	//UARTDebugOut("[dwc2_otg]: manufacturer id : %d \r\n", desc_->iManufacturer);
	//UARTDebugOut("[dwc2_otg]: serial id : %d \r\n", desc_->iSerialNumber);
	//UARTDebugOut("[dwc2_otg]: num config : %d \r\n", desc_->bNumConfigurations);
	//
	///** no strings for root hub, iss iss bom laaj pali no ??**/
	//UARTDebugOut("[dwc2_otg]: no string for root hub, iss iss bisarisili kiba, ulal kiba \r\n");
	///** next set address, and configure the hub, because devices are waiting to get powered in */
	//dwc2_control_transfer(regs, &ep, 0x00, 0x05, 1, 0x0000, NULL, 0x0000);
	//UARTDebugOut("[dwc2_otg]: set address successfull \r\n");

	//AA64SleepMS(1000);

	//memset(desc, 0, 4096);
	//
	//ep.dev_address = 1;
	///* now get the config descriptor*/
	//dwc2_control_transfer(regs, &ep, 0x80, 0x06, 0x0200, 0x0000, desc,9);
	//usb_config_desc_t* cdesc = (usb_config_desc_t*)desc;
	//UARTDebugOut("[dwc2_otg]: config desc wTotalLen : %d \r\n", cdesc->wTotalLength);
	//dwc2_control_transfer(regs, &ep, 0x80, 0x06, 0x0200, 0x0000, desc, cdesc->wTotalLength);
	//
	//usb_if_desc_t* interface_desc = raw_offset<usb_if_desc_t*>(cdesc, cdesc->bLength);
	//UARTDebugOut("num interfaces : %d , device class: %x \r\n", cdesc->bNumInterfaces, interface_desc->bInterfaceClass);
	//if (interface_desc->bInterfaceClass == 0x09)
	//	UARTDebugOut("This is a hub device \r\n");

	//dwc2_control_transfer(regs, &ep, 0x00, 0x09, 1, 0, NULL, 0);
	//UARTDebugOut("[dwc2_otg]: set configuration successfull \r\n");

	//usb_ep_desc_t* endp = (usb_ep_desc_t*)dwc2_get_descriptor(cdesc, DESCRIPTOR_TYPE_ENDPOINT);
	//while (raw_diff(endp, cdesc) < cdesc->wTotalLength) {
	//	if ((endp->bmAttributes & 0x03) == USB_EP_TYPE_INTERRUPT) {
	//		UARTDebugOut("Interrupt endpoint found num : %d dir -> %d \r\n", (endp->bEndpointAddress & 0x0f),
	//			((endp->bEndpointAddress >> 0x7) & 0xF) );
	//		UARTDebugOut("MPS: %d \r\n", endp->wMaxPacketSize & 0x7FF);
	//	}
	//	endp = raw_offset<usb_ep_desc_t*>(endp, endp->bLength);
	//}
	//

	//void* desc2 = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	//memset(desc2, 0, 4096);
	//dwc2_control_transfer(regs, &ep, 0xA0, 0x06, 0x2900, 0, desc2, 8);
	//usb_hub_desc_t* hub = (usb_hub_desc_t*)desc2;
	//UARTDebugOut("got hub descriptor, num ports : %d \r\n", hub->bNbrPorts);

	///** PORT power up **/
	//for (int i = 1; i <= hub->bNbrPorts; i++) {
	//	dwc2_control_transfer(regs, &ep, 0x23, 0x03, 8, i,NULL, 0);
	//	AA64SleepMS(100);
	//}
	//
	//UARTDebugOut("Port powered up \r\n");

	//return;
	//void* desc3 = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	//memset(desc3, 0, 4096);

	//for (int i = 1; i <= hub->bNbrPorts; i++) {
	//	usb_hub_get_port_status(regs, i, desc3, 1, speed);
	//	usb_port_status_t* pstatus = (usb_port_status_t*)desc3;

	//	if (pstatus->wPortChange == 0)
	//		continue;

	//	root_hub_handle_port_change(regs, i);
	//	/*if (pstatus->wPortStatus & PORT_STATUS_CONNECTION) {
	//		UARTDebugOut("Port[%d] already has connection \r\n", i);
	//		root_hub_handle_port_change(regs, i);
	//	}*/
	//	UARTDebugOut("PortStatus [%d] : %x change : %x \r\n", pstatus->wPortStatus, pstatus->wPortChange);
	//}
	//dwc2_usb_endpoint_t intep;
	//intep.dev_address = 1; // ep.dev_address;
	//intep.dir = 1U;
	//intep.ep_num = 1;
	//intep.max_packet_sz = 1;
	//intep.speed = ep.speed;
	//intep.type = USB_EP_TYPE_INTERRUPT;

	//void* inbuf = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	//memset(inbuf, 0, 4096);
	//intb = inbuf;
	//dwc2_interrupt_transfer(regs, &intep, inbuf, 0, 0, USB_PID_DATA0);

	//AuPmmngrReleasePage(desc3);
	//AuPmmngrReleasePage(desc2);
	//AuPmmngrReleasePage(desc);
	//dwc2_free_used_dma_list();

	_all_init = true;
}


void root_hub_transfer_int(dwc2_core_regs* regs, uint8_t pid) {
	if (!_all_init)
		return;
	dwc2_usb_endpoint_t intep;
	intep.dev_address = 1; // ep.dev_address;
	intep.dir = 1U;
	intep.ep_num = 1;
	intep.max_packet_sz = 1;
	intep.speed = speed_;
	intep.type = USB_EP_TYPE_INTERRUPT;

	dwc2_interrupt_transfer(regs, &intep, intb, 0, 0,pid );
}

void* root_hub_get_interrupt_buffer() {
	return intb;
}

#define PORT_CHANGE_ENABLE (1<<1)
#define PORT_CHANGE_SUSPEND (1<<2)
#define PORT_CHANGE_OVERCURRENT (1<<3)
#define PORT_CHANGE_RESET (1<<4)

void root_hub_handle_port_change(dwc2_core_regs* regs, uint8_t port) {
	void* p = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	usb_hub_get_port_status(regs, port, p, 1, speed_);
	AA64SleepMS(10);

	usb_port_status_t* status = (usb_port_status_t*)p;
	UARTDebugOut("status : %x , port change : %x \r\n", status->wPortStatus, status->wPortChange);
	if (status->wPortChange & PORT_CHANGE_CONNECTION) {
		dwc2_control_transfer(regs, &ep, 0x23, 0x01, 0x0010, port, NULL, 0);
		UARTDebugOut("[root-hub]: cleared port change connection %d \r\n", port);
	}

	if (status->wPortChange & PORT_CHANGE_ENABLE) {
		dwc2_control_transfer(regs, &ep, 0x23, 0x01, 0x0011, port, NULL, 0);
		UARTDebugOut("[root-hub]: cleared C_PORT_ENABLE port %d \r\n", port);
	}

	if (status->wPortChange & PORT_CHANGE_SUSPEND) {
		dwc2_control_transfer(regs, &ep, 0x23, 0x01, 0x0012, port, NULL, 0);
		UARTDebugOut("[root-hub]: cleared suspend bit at port %d \r\n", port);
	}

	if (status->wPortChange & PORT_CHANGE_OVERCURRENT) {
		dwc2_control_transfer(regs, &ep, 0x23, 0x01, 0x0013, port, NULL, 0);
		UARTDebugOut("[root-hub]: cleared overcurrent bit at port : %d \r\n", port);
	}

	if (status->wPortChange & PORT_CHANGE_RESET) {
		dwc2_control_transfer(regs, &ep, 0x23, 0x01, 0x0014, port, NULL, 0);
		UARTDebugOut("[root-hub]: cleared reset bit at port : %d \r\n", port);
	}


}
