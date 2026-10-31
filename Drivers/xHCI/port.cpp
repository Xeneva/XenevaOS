/**
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

#include <stdint.h>
#include "xhci.h"
#include "usb.h"
#include <Mm/kmalloc.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <_null.h>
#include <aucon.h>
#include <Drivers/usb.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/serial.h>
#include <Hal/apic.h>

/*
* XHCIPortReset -- reset the given port
* @param this_port -- pointer to port register
*/
void XHCIPortReset(xhci_port_regs_t* this_port) {
	uint8_t port_speed = (this_port->port_sc >> 10) & 0xf;
	uint32_t port_change_bit = ((1 << 17) | (1 << 18) | (1 << 20) | (1 << 21) | (1 << 22));


	if (port_speed <= 3) { //USB 2.0 devices
		AuTextOut("USB 2.0 device \n");

		this_port->port_sc |= (1 << 4);
		//APICTimerSleep(5000);
		x86_64_udelay(100 * 1000);
		
		while ((this_port->port_sc & (1<<4)) != 0);
		
		if ((this_port->port_sc & (1 << 21)) == 0) {
			AuTextOut("Port reset did not complete \n");
		}
		AuTextOut("Port is Enabled ? %d \n", ((this_port->port_sc & XHCI_PORTSC_PED) & 0xf));
		x86_64_udelay(1000 * 1000);
		/*if ((this_port->port_sc & (1 << 1)) == 0) {
			this_port->port_sc |= (1 << 4);
			while ((this_port->port_sc & (1 << 4)) != 0);
		}*/
	}
}

XHCISlot* XHCICreateDeviceCtx(XHCIDevice* dev, uint8_t slot_num, uint8_t port_speed, uint8_t root_port_num) {
	XHCISlot* slot = (XHCISlot*)kmalloc(sizeof(XHCISlot));
	memset(slot, 0, sizeof(XHCISlot));
	slot->endpoints = initialize_list();


	uint64_t output_ctx = (uint64_t)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	uint64_t* output_ctx_ptr = (uint64_t*)output_ctx;
	memset((void*)output_ctx, 0, PAGE_SIZE);

	uint64_t input_ctx = (uint64_t)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	uint8_t* input_ctx_ptr = (uint8_t*)input_ctx;
	memset((void*)input_ctx, 0, 4096);

	*raw_offset<volatile uint32_t*>(input_ctx, 0x4) = (1 << 0) | (1 << 1);
	
	*raw_offset<volatile uint32_t*>(input_ctx, 0x20) = USB_SLOT_CTX_DWORD0(1 | (1<<1), 0, 0, port_speed, 0);
	*raw_offset<volatile uint32_t*>(input_ctx, 0x20 + 4) = USB_SLOT_CTX_DWORD1(0, root_port_num, 0);
	*raw_offset<volatile uint32_t*>(input_ctx, 0x20 + 8) |= (0 & 0x3ff) << 22;


	/* initialize the endpoint 0 ctx here */
	*raw_offset<volatile uint32_t*>(input_ctx, 0x40) = USB_ENDPOINT_CTX_DWORD0(0, 0, 0, 0, 0, 0);
	*raw_offset<volatile uint32_t*>(input_ctx, 0x40 + 4) = USB_ENDPOINT_CTX_DWORD1(XHCIGetMaxPacketSize(port_speed), 0, 0, 4, 3);

	uint64_t cmd_ring = (uint64_t)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset((void*)cmd_ring, 0, 4096);

	uint64_t* cmd_ring_virt = (uint64_t*)AuMapMMIO(V2P(cmd_ring), 1);

	*raw_offset<volatile uint32_t*>(input_ctx, 0x40 + 8) = USB_ENDPOINT_CTX_DWORD2(V2P(cmd_ring), 1);
	*raw_offset<volatile uint32_t*>(input_ctx, 0x40 + 12) = USB_ENDPOINT_CTX_DWORD3(V2P(cmd_ring));
	*raw_offset<volatile uint32_t*>(input_ctx, 0x40 + 0x10) = USB_ENDPOINT_CTX_DWORD4(0, 8);

	memcpy(output_ctx_ptr, raw_offset<void*>(input_ctx_ptr, 0x20), 0x40);

	dev->dev_ctx_base_array[slot_num] = V2P(output_ctx);

	slot->cmd_ring_base = cmd_ring;
	slot->cmd_ring = (xhci_trb_t*)cmd_ring_virt;
	slot->cmd_ring_index = 0;
	slot->cmd_ring_max = 128;
	slot->cmd_ring_cycle = 1;
	slot->slot_id = slot_num;
	slot->root_hub_port_num = root_port_num;
	slot->input_context_phys = V2P(input_ctx);
	slot->output_context_phys = V2P(output_ctx);

	/* insert a link trb at the last of cmd ring */
	slot->cmd_ring[slot->cmd_ring_max].trb_param_1 = V2P(slot->cmd_ring_base) & UINT32_MAX;
	slot->cmd_ring[slot->cmd_ring_max].trb_param_2 = (V2P(slot->cmd_ring_base) >> 32) & UINT32_MAX;
	slot->cmd_ring[slot->cmd_ring_max].trb_status = 0;
	slot->cmd_ring[slot->cmd_ring_max].trb_control = TRB_TRANSFER_LINK << 10 | (1 << 5) | (slot->cmd_ring_cycle & 0x1);
	XHCIAddSlot(dev, slot);

	/* Here we need an function, which will issues commands to this slot */
	XHCISendAddressDevice(dev, slot, 0, V2P(input_ctx), slot_num);
	int idx = XHCIPollEvent(dev, TRB_EVENT_CMD_COMPLETION);

	return slot;
}

/*
 * XHCIGetSlot -- returns a slot from slot list
 * @param dev -- Pointer to USB device
 * @param port_num -- port id
 */
XHCISlot* XHCIGetSlot(XHCIDevice* dev, uint8_t port_num) {
	for (int i = 0; i < dev->slot_list->pointer; i++) {
		XHCISlot* slot = (XHCISlot*)list_get_at(dev->slot_list, i);
		if (slot->root_hub_port_num == port_num)
			return slot;
	}

	return NULL;
}

/*
 * XHCISlotReleaseEndpoints -- release all endpoint
 * @param slot -- Pointer to device slot
 */
void XHCISlotReleaseEndpoints(XHCISlot* slot) {
	for (int i = 0; i < slot->endpoints->pointer; i++) {
		XHCIEndpoint* ep = (XHCIEndpoint*)list_remove(slot->endpoints, i);
		if (ep) {
			AuPmmngrReleasePage((uint64_t)V2P((uint64_t)slot->cmd_ring));
			kfree(ep);
		}
	}
}


void XHCIPortChecLinkState(xhci_port_regs_t* this_port) {
#define PORTSC_PLS_MASK (0xF << 5)
#define PORTSC_PLS_SHIFT 5
#define PORTSC_LWS (1<<16)

	uint8_t pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;
	if (pls == 3) {
		AuTextOut("Port is in suspended state \n");
		//Port is in U3 state (suspended) -- power savings
		this_port->port_sc &= ~PORTSC_PLS_MASK;
		this_port->port_sc |= (15 << PORTSC_PLS_SHIFT);
		this_port->port_sc |= PORTSC_LWS;
		APICTimerSleep(1000);

	}
	else if (pls == 7 || pls == 5) {
		pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;
		AuTextOut("Port is in Polling state %d\n", pls);
		int timeout = 1000;
		while (pls != 0) {
			if (timeout <= 0)
				break;
			//APICTimerSleep(1000);
			x86_64_udelay(100 * 1000);
			/*for (int i = 0; i < 10000000000; i++)
				;*/
			pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;
			timeout--;
		}
		pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;

		/* retry code */
		AuTextOut("Port link state changed -> %d\n", pls);
		if (pls == 7 || pls == 5) {
			AuTextOut("Port link state is still in polling state retrying... \n");
			XHCIPortReset(this_port);
			pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;
			timeout = 1000;
			while (pls != 0) {
				if (timeout <= 0)
					break;
				//APICTimerSleep(1000);
				/*for (int i = 0; i < 10000000000; i++)
					;*/
				x86_64_udelay(100 * 1000);
				pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;
				timeout--;
			}
			pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;

			/* Forcing code */
			if (pls == 7 || pls == 5) {
				AuTextOut("Port link state is still in polling after retrying, now forcing \n");
				uint32_t portval = this_port->port_sc;
				portval &= ~(0xF << 5);
				portval |= (0 << 5);
				portval |= (1 << 16);
				this_port->port_sc = portval;
			}
			if (pls == 3) {
				AuTextOut("Port is in suspended state \n");
				//Port is in U3 state (suspended) -- power savings
				this_port->port_sc &= ~PORTSC_PLS_MASK;
				this_port->port_sc |= (0<<5);
				this_port->port_sc |= PORTSC_LWS;
				x86_64_udelay(100 * 1000);

			}
			pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;
			timeout = 1000;
			while (pls != 0) {
				if (timeout <= 0)
					break;
			/*	APICTimerSleep(1000);
				for (int i = 0; i < 10000000000; i++)
					;*/
				x86_64_udelay(100 * 1000);
				pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;
				timeout--;
			}
			pls = (this_port->port_sc >> PORTSC_PLS_SHIFT) & 0xF;
			if (pls == 7 || pls == 5 || pls == 3) {
				AuTextOut("Your usb device is malfunctioned :)) \n");
				return;
			}
		}
	}

}

uint8_t XHCIRetryEnableSlot(xhci_port_regs_t* regs, XHCIDevice* dev, int port) {
	int times = 6;
	uint8_t slotid = 0;
	AuTextOut("Enable slot command failed, retrying and reasoning \n");
	while (1) {
		if (times <= 0)
			break;
		if ((regs->port_sc & XHCI_PORTSC_CCS) != 0) {
			AuTextOut("The device on the port %d is disconnected \n", port);
			break;
		}
		if ((regs->port_sc & XHCI_PORTSC_PED) != 0)
			AuTextOut("The port %d is not powered \n", port);
		if (((regs->port_sc >> 5) & 0xF) != 0)
			AuTextOut("This port %d is not in active state\n", port);

		XHCIEnableSlot(dev, 0);
		int idx = XHCIPollEvent(dev, TRB_EVENT_CMD_COMPLETION);
		if (idx != -1) {
			xhci_event_trb_t* evt = (xhci_event_trb_t*)dev->event_ring_segment;
			xhci_trb_t* trb = (xhci_trb_t*)evt;
			slotid = (trb[idx].trb_control >> 24) & 0xff;
			AuTextOut("[xhci]: Retry enable slot cmd received slotid -> %d \n", slotid);
		}

		x86_64_udelay(1000 * 1000);
		times--;
	}
	return slotid;
}

bool hotPlug = false;
/*
 * XHCIPortInitialise -- initialise a given port
 * @param dev -- Pointer to XHCIDevice
 * @param port -- port number
 */
void XHCIPortInitialise(XHCIDevice* dev, unsigned int port) {
	xhci_port_regs_t* this_port = &dev->ports[port - 1];

	if ((this_port->port_sc & 1) == 0) {
		/* Handle device disconnection */
		XHCISlot* slot = XHCIGetSlot(dev, port);
		uint8_t slot_id = slot->slot_id;

		AuUSBDevice* usbdev = (AuUSBDevice*)AuUSBGetDeviceStruc(slot);

		/* disable the slot */
		XHCIDisableSlot(dev, slot_id);
		int idx = XHCIPollEvent(dev, TRB_EVENT_CMD_COMPLETION);
		if (idx == -1)
			return;

		dev->dev_ctx_base_array[slot_id] = 0;

		AuPmmngrReleasePage((uint64_t)slot->input_context_phys);
		AuPmmngrReleasePage((uint64_t)slot->output_context_phys);
		AuPmmngrReleasePage((uint64_t)V2P(slot->cmd_ring_base));
		

		/* remove endpoint data structures from the slot */
		XHCISlotReleaseEndpoints(slot);
	
		XHCISlotRemove(dev, slot_id);

		if (!usbdev)
			return; //will cause critical error
		
		/*Report it to USB Stack */
		AuUSBDeviceDisconnect((AuUSBDeviceStruc*)usbdev);

		/* Here we need to pass this notification to, Aurora
		* that device is disconnected */
		AuTextOut("Device disconnected at port -> %d, slot -> %d \n", port, slot_id);
		this_port->port_sc |= this_port->port_sc;
	}

	if ((this_port->port_sc & 1)) {
		if (hotPlug)
			AuTextOut("Port connection handling %d \r\n", port);
		/* Handle device connection */
		/* Reset the port */

		XHCIPortReset(this_port);

		XHCIPortChecLinkState(this_port);

		
		/*uint32_t portsc = this_port->port_sc;
		this_port->port_sc |= portsc;*/

		uint8_t port_speed = (this_port->port_sc >> 10) & 0xf;
		uint8_t class_code, sub_class_code, protocol = 0;
		uint8_t slot_id = 0;

		if (hotPlug)
			AuTextOut("Port reset complete \r\n");
		/* Enable slot command */
		XHCIEnableSlot(dev, 0);
		int idx = XHCIPollEvent(dev, TRB_EVENT_CMD_COMPLETION);
		if (idx != -1) {
			xhci_event_trb_t* evt = (xhci_event_trb_t*)dev->event_ring_segment;
			xhci_trb_t* trb = (xhci_trb_t*)evt;
			slot_id = (trb[idx].trb_control >> 24) & 0xff;
		}


		if (slot_id == 0) {
			slot_id = XHCIRetryEnableSlot(this_port, dev, port);
			if (slot_id == 0) {
				AuTextOut("Enable slot id really failed \n");
				return;
			}
		}

		/* After getting a device slot,
		* allocate device slot data structures
		*/
		XHCISlot* slot = XHCICreateDeviceCtx(dev, slot_id, port_speed, port);

		this_port->port_sc |= (1 << 9);

		slot->port_speed = port_speed;

		uint64_t* buffer = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		memset(buffer, 0, 4096);

		AuUSBDevice* usbdev = USBCreateDevice();

		usbdev->data = slot;

		USBGetDeviceDesc(usbdev, V2P((uint64_t)buffer), 18);

		x86_64_udelay(1000 * 1000);

		int t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);
		if (t_idx != -1) {
			xhci_event_trb_t* evt = (xhci_event_trb_t*)dev->event_ring_segment;
			xhci_trb_t* trb = (xhci_trb_t*)evt;
		}

		usb_dev_desc_t* dev_desc = (usb_dev_desc_t*)buffer;
	//	return;
		uint64_t input_ctx_virt = P2V(slot->input_context_phys);

		class_code = dev_desc->bDeviceClass;
		sub_class_code = dev_desc->bDeviceSubClass;
		protocol = dev_desc->bDeviceProtocol;

		/* if device class is HUB then set the hub bit in slot context */
		if (dev_desc->bDeviceClass == 0x09) {
			*raw_offset<volatile uint32_t*>(input_ctx_virt, 0x20) |= (1 << 26);
			return;
		}

		if (dev_desc->bMaxPacketSize0 == 0) {
			AuTextOut("bMaxpacket returned 0 for port %d \n", port);
			return;
		}
		/* update the endpoint context 0 with maximum packet size supported
		 * by the device */
		*raw_offset<volatile uint32_t*>(input_ctx_virt, 0x40 + 4) |= ((dev_desc->bMaxPacketSize0 & 0xFFFF) << 16);
		*raw_offset<volatile uint32_t*>(input_ctx_virt, 0x20 + 0x8) |= (0 << 22);
		*raw_offset<volatile uint32_t*>(input_ctx_virt, 0x00 + 0x4) |= (1 << 1);

		/* send evaluate context command for reflecting those changes */
		XHCIEvaluateContextCmd(dev, slot->input_context_phys, slot->slot_id);
		t_idx = XHCIPollEvent(dev, TRB_EVENT_CMD_COMPLETION);

		/* Now we have fully operational endpoint 0 pipe */

		/* Get the product (device) name using string descriptor */
		uint64_t* string_buf = (uint64_t*)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
		memset(string_buf, 0, PAGE_SIZE);

		USBGetStringDesc(usbdev,  V2P((uint64_t)string_buf), dev_desc->iProduct);
		t_idx = -1;
		t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);

		usb_string_desc_t* str_desc = (usb_string_desc_t*)string_buf;
		uint8_t* string_buf_ptr = ((uint8_t*)str_desc + 2);
		uint16_t* string = (uint16_t*)string_buf_ptr;

		/* Here !! do something with the string */
		for (int i = 0; i < 32; i++)
			AuTextOut("%c", string[i]);
		AuTextOut("\n");

		/* The main step: get the 0th config descriptor which contain the
		 * config value for set_config command */
		USBGetConfigDesc(usbdev, V2P((size_t)buffer), 9, 0);
		t_idx = -1;
		t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);
		if (t_idx != -1) {
			xhci_event_trb_t* evt = (xhci_event_trb_t*)dev->event_ring_segment;
			xhci_trb_t* trb = (xhci_trb_t*)evt;
		}
		usb_config_desc_t* config = (usb_config_desc_t*)buffer;
		uint16_t total_config_len = config->wTotalLength;
		uint8_t config_value = config->bConfigurationValue;
		memset(buffer, 0, PAGE_SIZE);
		/* The main step: get the 0th config descriptor which contain the
		* config value for set_config command */
		USBGetConfigDesc(usbdev, V2P((size_t)buffer), total_config_len, 0);
		t_idx = -1;
		t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);
		if (t_idx != -1) {
			xhci_event_trb_t* evt = (xhci_event_trb_t*)dev->event_ring_segment;
			xhci_trb_t* trb = (xhci_trb_t*)evt;
		}
		config = (usb_config_desc_t*)buffer;


		usb_if_desc_t* interface_desc = raw_offset<usb_if_desc_t*>(config, config->bLength);
		if (!class_code)
			class_code = interface_desc->bInterfaceClass;
		if (!sub_class_code)
			sub_class_code = interface_desc->bInterfaceSubClass;
		if (!protocol)
			protocol = interface_desc->bInterfaceProtocol;

		slot->classC = class_code;
		slot->subClassC = sub_class_code;
		slot->prot = protocol;

		usbdev->classCode = class_code;
		usbdev->subClassCode = sub_class_code;
		usbdev->protocol = protocol;
		
		usb_endpoint_desc_t* endp = raw_offset<usb_endpoint_desc_t*>(interface_desc, interface_desc->bLength);

		*raw_offset<volatile uint32_t*>(input_ctx_virt, 0x00 + 0x1C) |= ((interface_desc->bAlternateSetting & 0xff) << 16) |
			((interface_desc->bInterfaceNumber & 0xff) << 8) | ((config->bConfigurationValue & 0xff) << 0);

		/* send evaluate context command for reflecting those changes */
		XHCIEvaluateContextCmd(dev, slot->input_context_phys, slot->slot_id);
		t_idx = XHCIPollEvent(dev, TRB_EVENT_CMD_COMPLETION);

		uint64_t inte_addr = 0;
		uint32_t lasti = 0;
		while (raw_diff(endp, config) < config->wTotalLength) {

			if (endp->bDescriptorType != USB_DESCRIPTOR_ENDPOINT) {
				endp = raw_offset<usb_endpoint_desc_t*>(endp, endp->bLength);
				continue;
			}

			if (endp->bDescriptorType == USB_DESCRIPTOR_SUPERSPEED_ENDP_CMP) {
				endp = raw_offset<usb_endpoint_desc_t*>(endp, endp->bLength);
				continue;
			}

			uint8_t endp_num = endp->bEndpointAddress & 0xf;
			uint8_t dir = (endp->bEndpointAddress >> 7) & 0xf;
			uint16_t max_packet_sz = endp->wMaxPacketSize & 0x7FF;
			uint8_t max_burst_sz = (endp->wMaxPacketSize & 0x1800) >> 11;
			uint8_t transfer_type = endp->bmAttributes & 0x3;
			uint8_t interval = endp->bInterval;
			uint16_t ici = ((endp_num * 2) + 1) + dir;
			uint16_t dci = ((endp_num * 2) + dir);
			if (transfer_type == ENDPOINT_TRANSFER_TYPE_CONTROL) {
				ici = (endp_num + 1) * 2;
				dci = (endp_num * 2) + 1;
			}
			if (endp_num == 0)
				ici = (endp_num + 1) * 2;
			uint64_t addr = (static_cast<uint64_t>(ici) * 32);
			uint64_t dc_addr = (static_cast<uint64_t>(dci) * 32);
			uint8_t ep_type, cerr;
			cerr = 3;
			switch (transfer_type) {
			case ENDPOINT_TRANSFER_TYPE_CONTROL:
				ep_type = 4;
				break;
			case ENDPOINT_TRANSFER_TYPE_ISOCH:
				if (dir) //IF DIR 1, THEN TYPE IS IN
					ep_type = 5;
				else
					ep_type = 1;
				cerr = 0; // counter error doesn't apply for isoch transfer
				break;
			case ENDPOINT_TRANSFER_TYPE_BULK:
				//AuTextOut("BULK Ep num ->%d \n", endp_num);
				if (dir)
					ep_type = 6;
				else
					ep_type = 2;
				break;
			case ENDPOINT_TRANSFER_TYPE_INT:
				inte_addr = addr;
				if (dir)
					ep_type = 7;
				else
					ep_type = 3;
				break;

			}

			*raw_offset<volatile uint32_t*>(input_ctx_virt, 0x4) |= (1 << dci);

			auto max_esit = max_packet_sz * (max_burst_sz + 1);
			*raw_offset<volatile uint32_t*>(input_ctx_virt, addr + 0) = USB_ENDPOINT_CTX_DWORD0(max_esit >> 16, interval, 0, 0, 0, 0);
			*raw_offset<volatile uint32_t*>(input_ctx_virt, addr + 0x04) = USB_ENDPOINT_CTX_DWORD1(max_packet_sz, max_burst_sz, 1, ep_type, cerr);
			uint64_t cmd_ring = (uint64_t)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
			memset((void*)cmd_ring, 0, 4096);
			*raw_offset<volatile uint32_t*>(input_ctx_virt, addr + 0x08) = USB_ENDPOINT_CTX_DWORD2(V2P(cmd_ring), 1);
			*raw_offset<volatile uint32_t*>(input_ctx_virt, addr + 0x0C) = USB_ENDPOINT_CTX_DWORD3(V2P(cmd_ring));
			*raw_offset<volatile uint32_t*>(input_ctx_virt, addr + 0x10) = USB_ENDPOINT_CTX_DWORD4(max_esit, 0x400);

			XHCIEndpoint* ep = (XHCIEndpoint*)kmalloc(sizeof(XHCIEndpoint));
			memset(ep, 0, sizeof(XHCIEndpoint));
			ep->cmd_ring = (xhci_trb_t*)cmd_ring;
			ep->cmd_ring_cycle = 1;
			ep->cmd_ring_max = 128;
			ep->endpoint_num = endp_num;
			ep->endpoint_type = transfer_type;
			ep->interval = interval;
			ep->max_packet_sz = max_packet_sz;
			ep->dci = dci;
			ep->offset = addr;
			ep->ep_type = transfer_type;
			ep->dc_offset = dc_addr;
			ep->dir = dir;
			ep->endpointAddress = endp->bEndpointAddress;
			ep->endpointAttr = endp->bmAttributes;
			ep->callback = NULL;

			/* insert a link trb at the last of cmd ring */
			ep->cmd_ring[ep->cmd_ring_max].trb_param_1 = V2P(cmd_ring) & UINT32_MAX;
			ep->cmd_ring[ep->cmd_ring_max].trb_param_2 = (V2P(cmd_ring) >> 32) & UINT32_MAX;
			ep->cmd_ring[ep->cmd_ring_max].trb_status = 0;
			ep->cmd_ring[ep->cmd_ring_max].trb_control = TRB_TRANSFER_LINK << 10 | (1 << 5) | (ep->cmd_ring_cycle & 0x1);

			list_add(slot->endpoints, ep);

			if (lasti < dci) lasti = dci;

			/* also send configure endpoint command to xhc*/
			XHCIConfigureEndpoint(dev, slot->input_context_phys, slot_id);
			t_idx = -1;
			t_idx = XHCIPollEvent(dev, TRB_EVENT_CMD_COMPLETION);

				/* update the required endpoints, if not created, create one */
			endp = raw_offset<usb_endpoint_desc_t*>(endp, endp->bLength);
		}

		*raw_offset<volatile uint32_t*>(input_ctx_virt, 0x4) |= (1 << 0);
		*raw_offset<volatile uint32_t*>(input_ctx_virt, 0x20 + 0) = (lasti & 0x1f) << 27;

		/* send evaluate context command for reflecting those changes */
		XHCIEvaluateContextCmd(dev, slot->input_context_phys, slot->slot_id);
		t_idx = -1;
		t_idx = XHCIPollEvent(dev, TRB_EVENT_CMD_COMPLETION);

		usbdev->configValue = config_value;
		/* Now set the preferred configuration, returned in 0th config descriptor*/
		USBSetConfigDesc(usbdev,  config_value);
		t_idx = -1;
		t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);

		slot->descriptor_buff = (uint64_t)buffer;
		usbdev->descriptor = buffer;
		usbdev->deviceID = slot_id;
		AuTextOut("Port initialised CC- %x SC-%x \r\n", usbdev->classCode, usbdev->subClassCode);
		uint32_t val = this_port->port_sc;
		this_port->port_sc |= val;

		USBDeviceSetFunctions(usbdev);

		AuUSBDeviceConnect((AuUSBDeviceStruc*)usbdev);
	}
}


/*
* XHCIStartDefaultPorts -- initializes all powered ports
* @param dev -- Pointer to USB device structures
*/
void XHCIStartDefaultPorts(XHCIDevice* dev) {
	for (unsigned int i = 1; i <= dev->num_ports; i++) {
		xhci_port_regs_t* this_port = &dev->ports[i - 1];
		if ((this_port->port_sc & 1)) {
			XHCIPortInitialise(dev, i);
		}
	}
}