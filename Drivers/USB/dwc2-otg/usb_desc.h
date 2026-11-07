/**
* @file usb_desc.h
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

#ifndef __USB_DESC_H__
#define __USB_DESC_H__

#include <stdint.h>

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
typedef struct _string_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	//..string value
}usb_string_desc_t;
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
typedef struct _usb_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
}usb_descriptor_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _usb_hub_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t bPwrOn2PwrGood;
	uint8_t bHubContrCurrent;
}usb_hub_desc_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _usb_ep_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
}usb_ep_desc_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
	uint16_t wPortStatus;
	uint16_t wPortChange;
}usb_port_status_t;
#pragma pack(pop)

#define DESCRIPTOR_TYPE_CONFIG 2
#define DESCRIPTOR_TYPE_INTERFACE 4
#define DESCRIPTOR_TYPE_ENDPOINT 5

#define USB_EP_TYPE_CONTROL 0x00
#define USB_EP_TYPE_ISOC 0x01
#define USB_EP_TYPE_BULK 0x02
#define USB_EP_TYPE_INTERRUPT 0x03

// Request Types
#define REQUEST_OUT			0
#define REQUEST_IN			0x80

#define REQUEST_CLASS			0x20
#define REQUEST_VENDOR			0x40

#define REQUEST_TO_INTERFACE		1
#define REQUEST_TO_OTHER		3

// Standard Request Codes
#define GET_STATUS			0
#define CLEAR_FEATURE			1
#define SET_FEATURE			3
#define SET_ADDRESS			5
#define GET_DESCRIPTOR			6
#define SET_CONFIGURATION		9
#define SET_INTERFACE			11

// Port definitions
#define PORT_STATUS_CONNECTION  (1<<0)  //1-device connected, 0- no device connected
#define PORT_STATUS_ENABLED     (1<<1)
#define PORT_STATUS_SUSPEND     (1<<2)
#define PORT_STATUS_OVERCURRENT (1<<3)
#define PORT_STATUS_RESET       (1<<4)
#define PORT_STATUS_POWER       (1<<8)
#define PORT_STATUS_LOW_SPEED   (1<<9)
#define PORT_STATUS_HIGH_SPEED   (1<<10)
#define PORT_STATUS_TEST         (1<<11)
#define PORT_STATUS_INDICATOR    (1<<12)

#define PORT_RESET 4
#define PORT_POWER 8

// Speed
typedef enum
{
	USBSpeedLow,
	USBSpeedFull,
	USBSpeedHigh,
	USBSpeedUnknown
};

/**
 * @brief dwc2_get_descriptor -- find a descriptor within descriptor config
 * area
 * @param desc -- pointer to config descriptor
 * @param type -- type of the descriptor
 */
extern usb_descriptor_t* dwc2_get_descriptor(void* desc, uint8_t type);

#endif
