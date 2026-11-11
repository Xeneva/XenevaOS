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
#ifndef __DWC2_USB_H__
#define __DWC2_USB_H__

#include <stdint.h>
#include <_null.h>

/* USB 2.0 chapter 9 / chapter 11 / HID 1.11 definitions used by the driver */

#pragma pack(push, 1)
typedef struct {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} usb_setup_t;

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
} usb_device_desc_t;

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
} usb_config_desc_t;

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
} usb_interface_desc_t;

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
} usb_endpoint_desc_t;

typedef struct {
	uint8_t bDescLength;
	uint8_t bDescriptorType;
	uint8_t bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t bPwrOn2PwrGood;
	uint8_t bHubContrCurrent;
} usb_hub_desc_t;

typedef struct {
	uint16_t wPortStatus;
	uint16_t wPortChange;
} usb_port_status_t;
#pragma pack(pop)

/* bmRequestType */
#define USB_DIR_OUT		  0x00
#define USB_DIR_IN		  0x80
#define USB_TYPE_STANDARD 0x00
#define USB_TYPE_CLASS	  0x20
#define USB_RECIP_DEVICE  0x00
#define USB_RECIP_INTERFACE 0x01
#define USB_RECIP_OTHER	  0x03

/* standard requests */
#define USB_REQ_GET_STATUS		  0
#define USB_REQ_CLEAR_FEATURE	  1
#define USB_REQ_SET_FEATURE		  3
#define USB_REQ_SET_ADDRESS		  5
#define USB_REQ_GET_DESCRIPTOR	  6
#define USB_REQ_SET_CONFIGURATION 9

/* descriptor types */
#define USB_DT_DEVICE	 1
#define USB_DT_CONFIG	 2
#define USB_DT_STRING	 3
#define USB_DT_INTERFACE 4
#define USB_DT_ENDPOINT	 5
#define USB_DT_HID		 0x21
#define USB_DT_REPORT	 0x22
#define USB_DT_HUB		 0x29

/* classes */
#define USB_CLASS_HID 0x03
#define USB_CLASS_HUB 0x09

/* hub port features (USB 2.0 table 11-17) */
#define USB_PORT_FEAT_RESET		   4
#define USB_PORT_FEAT_POWER		   8
#define USB_PORT_FEAT_C_CONNECTION 16
#define USB_PORT_FEAT_C_ENABLE	   17
#define USB_PORT_FEAT_C_RESET	   20

/* hub port status / change bits (USB 2.0 tables 11-21, 11-22) */
#define USB_PORT_STAT_CONNECTION (1u << 0)
#define USB_PORT_STAT_ENABLE	 (1u << 1)
#define USB_PORT_STAT_RESET		 (1u << 4)
#define USB_PORT_STAT_POWER		 (1u << 8)
#define USB_PORT_STAT_LOW_SPEED	 (1u << 9)
#define USB_PORT_STAT_HIGH_SPEED (1u << 10)
#define USB_PORT_STAT_C_CONNECTION (1u << 0)
#define USB_PORT_STAT_C_RESET	 (1u << 4)

/* HID class requests and boot interface */
#define HID_REQ_SET_IDLE	 0x0A
#define HID_REQ_SET_PROTOCOL 0x0B
#define HID_SUBCLASS_BOOT	 1
#define HID_PROTOCOL_KEYBOARD 1
#define HID_PROTOCOL_MOUSE	 2

/* errors */
#define USB_OK		0
#define USB_EAGAIN	(-11) /* NAK: no data this time */
#define USB_EIO		(-5)
#define USB_ETIME	(-62)
#define USB_ESTALL	(-32)
#define USB_ENODEV	(-19)

#define USB_MAX_DEVICES 16
#define USB_MAX_DEPTH	5

/* a device on the bus */
typedef struct _usb_device_ {
	uint8_t used;
	uint8_t address;
	uint8_t speed;	  /* DWC2_SPEED_HIGH / FULL / LOW */
	uint8_t depth;
	uint16_t mps0;	  /* endpoint 0 max packet size */
	struct _usb_device_* parent; /* NULL: on the root port */
	uint8_t port;	  /* port on the parent hub */
	/* full/low speed device behind a high speed hub: transactions go
	 * through that hub's transaction translator as split transactions */
	uint8_t split;
	uint8_t tt_hub;	  /* address of the high speed hub with the TT */
	uint8_t tt_port;  /* its port the device (or its hub chain) is on */
	uint8_t toggle_in[16];	/* next data PID per IN endpoint */
	uint8_t toggle_out[16];
	usb_device_desc_t desc;
} usb_device_t;

/* host controller (dwc2_hcd.cpp) */
extern int dwc2_hcd_init();
extern int dwc2_root_port_reset(uint8_t* speed);
extern uint8_t dwc2_root_port_speed();
extern int usb_control_msg(usb_device_t* dev, uint8_t request_type, uint8_t request, uint16_t value,
						   uint16_t index, void* data, uint16_t length);
extern int usb_interrupt_in(usb_device_t* dev, uint8_t ep, uint16_t mps, void* data, int length);

/* enumeration (usb_enum.cpp) */
extern int usb_enumerate_root();

/* time helpers (dwc2_hcd.cpp) */
extern uint64_t dwc2_now_us();
extern void dwc2_delay_us(uint64_t us);
extern void dwc2_delay_ms(uint64_t ms);

#endif
