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

#include "hub.h"
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <aucon.h>
#include <Hal/serial.h>
#include <_null.h>

void HUBSetPortFeature(USBDevice* dev, XHCISlot* slot, uint8_t feature, uint8_t port) {
	uint64_t buf = (uint64_t)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset((void*)buf, 0, 4096);

	USB_REQUEST_PACKET pack;
	pack.request_type = USB_BM_REQUEST_OUTPUT | USB_BM_REQUEST_CLASS | USB_BM_REQUEST_OTHER;
	pack.request = HUB_CMD_SET_FEATURE;
	pack.value = feature;//USB_DESCRIPTOR_WVALUE(0, feature);
	pack.index = port & 0xFF;
	pack.length = 0;

	XHCISendControlCmd(dev, slot, slot->slot_id, &pack, V2P(buf), 0, CTL_TRANSFER_TRT_NO_DATA);
}

void HUBGetDescriptor(USBDevice* dev, XHCISlot* slot, uint64_t buffer, uint16_t len) {
	USB_REQUEST_PACKET pack;
	pack.request_type = USB_BM_REQUEST_OUTPUT | USB_BM_REQUEST_CLASS | USB_BM_REQUEST_OTHER;
	pack.request = USB_BREQUEST_GET_DESCRIPTOR;
	pack.value = USB_DESCRIPTOR_WVALUE(HUB_DESCRIPTOR, 0);
	pack.index = 0;
	pack.length = len;

	XHCISendControlCmd(dev, slot, slot->slot_id, &pack, buffer, pack.length);
}


void USBHubInitialise(USBDevice* dev, XHCISlot* slot) {
	uint64_t buffer = (uint64_t)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset((void*)buffer, 0, PAGE_SIZE);

	uint8_t hub_def_numports = 4;

	USBGetDeviceDesc(dev, slot, slot->slot_id, V2P(buffer), sizeof(usb_dev_desc_t));
	int t_idx = -1;
	t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);
	if (t_idx != -1) {
		xhci_event_trb_t *evt = (xhci_event_trb_t*)dev->event_ring_segment;
		xhci_trb_t *trb = (xhci_trb_t*)evt;
	}
	usb_dev_desc_t* devdesc = (usb_dev_desc_t*)buffer;
	AuTextOut("HUB Protocol -> %x \r\n", devdesc->bDeviceProtocol);


	USBGetDeviceDesc(dev, slot, slot->slot_id, V2P(buffer), sizeof(usb_dev_desc_t));
	t_idx = -1;
	t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);
	if (t_idx != -1) {
		xhci_event_trb_t *evt = (xhci_event_trb_t*)dev->event_ring_segment;
		xhci_trb_t *trb = (xhci_trb_t*)evt;
	}

	HUBGetDescriptor(dev, slot, V2P(buffer), 71);
	t_idx = -1;
	t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);
	if (t_idx != -1) {
		xhci_event_trb_t *evt = (xhci_event_trb_t*)dev->event_ring_segment;
		xhci_trb_t *trb = (xhci_trb_t*)evt;
	}
	USBHubDescriptor *hub = (USBHubDescriptor*)buffer;
	AuTextOut("Hub Num Ports %d \n", hub->bNumberOfPorts);
	SeTextOut("Setting power up \r\n");
	for (uint8_t i = 1; i <= hub->bNumberOfPorts; i++) {
		HUBSetPortFeature(dev, slot, HUB_FEATURE_PORT_POWER, i);
		t_idx = -1;
		t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);
		if (t_idx != -1) {
			xhci_event_trb_t *evt = (xhci_event_trb_t*)dev->event_ring_segment;
			xhci_trb_t *trb = (xhci_trb_t*)evt;
		}
	}

	/*for (int i = 1; i <= hub->bNumberOfPorts; i++) {
		HUBSetPortFeature(dev, slot, HUB_FEATURE_C_PORT_CONNECTION, i, HUB_CMD_CLEAR_FEATURE);
		t_idx = -1;
		t_idx = XHCIPollEvent(dev, TRB_EVENT_TRANSFER);
		if (t_idx != -1) {
			xhci_event_trb_t *evt = (xhci_event_trb_t*)dev->event_ring_segment;
			xhci_trb_t *trb = (xhci_trb_t*)evt;
		}
	}*/

	AuTextOut("Hub Ports powered up \n");
}
