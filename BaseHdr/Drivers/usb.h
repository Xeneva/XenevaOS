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

#ifndef __AURORA_USB_H__
#define __AURORA_USB_H__

#include <stdint.h>
#include <string.h>
#include <_null.h>

#pragma pack(push,1)
typedef struct _usb_request_pack {
	uint8_t request_type;
	uint8_t request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
}AuUSBRequestPacket;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _au_usb_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
}AuUSBDescriptor;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _au_dev_desc_ {
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
}AuUSBDevDesc;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _au_interface_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
}AuUSBInterfaceDesc;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _au_qualifier_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint8_t bNumConfigurations;
	uint8_t bReserved;
}AuUSBQualifierDesc;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _au_config_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
}AuUSBConfigDesc;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _au_string_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	//..string value
}AuUSBStringDesc;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _au_endpoint_desc_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
}AuUSBEndpointDesc;
#pragma pack(pop)


/* Standard XHCI defined Transfer/Command/Event
* TRB type values
*/
#define USB_WAIT_TRANSFER_NORMAL                 1
#define USB_WAIT_TRANSFER_SETUP_STAGE            2
#define USB_WAIT_TRANSFER_DATA_STAGE             3
#define USB_WAIT_TRANSFER_STATUS_STAGE           4
#define USB_WAIT_TRANSFER_ISOCH                  5
#define USB_WAIT_TRANSFER_LINK                   6
#define USB_WAIT_TRANSFER_EVENT_DATA             7
#define USB_WAIT_TRANSFER_NO_OP                  8
#define USB_WAIT_CMD_ENABLE_SLOT                 9
#define USB_WAIT_CMD_DISABLE_SLOT                10
#define USB_WAIT_CMD_ADDRESS_DEV                 11
#define USB_WAIT_CMD_CONFIG_ENDPOINT             12
#define USB_WAIT_CMD_EVALUATE_CTX                13
#define USB_WAIT_CMD_RESET_ENDPOINT              14
#define USB_WAIT_CMD_STOP_ENDPOINT               15
#define USB_WAIT_CMD_SET_TR_DEQ_POINTER          16
#define USB_WAIT_CMD_RESET_DEV                   17
#define USB_WAIT_CMD_FORCE_EVENT                 18
#define USB_WAIT_CMD_NEGOTIATE_BANDWIDTH          19
#define USB_WAIT_CMD_SET_LATENCY_TOLERANCE_VALUE  20
#define USB_WAIT_CMD_GET_PORT_BANDWIDTH           21
#define USB_WAIT_CMD_FORCE_HEADER                 22
#define USB_WAIT_CMD_NO_OP                        23
#define USB_WAIT_CMD_GET_EXT_PROPERTY             24
#define USB_WAIT_CMD_SET_EXT_PROPERTY             25
#define USB_WAIT_EVENT_TRANSFER                   32
#define USB_WAIT_EVENT_CMD_COMPLETION             33
#define USB_WAIT_EVENT_PORT_STATUS_CHANGE         34
#define USB_WAIT_EVENT_BANDWIDTH_REQUEST          35
#define USB_WAIT_EVENT_DOORBELL                   36
#define USB_WAIT_EVENT_HOST_CONTROLLER            37
#define USB_WAIT_EVENT_DEVICE_NOTIFICATION        38
#define USB_WAIT_EVENT_MFINDEX                    39

#define USB_DESCRIPTOR_WVALUE(type,index) ((type << 8) | index)

/* transfer type defined in endpoint descriptor*/
#define ENDPOINT_TRANSFER_TYPE_CONTROL 0
#define ENDPOINT_TRANSFER_TYPE_ISOCH 1
#define ENDPOINT_TRANSFER_TYPE_BULK  2
#define ENDPOINT_TRANSFER_TYPE_INT   3

struct _au_usb_dev_;

typedef int(*au_usb_drv_entry)(struct _au_usb_dev_* dev);
typedef int(*au_usb_drv_unload)(struct _au_usb_dev_* dev);


typedef void (*schedule_interrupt_callback)(struct _au_usb_dev_* controller, void* ep, uint64_t buffer, void (*callback)(void* dev, void* slot, void* Endp));
typedef void (*control_transfer)(struct _au_usb_dev_* usbdev, const AuUSBRequestPacket* request, uint64_t buffer_addr, const size_t len);
typedef void (*bulk_transfer)(struct _au_usb_dev_* usbdev, uint64_t buffer_addr, uint16_t len, void* ep);
typedef void (*get_device_desc_callback)(struct _au_usb_dev_* dev, uint64_t buffer, uint16_t len);
typedef void (*get_string_desc_callback)(struct _au_usb_dev_* dev, uint64_t buffer, uint16_t id);
typedef void (*get_config_desc_callback)(struct _au_usb_dev_* dev, uint64_t buffer, uint16_t len, uint8_t id);
typedef void* (*get_endpoint)(struct _au_usb_dev_* dev, uint8_t ep_type);
typedef void* (*get_bulk_ep)(struct _au_usb_dev_* dev, uint8_t dir);
typedef int (*get_max_pack_sz)(struct _au_usb_dev_* dev, void* ep);
typedef uint8_t(*get_endpoint_address)(struct _au_usb_dev_* dev, void* ep);
typedef uint8_t(*get_endpoint_attrib)(struct _au_usb_dev_* dev, void* ep);
typedef AuUSBDescriptor* (*get_descriptor_callback)(struct _au_usb_dev_* dev, uint8_t type);
typedef void (*set_config_val_callback)(struct _au_usb_dev_* dev, uint8_t config_val);
typedef int (*poll_wait_callback)(struct _au_usb_dev_* dev, int poll_wait);

#pragma pack(push,1)
typedef struct _au_usb_dev_ {
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
	schedule_interrupt_callback AuScheduleInterrupt;
	control_transfer AuControlTransfer;
	bulk_transfer AuBulkTranfer;
	get_device_desc_callback AuGetDeviceDescriptor;
	get_string_desc_callback AuGetStringDescriptor;
	get_config_desc_callback AuGetConfigDescriptor;
	get_descriptor_callback AuGetDescriptor;
	get_endpoint AuGetEndpoint;
	get_bulk_ep AuGetBulkEndpoint;
	get_max_pack_sz AuGetMaxPacketSize;
	get_endpoint_address AuGetEndpointAddress;
	get_endpoint_attrib AuGetEndpointAttrib;
	set_config_val_callback AuSetConfigValue;
	poll_wait_callback AuUSBWait;
	au_usb_drv_entry ClassEntry;
	au_usb_drv_unload ClassUnload;
}AuUSBDeviceStruc;
#pragma pack(pop)


/*
 * AuUSBSubsystemInit -- initialize the
 * aurora usb system
 */
extern void AuUSBSubsystemInit();

/*
 * AuXhciInitialize -- initialize the xHCI host controller
 * Scans PCI for xHCI (class 0x0c/0x03/0x30), brings it up, and
 * begins polling for Bluetooth HID adapters.
 */
extern void AuXhciInitialize(void);

/*
 * AuXhciStartPolling -- start the xHCI HID/Bluetooth service threads
 * Called after the scheduler's thread list is initialized.
 */
extern void AuXhciStartPolling(void);


/*
 * AuUSBGetDeviceStruc -- returns USB device struc by looking
 * its data field
 * @param data -- Pointer to host controller related data
 */
AU_EXTERN AU_EXPORT void* AuUSBGetDeviceStruc(void* data);
/*
 * AuUSBDeviceDisconnect -- device detach callback
 * called from host controller
 * @param dev -- Pointer to USB Device structure
 */
AU_EXTERN AU_EXPORT void AuUSBDeviceDisconnect(AuUSBDeviceStruc* dev);

/*
 * AuUSBDeviceConnect -- function is called from the host
 * driver whenever a port change event occurs
 * @param device -- Pointer to AuUSBDevice passed by the host
 */
AU_EXTERN AU_EXPORT void AuUSBDeviceConnect(AuUSBDeviceStruc* device);
#endif
