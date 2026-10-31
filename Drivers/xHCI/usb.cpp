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

#include "usb.h"
#include "xhci.h"
#include <stdint.h>
#include <_null.h>
#include <aucon.h>
#include <Mm/kmalloc.h>
#include <string.h>

/*
 * USBCreateDevice -- create aurora usb device
 */
AuUSBDevice* USBCreateDevice() {
	AuUSBDevice* dev = (AuUSBDevice*)kmalloc(sizeof(AuUSBDevice));
	memset(dev, 0, sizeof(AuUSBDevice));
	return dev;
}
/*
* USBGetDeviceDesc -- sends USB_GET_DESCRIPTOR request to specific device
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param buffer -- address of the buffer where device descriptor will be written
* @param len -- number of bytes needs to be requested
*/
void USBGetDeviceDesc(AuUSBDevice *usbdev, uint64_t buffer, uint16_t len) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return; //critical error
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return; //critical error
	uint8_t slot_id = slot->slot_id;
	USB_REQUEST_PACKET pack;
	pack.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	pack.request = USB_BREQUEST_GET_DESCRIPTOR;
	pack.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_DEVICE, 0);
	pack.index = 0;
	pack.length = len;
	XHCISendControlCmd(dev, slot, slot_id, &pack, buffer, len);
}


/*
* USBGetStringDesc -- request a string descriptor to specific device
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param buffer -- address of the buffer where device descriptor will be written
* @param id-- type of the string needs to be requested
*/
void USBGetStringDesc(AuUSBDevice* usbdev, uint64_t buffer, uint16_t id) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return; //critical error
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return; //critical error
	uint8_t slot_id = slot->slot_id;
	USB_REQUEST_PACKET pack;
	pack.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	pack.request = USB_BREQUEST_GET_DESCRIPTOR;
	pack.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_STRING, id);
	pack.index = 0x0409;
	pack.length = 256;

	XHCISendControlCmd(dev, slot, slot_id, &pack, buffer, 256);
}

/*
 * USBGetConfigDesc -- get configuration descriptor
 * @param dev -- Pointer to usb device structure
 * @param slot -- Pointer to usb slot data structure
 * @param slot_id -- Slot id of the device
 * @param buffer -- address of the buffer where device descriptor will be written
 */
void USBGetConfigDesc(AuUSBDevice *usbdev,uint64_t buffer, uint16_t len, uint8_t id) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return; //critical error
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return; //critical error
	uint8_t slot_id = slot->slot_id;
	USB_REQUEST_PACKET pack;
	pack.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	pack.request = USB_BREQUEST_GET_DESCRIPTOR;
	pack.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_CONFIGURATION, id);
	pack.index = 0;
	pack.length = len;

	XHCISendControlCmd(dev, slot, slot_id, &pack, buffer, len);
}

/*
* USBGetConfigDesc -- get configuration descriptor
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param configval -- config value returned in configuration descriptor
*/
void USBSetConfigDesc(AuUSBDevice *usbdev, uint8_t configval) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return; //critical error
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return; //critical error
	uint8_t slot_id = slot->slot_id;
	USB_REQUEST_PACKET pack;
	pack.request_type = USB_BM_REQUEST_OUTPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	pack.request = USB_BREQUEST_SET_CONFIGURATION;
	pack.value = USB_DESCRIPTOR_WVALUE(0, configval);
	pack.index = 0;
	pack.length = 0;

	XHCISendControlCmd(dev, slot, slot_id, &pack, NULL, 0);
}

usb_descriptor_t* USBGetDescriptor(AuUSBDevice *usbdev, uint8_t type) {
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return NULL;
	usb_descriptor_t* desc = (usb_descriptor_t*)slot->descriptor_buff;
	usb_config_desc_t* config = (usb_config_desc_t*)slot->descriptor_buff;
	usb_if_desc_t* interface_desc = raw_offset<usb_if_desc_t*>(config, config->bLength);
	usb_descriptor_t* endp = raw_offset<usb_descriptor_t*>(interface_desc, interface_desc->bLength);
	while (raw_diff(endp, config) < config->wTotalLength) {
		if (endp->bDescriptorType == type) {
			return endp;
		}
		endp = raw_offset<usb_descriptor_t*>(endp, endp->bLength);
	}
}

/*
 * USBGetEndpoint -- returns a specific endpoint by
 * its type
 * @param usbdev -- Pointer to USB Device
 * @param ep_type -- Endpoint type
 */
void* USBGetEndpoint(AuUSBDevice* usbdev, uint8_t ep_type) {
	if (!usbdev)
		return NULL;
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return NULL;
	XHCIEndpoint* ep_ = NULL;
	for (int i = 0; i < slot->endpoints->pointer; i++) {
		XHCIEndpoint* ep = (XHCIEndpoint*)list_get_at(slot->endpoints, i);
		if (ep->ep_type == ep_type) {
			ep_ = ep;
			break;
		}
	}
	return (void*)ep_;
}


/*
 * USBGetBulkEndpoint -- checks and return bulk endpoint
 * @param usbdev -- Pointer to usb device struc
 * @param dir -- Direction of the bulk endpoint 1-- IN, 0 -- OUT
 */
void* USBGetBulkEndpoint(AuUSBDevice* usbdev, uint8_t dir) {
	if (!usbdev)
		return NULL;
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return NULL;
	XHCIEndpoint* ep_ = NULL;
	for (int i = 0; i < slot->endpoints->pointer; i++) {
		XHCIEndpoint* ep = (XHCIEndpoint*)list_get_at(slot->endpoints, i);
		if (ep->ep_type == ENDPOINT_TRANSFER_TYPE_BULK) {
			if (ep->dir == dir) {
				ep_ = ep;
				break;
			}
		}
	}
	return (void*)ep_;
}
/*
 * USBScheduleInterrupt -- schedules an interrupt for a device
 * @param dev -- Pointer to USB Device
 * @param slot -- Controller slot
 * @param ep -- Endpoint data
 * @param physdata -- Physical memory area
 */
void USBScheduleInterrupt(AuUSBDevice *usbdev,void* ep, uint64_t physdata, void (*callback)(void* dev, void* slot, void* Endp)) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return;
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return;
	XHCIEndpoint* ep_ = (XHCIEndpoint*)ep;
	if (!ep_->callback)
		ep_->callback = callback;

	XHCISendNormalTRB(dev, slot, physdata, ep_->max_packet_sz, ep_);
}

void USBControlTransfer(AuUSBDevice* usbdev,const USB_REQUEST_PACKET* request, uint64_t buffer_addr, const size_t len) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return;
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return;
	XHCISendControlCmd(dev, slot, slot->slot_id, request, buffer_addr, len);
}

/*
 * USBBulkTransfer -- bulk transfer callback
 * @param usbdev -- Pointer to USB Device data structure
 * @param buffer -- Pointer to buffer
 * @param data_len -- Data length
 * @param ep -- Pointer to endpoint data structure
 */
void USBBulkTransfer(AuUSBDevice* usbdev, uint64_t buffer, uint16_t data_len, void* ep) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return;
	XHCISlot* slot = (XHCISlot*)usbdev->data;
	if (!slot)
		return;
	XHCIEndpoint* ep_ = (XHCIEndpoint*)ep;
	if (!ep_)
		return;

	XHCIBulkTransfer(dev, slot, buffer, data_len, ep_);
}


int USBPollWait(AuUSBDevice* usb, int poll_type) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return -1;
	int t_idx = XHCIPollEvent(dev, poll_type);
	return t_idx;
}

/*
 * USBGetMaxPacketSize -- returns the maximum packet
 * size of an endpoint
 * @param ep -- Pointer to endpoint
 */
int USBGetMaxPacketSize(AuUSBDevice *dev_,void* ep) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return -1;
	XHCIEndpoint* ep_ = (XHCIEndpoint*)ep;
	if (!ep_)
		return -1;
	return ep_->max_packet_sz;
}


uint8_t USBGetEndpointAttribute(AuUSBDevice* dev_, void* ep) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return 0;
	XHCIEndpoint* endp = (XHCIEndpoint*)ep;
	if (!endp)
		return 0;
	return endp->endpointAttr;
}

uint8_t USBGetEndpointAddress(AuUSBDevice* dev_, void* ep) {
	XHCIDevice* dev = XHCIGetHost();
	if (!dev)
		return 0;
	XHCIEndpoint* endp = (XHCIEndpoint*)ep;
	if (!endp)
		return 0;
	return endp->endpointAddress;
}


/*
 * USBDeviceSetFunctions -- setup the device data structure with
 * all the function pointers
 * @param dev -- Pointer to USB Device
 */
void USBDeviceSetFunctions(AuUSBDevice* dev) {
	dev->schedule_interrupt = USBScheduleInterrupt;
	dev->get_device_desc = USBGetDeviceDesc;
	dev->get_config_desc = USBGetConfigDesc;
	dev->get_string_desc = USBGetStringDesc;
	dev->get_descriptor = USBGetDescriptor;
	dev->get_endpoint = USBGetEndpoint;
	dev->get_bulk_ep = USBGetBulkEndpoint;
	dev->get_max_pack_sz = USBGetMaxPacketSize;
	dev->get_endpoint_address = USBGetEndpointAddress;
	dev->get_endpoint_attrib = USBGetEndpointAttribute;
	dev->set_config_val = USBSetConfigDesc;
	dev->control_transfer = USBControlTransfer;
	dev->bulk_transfer = USBBulkTransfer;
	dev->poll_wait = USBPollWait;
}