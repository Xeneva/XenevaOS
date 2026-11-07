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

#ifndef __USB3_H__
#define __USB3_H__

#include <stdint.h>
#include "xhci.h"
#include <Hal/x86_64_sched.h>
#include <list.h>
#include <Sync/spinlock.h>

#pragma pack(push,1)
typedef struct _usb_dev_ {
	bool initialised;
	bool is_csz_64;
	AuThread *usb_thread;
	list_t *slot_list;
	xhci_cap_regs_t* cap_regs;
	xhci_op_regs_t* op_regs;
	xhci_doorbell_regs_t* db_regs;
	xhci_runtime_regs_t* rt_regs;
	xhci_ext_cap_t* ext_cap;
	xhci_port_regs_t* ports;
	uint32_t num_slots;
	uint32_t num_ports;
	uint16_t max_intrs;
	uint8_t irq;
	uint64_t* dev_ctx_base_array;
	xhci_trb_t* cmd_ring;
	uint64_t cmd_ring_phys;
	unsigned cmd_ring_index;
	unsigned cmd_ring_max;
	unsigned cmd_ring_cycle;
	uint64_t* event_ring_segment;
	uint64_t* event_ring_seg_phys;
	unsigned evnt_ring_index;
	unsigned evnt_ring_cycle;
	unsigned evnt_ring_max;
	int poll_event_for_trb;
	bool event_available;
	int poll_return_trb_type;
	int trb_event_index;
	Spinlock *usb_lock;
}USBDevice;
#pragma pack(pop)

/* USBMessage structure */
#pragma pack(push,1)
typedef struct __usb_msg__ {
	uint8_t type;
	uint8_t uchar1;
	uint8_t uchar2;
	uint8_t uchar3;
	uint8_t uchar4;
	uint8_t uchar5;
	uint16_t ushort1;
	uint16_t ushort2;
	uint32_t dword1;
	uint32_t dword2;
}USBMessage;
#pragma pack(pop)

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
struct USB_REQUEST_PACKET {
	uint8_t request_type;
	uint8_t request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
};
#pragma pack(pop)

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
/*
* USBGetDeviceDesc -- sends USB_GET_DESCRIPTOR request to specific device
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param buffer -- address of the buffer where device descriptor will be written
* @param len -- number of bytes needs to be requested
*/
extern void USBGetDeviceDesc(USBDevice *dev, XHCISlot *slot, uint8_t slot_id, uint64_t buffer, uint16_t len);

/*
* USBGetStringDesc -- request a string descriptor to specific device
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param buffer -- address of the buffer where device descriptor will be written
* @param id-- type of the string needs to be requested
*/
extern void USBGetStringDesc(USBDevice *dev, XHCISlot *slot, uint8_t slot_id, uint64_t buffer, uint16_t id);

/*
* USBGetConfigDesc -- get configuration descriptor
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param buffer -- address of the buffer where device descriptor will be written
*/
void USBGetConfigDesc(USBDevice* dev, XHCISlot* slot, uint8_t slot_id, uint64_t buffer, uint16_t len, uint8_t id);

/*
* USBGetConfigDesc -- get configuration descriptor
* @param dev -- Pointer to usb device structure
* @param slot -- Pointer to usb slot data structure
* @param slot_id -- Slot id of the device
* @param configval -- config value returned in configuration descriptor
*/
void USBSetConfigDesc(USBDevice* dev, XHCISlot* slot, uint8_t slot_id, uint8_t configval);

/*
* XHCIPollEvent -- waits for an event to occur on interrupts
* @param usb_device -- pointer to usb device structure
* @param trb_type -- type of trb to look
* @return trb_event_index -- index of the trb on event_ring_segment
*/
extern int XHCIPollEvent(USBDevice* usb_device, int trb_type);

/*
* XHCISendCommand -- Sends command to USB device through XHCI
* host
* @param dev -- Pointer to usb device structure
* @param param1 -- parameter 1 of TRB
* @param param2 -- parameter 2 of TRB
* @param status -- status of TRB
* @param ctrl -- control for TRB
*/
extern void XHCISendCommand(USBDevice *dev, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl);

/*
* XHCISendCommandSlot -- sends command to slot trb
* @param slot -- pointer to slot data structure
* @param param1 -- first parameter of trb structure
* @param param2 -- 2nd parameter of trb structure
* @param status -- status field of trb structure
* @param ctrl -- control field of trb structure
*/
extern void XHCISendCommandSlot(XHCISlot* slot, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl);

/*
* XHCISendCommandEndpoint -- sends command to endpoint trb
* @param slot -- pointer to slot data structure
* @param endp_num -- endpoint number
* @param param1 -- first parameter of trb structure
* @param param2 -- 2nd parameter of trb structure
* @param status -- status field of trb structure
* @param ctrl -- control field of trb structure
*/
void XHCISendCommandEndpoint(XHCISlot* slot, uint8_t endp_num, uint32_t param1, uint32_t param2, uint32_t status, uint32_t ctrl);

/*
* XHCIRingDoorbell -- rings the host doorbell
* @param dev -- Pointer to usb structure
*/
extern void XHCIRingDoorbell(USBDevice* dev);

/*
* XHCIRingDoorbellSlot -- rings the doorbell by slot
* @param dev -- Pointer to usb structure
* @param slot -- slot id
* @param endpoint -- endpoint number, it should be 0 if
* the slot is 0, else values 1-255 should be placed
*
*/
extern void XHCIRingDoorbellSlot(USBDevice* dev, uint8_t slot, uint32_t endpoint);

/*
* XHCISendCommandMultiple -- sends multiple commands to the command ring
* @param dev -- Pointer to usb structure
* @param trb -- TRB address containing multiple TRBs
* @param num_count -- counts of TRB to send
*/
extern void XHCISendCommandMultiple(USBDevice* dev, xhci_trb_t* trb, int num_count);

/*
* XHCIReset -- reset the xhci controller
* @param dev -- pointer to USB device
* structure
*/
extern void XHCIReset(USBDevice *dev);

/*
* XHCIDeviceContextInit -- initialise the
* device context base array pointer (DCBAAP)
* @param dev -- Pointer to USB device
* structure
*/
extern void XHCIDeviceContextInit(USBDevice *dev);

/*
* XHCICommandRingInit -- initialise the command ring
* buffer
* @param dev -- Pointer to usb device
*/
extern void XHCICommandRingInit(USBDevice *dev);

/*
* XHCIProtocolInit -- Initialize XHCI Extended Protocol
* @param dev -- Pointer to usb device
*/
extern void XHCIProtocolInit(USBDevice *dev);

/*
* xhci_event_ring_init -- initialize the
* default event ring
* @param dev -- Pointer to usb device
*/
extern void XHCIEventRingInit(USBDevice *dev);

/*
* XHCIStartDefaultPorts -- initializes all powered ports
* @param dev -- Pointer to USB device structures
*/
extern void XHCIStartDefaultPorts(USBDevice *dev);

/*
* XHCIPortInitialize -- initializes a specific port
* @param dev -- Pointer to USB device structures
* @param port -- number of the port
*/
extern void XHCIPortInitialize(USBDevice *dev, unsigned int port);

/*
* XHCIGetSlotByID -- returns a slot from slot list
* @param dev -- Pointer to USB device
* @param slot_id -- slot id
*/
extern XHCISlot* XHCIGetSlotByID(USBDevice* dev, uint8_t slot_id);


/*
* XHCISlotGetEP_DCI -- returns an endpoint by its endpoint number
*/
extern XHCIEndpoint *XHCISlotGetEP_DCI(XHCISlot* slot, uint8_t endp_num);

/*
* USBGetMainDevice -- returns the
* main usb device structure
*/
extern USBDevice* USBGetMainDevice();

/*
* XHCISlotGetEP -- returns an endpoint by its endpoint number
*/
extern XHCIEndpoint *XHCISlotGetEP(XHCISlot* slot, uint8_t endp_num);

/*
 * USBGetDescriptor -- checks and return a descriptor of given
 * type
 * @param slot -- XHCI Provided slot
 * @param type -- type of the descriptor
 */
extern usb_descriptor_t* USBGetDescriptor(XHCISlot* slot, uint8_t type);

/*
 * USBScheduleInterrupt -- schedules an interrupt for a device
 * @param dev -- Pointer to USB Device
 * @param slot -- Controller slot
 * @param ep -- Endpoint data
 * @param physdata -- Physical memory area
 */
extern void USBScheduleInterrupt(USBDevice* dev, XHCISlot* slot, XHCIEndpoint* ep, uint64_t physdata);
#endif