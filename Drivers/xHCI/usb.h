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

#ifndef __USB_H__
#define __USB_H__

#include <stdint.h>
#include "xhci.h"

/* Standard USB Requests */
#define USB_BM_REQUEST_OUTPUT 0
#define USB_BM_REQUEST_INPUT  0x80
#define USB_BM_REQUEST_STANDARD 0
#define USB_BM_REQUEST_CLASS  0x20
#define USB_BM_REQUEST_VENDOR 0x40
#define USB_BM_REQUEST_DEVICE  0
#define USB_BM_REQUEST_INTERFACE 1
#define USB_BM_REQUEST_ENDPOINT 2
#define USB_BM_REQUEST_OTHER 3
#define USB_BM_REQUEST_VENDORSPEC  0x1F

#define USB_BREQUEST_GET_STATUS  0
#define USB_BREQUEST_CLEAR_FEATURE 1
#define USB_BREQUEST_SET_FEATURE 3
#define USB_BREQUEST_SET_ADDRESS 5
#define USB_BREQUEST_GET_DESCRIPTOR 6
#define USB_BREQUEST_SET_DESCRIPTOR 7
#define USB_BREQUEST_GET_CONFIGURATION 8
#define USB_BREQUEST_SET_CONFIGURATION 9
#define USB_BREQUEST_GET_INTERFACE  10
#define USB_BREQUEST_SET_INTERFACE  11

#define USB_DESCRIPTOR_DEVICE  1
#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_STRING  3
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT  5
#define USB_DESCRIPTOR_SUPERSPEED_ENDP_CMP 48

#define USB_DESCRIPTOR_WVALUE(type,index) ((type << 8) | index)

#define USB_AUDIO_DEV_CLASS   0x01
#define USB_COMMUNICATION_CDC_CTRL_CLASS 0x02
#define USB_HID_DEV_CLASS  0x03
#define USB_PHYSICAL_DEV_CLASS 0x05
#define USB_IMAGE_DEV_CLASS 0x06
#define USB_PRINTER_DEV_CLASS  0x07
#define USB_MASS_STORAGE_DEV_CLASS  0x08
#define USB_HUB_DEV_CLASS  0x09
#define USB_CDC_DATA_DEV_CLASS  0x0A
#define USB_SMART_CARD_DEV_CLASS 0x0B
#define USB_CONTENT_SECURITY_DEV_CLASS 0x0D
#define USB_VIDEO_DEV_CLASS 0x0E
#define USB_PERSONAL_HEALTHCARE_DEV_CLASS 0x0F
#define USB_AUDIO_VIDEO_DEV_CLASS 0x10
#define USB_BILLBOARD_DEV_CLASS 0x11
#define USB_TYPE_C_BRIDGE_CLASS  0x12
#define USB_I3C_DEV_CLASS  0x3C
#define USB_DIAGNOSTIC_DEV_CLASS 0xDC
#define USB_WIRELESS_CONTROLLER_DEV_CLASS 0xE0
#define USB_MISCELLANEOUS_DEV_CLASS 0xEF

/* transfer type defined in endpoint descriptor*/
#define ENDPOINT_TRANSFER_TYPE_CONTROL 0
#define ENDPOINT_TRANSFER_TYPE_ISOCH 1
#define ENDPOINT_TRANSFER_TYPE_BULK  2
#define ENDPOINT_TRANSFER_TYPE_INT   3



#pragma pack(push,1)
typedef struct _dev_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t  bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t  iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
}usb_dev_desc_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _interface_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
}usb_if_desc_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _qualifier_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint8_t bNumConfigurations;
	uint8_t bReserved;
}usb_qualifier_desc_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _config_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
}usb_config_desc_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _string_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	//..string value
}usb_string_desc_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _endpoint_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
}usb_endpoint_desc_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _usb_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
}usb_descriptor_t;
#pragma pack(pop)

struct _usb_dev_;

typedef int(*usb_drv_entry)(_usb_dev_* dev);
typedef int(*usb_drv_unload)(_usb_dev_* dev);



#pragma pack(push,1)
typedef struct _usb_dev_ {
	void* data;
	uint32_t classCode;
	uint32_t subClassCode;
	uint16_t vendorID;
	uint16_t deviceID;
	int configValue;
	uint16_t usbVersion;
	uint8_t address;
	uint8_t protocol;
	void* descriptor;
	int numInterfaces;
	int numEndpoint;
	bool driverInitialized;
	void (*schedule_interrupt)(_usb_dev_* dev, void* ep, uint64_t buffer, void (*callback)(void* dev, void* slot, void* Endp));
	void (*control_transfer)(_usb_dev_* usbdev, const USB_REQUEST_PACKET* request, uint64_t buffer_addr, const size_t len);
	void (*bulk_transfer)(_usb_dev_* usbdev, uint64_t buffer, uint16_t data_len, void* ep);
	void (*get_device_desc)(_usb_dev_* dev, uint64_t buffer, uint16_t len);
	void (*get_string_desc)(_usb_dev_* dev, uint64_t buffer, uint16_t id);
	void (*get_config_desc)(_usb_dev_* dev, uint64_t buffer, uint16_t len, uint8_t id);
	usb_descriptor_t* (*get_descriptor)(_usb_dev_* dev, uint8_t type);
	void* (*get_endpoint)(_usb_dev_* dev, uint8_t ep_type);
	void* (*get_bulk_ep)(_usb_dev_* dev, uint8_t dir);
	int (*get_max_pack_sz)(_usb_dev_* dev, void* ep);
	uint8_t(*get_endpoint_address)(_usb_dev_* dev, void* ep);
	uint8_t(*get_endpoint_attrib)(_usb_dev_* dev, void* ep);
	void (*set_config_val)(_usb_dev_* dev, uint8_t config_val);
	int (*poll_wait)(_usb_dev_* dev, int wait_type);
	usb_drv_entry ClassEntry;
	usb_drv_unload ClassUnload;
}AuUSBDevice;
#pragma pack(pop)


/*
 * USBCreateDevice -- create aurora usb device
 */
extern AuUSBDevice* USBCreateDevice();
/*
* USBGetDeviceDesc -- sends USB_GET_DESCRIPTOR request to specific device
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param buffer -- address of the buffer where device descriptor will be written
* @param len -- number of bytes needs to be requested
*/
extern void USBGetDeviceDesc(AuUSBDevice* usbdev,  uint64_t buffer, uint16_t len);

/*
* USBGetStringDesc -- request a string descriptor to specific device
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param buffer -- address of the buffer where device descriptor will be written
* @param id-- type of the string needs to be requested
*/
extern void USBGetStringDesc(AuUSBDevice* usbdev,uint64_t buffer, uint16_t id);

/*
 * USBGetConfigDesc -- get configuration descriptor
 * @param dev -- Pointer to usb device structure
 * @param slot -- Pointer to usb slot data structure
 * @param slot_id -- Slot id of the device
 * @param buffer -- address of the buffer where device descriptor will be written
 */
extern void USBGetConfigDesc(AuUSBDevice* usbdev,uint64_t buffer, uint16_t len, uint8_t id);

/*
* USBGetConfigDesc -- get configuration descriptor
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param configval -- config value returned in configuration descriptor
*/
extern void USBSetConfigDesc(AuUSBDevice* usbdev, uint8_t configval);

/*
 * USBScheduleInterrupt -- schedules an interrupt for a device
 * @param dev -- Pointer to USB Device
 * @param slot -- Controller slot
 * @param ep -- Endpoint data
 * @param physdata -- Physical memory area
 */
extern void USBScheduleInterrupt(AuUSBDevice* usbdev, void * ep, uint64_t physdata);

//extern void USBControlTransfer(XHCIDevice* dev, XHCISlot* slot, uint8_t slot_id, const USB_REQUEST_PACKET* request, uint64_t buffer_addr, const size_t len,
//	uint8_t trt);


/*
 * USBDeviceSetFunctions -- setup the device data structure with
 * all the function pointers
 * @param dev -- Pointer to USB Device
 */
extern void USBDeviceSetFunctions(AuUSBDevice* dev);
#endif
