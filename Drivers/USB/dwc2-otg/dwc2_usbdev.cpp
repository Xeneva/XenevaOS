/**
* @file dwc2_usbdev.cpp
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

#include "dwc2_usbdev.h"
#include <Drivers/uart.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <_null.h>
#include <Hal/AA64/aa64cpu.h>
#include "usb_stdhub.h"
#include "lan7800.h"
#include "usb_hid.h"
#include <Log/klog.h>

/**
 * @brief dwc2_usbdev_setaddress -- set an address to the current device
 * @param regs -- pointer to dwc2 core registers
 * @param dev -- pointer to usb device structure
 * @param address -- address number to assign
 */
static bool dwc2_usbdev_setaddress(dwc2_core_regs* regs, dwc2_usb_device* dev, uint8_t address) {
	if (dwc2_control_transfer(regs, &dev->ep, 0x00, 0x05, address, 0x0000, NULL, 0x0000)) {
		UARTDebugOut("[dwc2-otg]: failed to set address to device \r\n");
		return 1;
	}
	AA64SleepMS(100);
	return 0;
}

/**
 * @brief dwc2_usbdev_configure -- configure the usb device now
 * @param regs -- pointer to dwc2 core registers
 * @param dev -- pointer to usb device structure
 */
static bool dwc2_usbdev_configure(dwc2_core_regs* regs, dwc2_usb_device* dev) {

	uint64_t config_desc_phys = 0;

	void* desc = AuDMAGClassAlloc(dwc2_get_dma_class(), sizeof(usb_config_desc_t), &config_desc_phys);//(void*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));

	if (dwc2_control_transfer(regs, &dev->ep, 0x80, 0x06, 0x0200, 0x0000, (void*)config_desc_phys, 9)) {
		UARTDebugOut("[dwc2-otg]: failed to get configuration descriptor \r\n");
		return 1;
	}
	
	usb_config_desc_t* cdesc = (usb_config_desc_t*)desc;
    
	if (dwc2_control_transfer(regs, &dev->ep, 0x80, 0x06, 0x0200, 0x0000,(void*)config_desc_phys /*(void*)V2P((uint64_t)desc)*/, cdesc->wTotalLength)) {
		UARTDebugOut("[dwc2-otg]: failed to get second stage configuration descriptor \r\n");
		return 1;
	}

	dev->config_desc = cdesc;

	/**yaaayyy,finally check the type of the device **/
	usb_if_desc_t* interface_desc = raw_offset<usb_if_desc_t*>(cdesc, cdesc->bLength);
	
	dev->interface_desc = interface_desc;

	if (dwc2_control_transfer(regs, &dev->ep, 0x00, 0x09, 1, 0, NULL, 0)) {
		UARTDebugOut("[dwc2-otg]: failed to set configuration value \r\n");
		return 1;
	}

	/** verify and call each class driver for this device **/
	switch (interface_desc->bInterfaceClass) {
	case 0x09: {
		UARTDebugOut("[dwc2-otg]: found a hub device \r\n");
		if (usb_hub_initialize(regs, dev)) 
			UARTDebugOut("[dwc2-otg]: failed to initialize hub \r\n");
		
		break;
	}
	case 0x03:
		UARTDebugOut("[dwc2-otg]: found a HID device \r\n");
		USBHidInitialize(regs, dev);
		break;
	case 0x01: {
		UARTDebugOut("[dwc2-otg]: found USB-Audio based device \r\n");
		break;
	}
	default:
		UARTDebugOut("[dwc2-otg]: found interface class : %x \r\n", interface_desc->bInterfaceClass);
		UARTDebugOut("[dwc2-otg]: vendor ID : %x, product ID : %x \r\n", dev->device_desc->idVendor, dev->device_desc->idProduct);
		UARTDebugOut("[dwc2-otg]: subClass : %x, protocol : %x \r\n", dev->device_desc->bDeviceSubClass, 
			dev->device_desc->bDeviceProtocol);

		break;
	}

	/** try here again with product id **/
	switch (dev->device_desc->idProduct) {
	case 0x7800:
		//lan7800_initialize(regs, dev);
		break;
	}

	return 0;
}

void dwc2_usb_get_string(dwc2_core_regs* regs, dwc2_usb_device* dev, uint8_t index, uint16_t langID, char* out) {
	if (index == 0) {
		strcpy(out, "<none>");
		return;
	}
	uint64_t strphys_out = 0;
	void* buff = AuDMAGClassAlloc(dwc2_get_dma_class(), 255, &strphys_out);

	dwc2_control_transfer(regs, &dev->ep, 0x80, 0x06, (0x03 << 8) | index, langID, (void*)strphys_out, 255);

	uint8_t* buf = (uint8_t*)buff;

	uint8_t len = buf[0];
	if (len < 2 || buf[1] != 0x03) {
		strcpy(out, "<invalid>");
		return;
	}

	int out_idx = 0;
	for (int i = 2; i < len; i += 2) {
		uint16_t wchar = buf[i] | (buf[i + 1] << 8);
		out[out_idx++] = (wchar < 0x80) ? (char)wchar : '?';
	}
	out[out_idx] = '\0';
	AuDMAGClassFree(dwc2_get_dma_class(), buff, strphys_out, 255);
}
/**
 * dwc2_usbdev_initialize -- initializing usb device 
 * @param port -- port number
 * @param hub_addr -- hub device address
 * @param hub_port -- hub's port number
 * @param speed -- speed of the port
 */
bool dwc2_usbdev_initialize(dwc2_core_regs* regs, uint8_t port, uint8_t hub_addr, uint8_t hub_port, uint8_t speed) {

	dwc2_usb_device* usb = (dwc2_usb_device*)kmalloc(sizeof(dwc2_usb_device));
	memset(usb, 0, sizeof(dwc2_usb_device));

	/* let's get the config descriptor first */
	usb->ep.dev_address = 0; //0 for very beginning 
	usb->ep.ep_num = 0; //control ep
	usb->ep.type = EP_CONTROL; //control
	usb->ep.dir = EP_BIDIR;
	usb->ep.speed = speed;
	usb->ep.max_packet_sz = 8;
	usb->ep.hub_address = hub_addr;
	usb->ep.hub_port = port;
	usb->hub_address = hub_addr;
	usb->hub_port = port;
	usb->ep.ch = 0;
	if ((speed == USBSpeedLow && hub_addr != 0) || 
		(speed == USBSpeedFull && hub_addr != 0))
		usb->ep.split_enable = 1;


	uint64_t dev_desc_phys = 0;
	/** get the first device descriptor **/
	void* desc = (void*)AuDMAGClassAlloc(dwc2_get_dma_class(), sizeof(usb_dev_desc_t), &dev_desc_phys);   //P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(desc, 0, 4096);
	if (dwc2_control_transfer(regs, &usb->ep, 0x80, 0x06, 0x0100, 0, (void*)dev_desc_phys, 0x0008)) {
		UARTDebugOut("[dwc2_otg]: failed to device descriptor \r\n");
		return 1;
	}

	usb_dev_desc_t* desc_ = (usb_dev_desc_t*)desc;
	usb->ep.max_packet_sz = desc_->bMaxPacketSize0;
	
	uint8_t address = dwc2_assign_address();

	if (dwc2_usbdev_setaddress(regs, usb, address)) {
		kfree(usb);
		AuPmmngrReleasePage((uint64_t)V2P((uint64_t)desc));
		return 1;
	}

	usb->ep.dev_address = address;
	usb->dev_address = address;


	if (dwc2_control_transfer(regs, &usb->ep, 0x80, 0x06, 0x0100, 0, (void*)dev_desc_phys, sizeof(usb_dev_desc_t))) {
		UARTDebugOut("[dwc2_otg]: failed to get second stage device descriptor \r\n");
		return 1;
	}


	UARTDebugOut("[dwc2_otg]: usb device descriptor received len : %d, maxpack : %d\r\n", desc_->bLength, desc_->bMaxPacketSize0);
	UARTDebugOut("[dwc2_otg]: string blength : %d , descriptorType : %d \r\n", desc_->bLength, desc_->bDescriptorType);
	UARTDebugOut("[dwc2_otg]: bNumConfig : %d \r\n", desc_->bNumConfigurations);
	usb->device_desc = desc_;

	uint64_t string_phys = 0;
	void* strdesc = AuDMAGClassAlloc(dwc2_get_dma_class(), sizeof(usb_string_desc_t), &string_phys);
	if (dwc2_control_transfer(regs, &usb->ep, 0x80, 0x06, 0x0300, 0x0000, (void*)string_phys, 4)) {
		UARTDebugOut("[dwc2-otg]: failed to get string descriptor \r\n");
	}

	uint8_t* strbuf = (uint8_t*)strdesc;
	uint16_t lang_id = *(uint16_t*)&strbuf[2];

	char product[32];
	dwc2_usb_get_string(regs, usb, desc_->iProduct, lang_id, product);
	UARTDebugOut("usb device name : %s \r\n", product);
	memset(product, 0, 32);
	dwc2_usb_get_string(regs, usb, desc_->iManufacturer, lang_id, product);
	UARTDebugOut("manufactured by : %s \r\n", product);

	//AuPmmngrReleasePage(strdesc);

	if (dwc2_usbdev_configure(regs, usb)) {
		kfree(usb);
		UARTDebugOut("[dwc2-otg]: failed to configure the device \r\n");
		//AuPmmngrReleasePage((uint64_t)V2P((uint64_t)desc));
		return 1;
	}
	return 0;
}