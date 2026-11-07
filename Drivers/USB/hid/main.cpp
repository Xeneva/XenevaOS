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

#include <aurora.h>
#include <Drivers/usb.h>
#include <aucon.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include "hid.h"
#include <Fs/Dev/devinput.h>
#include <Hal/serial.h>

/*
 * TODO : HID Application : Keyboard & Joystick
 */


#define HID_DESCRIPTOR_HID 0x21
#define HID_DESCRIPTOR_REPORT 0x22
#define HID_DESCRIPTOR_PHYSICAL 0x23



/* Main item tags */
#define HID_MAIN_ITEM_TAG_INPUT 0x80  //1000 00 nn
#define HID_MAIN_ITEM_TAG_OUTPUT 0x90 //100100 nn
#define HID_MAIN_ITEM_TAG_FEATURE 0xB0 //101100 nn
#define HID_MAIN_ITEM_TAG_COLLECTION 0xA0 //101000 nn
#define HID_MAIN_ITEM_TAG_END_COLLECTION 0xC0 //110000 nn

#define HID_GLOBAL_ITEM_TAG_USAGE_PAGE 0x4 //0000 01 nn
#define HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM 0x14 //000101 nn
#define HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM 0x24 //001001 nn
#define HID_GLOBAL_ITEM_TAG_PHYSICAL_MINIMUM 0x34 //001101 nn
#define HID_GLOBAL_ITEM_TAG_PHYSICAL_MAXIMUM 0x44 //010001 nn
#define HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT 0x54 //010101 nn
#define HID_GLOBAL_ITEM_TAG_UNIT 0x64 //011001 nn
#define HID_GLOBAL_ITEM_TAG_REPORT_SIZE 0x74 //0111 01 nn
#define HID_GLOBAL_ITEM_TAG_REPORT_ID 0x84 //1000 01 nn
#define HID_GLOBAL_ITEM_TAG_REPORT_COUNT 0x94 //100101 nn
#define HID_GLOBAL_ITEM_TAG_PUSH 0xA4 //101001 nn
#define HID_GLOBAL_ITEM_TAG_POP 0xB4 //101101 nn

#define HID_LOCAL_ITEM_TAG_USAGE 0x8 //0000 10 nn
#define HID_LOCAL_ITEM_TAG_USAGE_MINIMUM 0x18 //0001 10 nn
#define HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM 0x28 //001010 nn
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_INDEX 0x38 //001110 nn
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_MINIMUM 0x48 //010010 nn
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_MAXIMUM 0x58 //010110 nn
#define HID_LOCAL_ITEM_TAG_STRING_INDEX 0x78 //011110 nn
#define HID_LOCAL_ITEM_TAG_STRING_MINIMUM 0x88 //1000 10 nn
#define HID_LOCAL_ITEM_TAG_STRING_MAXIMUM 0x98 //1001 10 nn
#define HID_LOCAL_ITEM_TAG_DELIMITER 0xA8 //101010 nn

#define HID_COLLECTION_TYPE_PHYSICAL 0x00
#define HID_COLLECTION_TYPE_APPLICATION 0x01
#define HID_COLLECTION_TYPE_LOGICAL 0x02
#define HID_COLLECTION_TYPE_REPORT 0x03
#define HID_COLLECTION_NAMED_ARRAY 0x04
#define HID_COLLECTION_USAGE_SWITCH 0x05
#define HID_COLLECTION_USAGE_MODIFIER 0x06


#define HID_ITEM_TYPE_MAIN 0
#define HID_ITEM_TYPE_GLOBAL 1
#define HID_ITEM_TYPE_LOCAL 2
#define HID_ITEM_TYPE_RESERVED 3

#define HID_APPLICATION_GENERIC_DESKTOP_CTL 1
#define HID_APPLICATION_KEYBOARD_KEYPAD 7
#define HID_APPLICATION_BUTTONS 9
#define HID_APPLICATION_LEDS 8

#define HID_USAGE_X_AXIS 0x30
#define HID_USAGE_Y_AXIS 0x31
#define HID_USAGE_Z_AXIS 0x32
#define HID_USAGE_WHEEL 0x38


struct ReportState {
	int usageCount;
	uint16_t usages[32];
	uint32_t application;
	int32_t logicalMinimum, logicalMaximum;
	int32_t physicalMinimum, physicalMaximum;
	uint16_t usage;
	uint8_t bits;
	uint8_t type;
	uint32_t reportCount;
};

#define REPORT_ITEM_INPUT 1
#define REPORT_ITEM_OUTPUT 2
#define REPORT_ITEM_FEATURE 3
#define REPORT_ITEM_USAGE_MOUSE 1

struct ReportItem {
	uint32_t application;
	int32_t logicalMinimum, logicalMaximum;
	int32_t physicalMinimum, physicalMaximum;
	uint16_t usage;
	uint8_t bits;
	uint8_t type;
	uint8_t reportsize;
	uint8_t dataOffset;
	bool signedBits;
	bool relative;
};

/*
* AuUSBDriverUnload -- deattach the driver from
* aurora system
*/
AU_EXTERN AU_EXPORT int AuUSBDriverUnload(AuUSBDeviceStruc* dev) {

	return 0;
}

ReportItem items[32];
uintptr_t reportItemLength;
uint64_t mouse_data;
AuUSBDeviceStruc* device;
void* endpoint;

void SetProtocol(AuUSBDeviceStruc* dev) {
	AuUSBRequestPacket pack;
	pack.request_type = 0x21;
	pack.request = HID_SET_PROTOCOL;
	pack.value = 0;
	pack.index = 0;
	pack.length = 0;
	//XHCISendControlCmd(dev, slot, slot_id, &pack, 0, 0);
	dev->AuControlTransfer(dev, &pack, 0, 0);
}

void SetIDLE(AuUSBDeviceStruc* dev) {
	AuUSBRequestPacket pack;
	pack.request_type = 0x21;
	pack.request = 0x0A;
	pack.value = USB_DESCRIPTOR_WVALUE(0, 0);
	pack.index = 0;
	pack.length = 0;
	//XHCISendControlCmd(dev, slot, slot_id, &pack, 0, 0);
	dev->AuControlTransfer(dev, &pack, 0, 0);
}

void GetReport(AuUSBDeviceStruc* dev, uint64_t buffer, uint16_t report_bytes) {
	AuUSBRequestPacket pack;
	pack.request_type = 0x81;
	pack.request = 0x06;
	pack.value = USB_DESCRIPTOR_WVALUE(0x22, 0);
	pack.index = 0;
	pack.length = report_bytes;
	//XHCISendControlCmd(dev, slot, slot->slot_id, &pack, buffer, report_bytes);
	dev->AuControlTransfer(dev, &pack, buffer, report_bytes);
}

bool HIDParseReportDescriptor(uint8_t* report, size_t reportBytes) {
	int position = 0;

	ReportState state[32];
	memset(&state, 0, sizeof(ReportState) * 32);
	uint8_t stateidx = 0;
	uint8_t current_data_offset;
	while (position < reportBytes) {
		uint8_t item_header = report[position];
		size_t item_size = item_header & 0x3;
		uint8_t item_type = (item_header >> 2) & 0x3;
		uint8_t item_tag = (item_header >> 4) & 0xf;
		uint8_t one_byte_prefix = item_header & ~3;
		int32_t data = 0;
		uint32_t usage = 0;
		if (item_header == 0xFE) {
			//AuTextOut("[USB HID]: Long items, unused \r\n");
			if (position + 3 > reportBytes) return false;
			position += 3 + report[position + 1];
			continue;
		}

		for (uintptr_t i = 0; i < item_size; i++)
			data |= report[(position + 1) + i] << (i * 8);

		int32_t sdata = data;

		if (item_size && (report[position + item_size - 1] & 0x80)) {
			for (uintptr_t i = item_size; i < 4; i++)
				sdata |= 0xff << (i * 8);
		}

		if (item_header != 0) {
			switch (item_type) {
			case HID_ITEM_TYPE_MAIN:
				switch (one_byte_prefix) {
				case HID_MAIN_ITEM_TAG_INPUT:
					//AuTextOut("HID Main item tag input \n");
					if (state[stateidx].usageCount > 1) {
						for (int i = 0; i < state[stateidx].reportCount; i++) {
							items[reportItemLength].application = state[stateidx].application;
							items[reportItemLength].bits = state[stateidx].bits;
							items[reportItemLength].logicalMaximum = state[stateidx].logicalMaximum;
							items[reportItemLength].logicalMinimum = state[stateidx].logicalMinimum;
							items[reportItemLength].type = REPORT_ITEM_INPUT;
							items[reportItemLength].usage = state[stateidx].usages[i];
							items[reportItemLength].reportsize = 1;
							items[reportItemLength].dataOffset = current_data_offset;
							items[reportItemLength].physicalMinimum = state[stateidx].physicalMinimum;
							items[reportItemLength].physicalMaximum = state[stateidx].physicalMaximum;
							if (items[reportItemLength].logicalMinimum < 0 || items[reportItemLength].logicalMaximum < 0)
								items[reportItemLength].signedBits = true;
							if ((data & (1 << 2)) == 1)
								items[reportItemLength].relative = 1;
							reportItemLength++;
						}
					}
					else {
						items[reportItemLength].application = state[stateidx].application;
						items[reportItemLength].bits = state[stateidx].bits;
						items[reportItemLength].logicalMaximum = state[stateidx].logicalMaximum;
						items[reportItemLength].logicalMinimum = state[stateidx].logicalMinimum;
						items[reportItemLength].type = REPORT_ITEM_INPUT;
						items[reportItemLength].usage = state[stateidx].usages[state[stateidx].usageCount - 1];
						items[reportItemLength].reportsize = state[stateidx].reportCount;
						items[reportItemLength].dataOffset = current_data_offset;
						items[reportItemLength].physicalMinimum = state[stateidx].physicalMinimum;
						items[reportItemLength].physicalMaximum = state[stateidx].physicalMaximum;
						if (items[reportItemLength].logicalMinimum < 0 || items[reportItemLength].logicalMaximum < 0)
							items[reportItemLength].signedBits = true;
						reportItemLength++;
					}
					current_data_offset += (state[stateidx].bits * state[stateidx].reportCount);
					stateidx++;
					break;
				case HID_MAIN_ITEM_TAG_OUTPUT:
					//AuTextOut("HID Main item tag output \n");
					items[reportItemLength].type = REPORT_ITEM_OUTPUT;
					break;
				case HID_MAIN_ITEM_TAG_COLLECTION:
					//AuTextOut("HID Main item tag collection start %x \n", data);
					break;
				case HID_MAIN_ITEM_TAG_END_COLLECTION:
					//AuTextOut("HID Main item end tag collection end %x \n", data);
					break;
				}
				break;
			case HID_ITEM_TYPE_GLOBAL:
				switch (one_byte_prefix) {
				case HID_GLOBAL_ITEM_TAG_USAGE_PAGE: {
					uint32_t upage = 0;
					if (item_size == 4) {
						upage = (data >> 16) & 0xffff;
						usage = data & 0xffff;
					}
					else {
						upage = 0;
						usage = data;
					}
					if (state[stateidx].application != 0)
						stateidx++;

					state[stateidx].application = data;
					//AuTextOut("HID Global Usage Page %x \n",data);
					break;
				}
				case HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM:
					state[stateidx].logicalMinimum = data;
					break;
				case HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM:
					state[stateidx].logicalMaximum = data;
					break;
				case HID_GLOBAL_ITEM_TAG_PHYSICAL_MINIMUM:
					state[stateidx].physicalMinimum = data;
					break;
				case HID_GLOBAL_ITEM_TAG_PHYSICAL_MAXIMUM:
					state[stateidx].physicalMaximum = data;
					break;
				case HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT:

					break;
				case HID_GLOBAL_ITEM_TAG_UNIT:

					break;
				case HID_GLOBAL_ITEM_TAG_REPORT_SIZE:
					//AuTextOut("HID Global Reporst Size %d \n", data);
					state[stateidx].bits = data;
					break;
				case HID_GLOBAL_ITEM_TAG_REPORT_COUNT: {
					//AuTextOut("HID Global Report Count (%d)\n", data);
					state[stateidx].reportCount = data;
				}break;
				case HID_GLOBAL_ITEM_TAG_REPORT_ID:
					//AuTextOut("HID Global Report ID (%d)\n", data);
					break;
				case HID_GLOBAL_ITEM_TAG_PUSH:
					//AuTextOut("HID Global Push \n");
					break;
				case HID_GLOBAL_ITEM_TAG_POP:
					//AuTextOut("HID Global Pop \n");
					break;
				}
				break;
			case HID_ITEM_TYPE_LOCAL:
				switch (one_byte_prefix)
				{
				case HID_LOCAL_ITEM_TAG_USAGE:
					state[stateidx].usages[state[stateidx].usageCount++] = (data & 0xffff);
					break;
				case HID_LOCAL_ITEM_TAG_USAGE_MINIMUM:
					//AuTextOut("HID Local Minimum \n");
					break;
				case HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM:
					//AuTextOut("HID Local Maximum \n");
					break;
				case HID_LOCAL_ITEM_TAG_STRING_MINIMUM:
					//AuTextOut("HID Local String Minimum \n");
					break;
				case HID_LOCAL_ITEM_TAG_STRING_MAXIMUM:
					//AuTextOut("HID Local String Maximum \n");
					break;
				case HID_LOCAL_ITEM_TAG_STRING_INDEX:
					//AuTextOut("HID Local String Index \n");
					break;
				case HID_LOCAL_ITEM_TAG_DESIGNATOR_INDEX:
					//AuTextOut("HID Local Designator Index \n");
					break;
				case HID_LOCAL_ITEM_TAG_DESIGNATOR_MINIMUM:
					//AuTextOut("HID Local Designator Minimum \n");
					break;
				case HID_LOCAL_ITEM_TAG_DESIGNATOR_MAXIMUM:
					//AuTextOut("HID Local Designator Maximum \n");
					break;
				case HID_LOCAL_ITEM_TAG_DELIMITER:
					//AuTextOut("HID Local Delimiter \n");
					break;
				}
				break;
			case HID_ITEM_TYPE_RESERVED:
				break;
			}
		}

		if (item_size == 0)
			position++;
		else {
			/* here we add one because, current position is also added
			 * if ,the size of data is 2 byte then we increment it by 2+1,
			 * 1 for the current position
			 */
			position += item_size + 1;
		}
	}

}

int usb_pow(int a, int b) {
	switch (b) {
	case 0:
		return 1;
		break;
	case  1:
		return a;
		break;
	default:
		return a * usb_pow(a, b - 1);
		break;
	}
}

/*
 * HIDCallback -- Human interface device interrupt
 * callback
 */
void HIDCallback(void* dev, void* slot, void* endp) {
	uint8_t* md = (uint8_t*)mouse_data;

	/* mouse information */
	/* extract the button information */
	uint8_t button = 0;
	int32_t mouse_x, mouse_y = 0;
	uint8_t byteOffset = 0;
	int lastsz = 0;
	AuInputMessage newmsg;
	memset(&newmsg, 0, sizeof(AuInputMessage));
	newmsg.type = AU_INPUT_MOUSE;
	newmsg.xpos = 0;
	newmsg.ypos = 0;
	newmsg.button_state = 0;

	for (int i = 0; i < reportItemLength; i++) {
		if (items[i].application == HID_APPLICATION_BUTTONS && items[i].usage == 0) {
			uint8_t buttonByte = md[byteOffset];
			if (buttonByte == 0x1)
				newmsg.button_state |= 1;
			else if (buttonByte == 0x2)
				newmsg.button_state |= 2;
		}

		if (items[i].application == HID_APPLICATION_GENERIC_DESKTOP_CTL && items[i].usage == HID_USAGE_X_AXIS) {
			if ((items[i].physicalMinimum == 0) && (items[i].physicalMaximum == 0)) {
				items[i].physicalMinimum = items[i].logicalMinimum;
				items[i].physicalMaximum = items[i].logicalMaximum;
			}

			int resolution;
			if (items[i].physicalMaximum == items[i].physicalMinimum) {
				resolution = 1;
			}
			else {
				resolution = (items[i].logicalMaximum - items[i].logicalMinimum) /
					((items[i].physicalMaximum - items[i].physicalMinimum) * usb_pow(10, 0));
			}

			if (items[i].relative)
				SeTextOut("Mouse movement is relative \r\n");

			uint32_t result = (md[byteOffset + 1] & 0xff) << 8 | md[byteOffset] & 0xff;

			if (result & (1 << (items[i].bits - 1))) {
				for (uintptr_t i = items[i].bits; i < 32; i++) {
					result |= 1 << i;
				}
			}


			int val = (((result - items[i].logicalMinimum) / resolution) +
				items[i].physicalMinimum);
			mouse_x = val;

			newmsg.xpos = mouse_x;
			newmsg.code3 = items[i].logicalMinimum;
			newmsg.code4 = items[i].logicalMaximum;

		}

		if (items[i].application == HID_APPLICATION_GENERIC_DESKTOP_CTL && items[i].usage == HID_USAGE_Y_AXIS) {
			uint32_t result = (md[byteOffset + 1] & 0xff) << 8 | (md[byteOffset] & 0xff);
			if (result & (1 << (items[i].bits - 1))) {
				for (uintptr_t i = items[i].bits; i < 32; i++) {
					result |= 1 << i;
				}
			}

			int resolution;
			if (items[i].physicalMaximum == items[i].physicalMinimum) {
				resolution = 1;
			}
			else {
				resolution = (items[i].logicalMaximum - items[i].logicalMinimum) /
					((items[i].physicalMaximum - items[i].physicalMinimum) * usb_pow(10, 0));
			}

			int val = (((result - items[i].logicalMinimum) / resolution) +
				items[i].physicalMinimum);

			mouse_y = val;

			newmsg.code3 = items[i].logicalMinimum;
			newmsg.code4 = items[i].logicalMaximum;
			newmsg.ypos = mouse_y;

		}

		int fieldSz = (items[i].bits * items[i].reportsize) + lastsz;
		byteOffset += fieldSz / 8;
		if (byteOffset >= 8/*ep->max_packet_sz*/)
			break;
		if (fieldSz == 0)
			byteOffset++;

		if (fieldSz >= 8)
			lastsz = 0;
		if (fieldSz < 8)
			lastsz = fieldSz;

		if (items[i].application == HID_APPLICATION_KEYBOARD_KEYPAD) {
			//handle keyboard inputs
			AuTextOut("[HID]: Keyboard received \n");
		}
	}

	AuDevWriteMice(&newmsg);
	device->AuScheduleInterrupt(device, endpoint, V2P(mouse_data), HIDCallback);
}
/*
* AuUSBDriverMain -- Main entry for USB driver 
*/
AU_EXTERN AU_EXPORT int AuUSBDriverMain(AuUSBDeviceStruc* dev) {
	HIDDescriptor* hid = (HIDDescriptor*)dev->AuGetDescriptor(dev, 0x21);
	size_t report_bytes = 0;
	device = dev;
	
	for (int i = 0; i < hid->bNumDescriptors; i++) {
		if (hid->links[i].type == 0x22) {
			report_bytes = (size_t)hid->links[i].length[0] | ((size_t)hid->links[i].length[1] << 8);
		}
	}

	memset(&items, 0, sizeof(ReportItem) * 32);


	if (dev->protocol > 0) {
		SetProtocol(dev);
		dev->AuUSBWait(dev, USB_WAIT_EVENT_TRANSFER);
	}


	uint64_t buff = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	memset((void*)buff, 0, PAGE_SIZE);
	uint8_t* report = (uint8_t*)buff;

	GetReport(dev, buff, report_bytes);
	int t_idx = -1;
	t_idx = dev->AuUSBWait(dev, USB_WAIT_EVENT_TRANSFER);


	HIDParseReportDescriptor((uint8_t*)buff, report_bytes);

	SetIDLE(dev);
	t_idx = -1;
	t_idx = dev->AuUSBWait(dev, USB_WAIT_EVENT_TRANSFER);

	mouse_data = (uint64_t)P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset((void*)mouse_data, 0, PAGE_SIZE);

	void* ep = dev->AuGetEndpoint(dev, ENDPOINT_TRANSFER_TYPE_INT);
	if (!ep) {
		AuTextOut("[HID]: Failed to obtaine interrupt endpoint \n");
		return 1;
	}
	endpoint = ep;
	dev->AuScheduleInterrupt(dev, ep, V2P(mouse_data), HIDCallback);

	
	return 0;
}