/**
* @file usb_hid.h
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

#ifndef __USB_HID_H__
#define __USB_HID_H__

#include <stdint.h>
#include "dwc2.h"
#include "dwc2_reg.h"
#include "dwc2_usbdev.h"
#include "usb_desc.h"
#include <Mm/dma.h>
#include <Drivers/uart.h>
#include <_null.h>

#pragma pack(push,1)
typedef struct _usb_hid_ {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdHID;
	uint8_t bCountryCode;
	uint8_t bNumDescriptors;
	uint8_t bClassDescType;
	uint16_t wDescriptorLength;
}usb_hid_desc_t;
#pragma pack(pop)



extern void USBHidInitialize(dwc2_core_regs* regs, dwc2_usb_device* dev);

extern uint64_t usb_hid_get_physical();

extern dwc2_usb_device* usb_hid_get_dev();

extern void* usb_hid_get_interrupt_buf();

extern void USBHIDSetMouse(int dx, int dy, int button);

extern void USBHIDStartInterrupt(dwc2_core_regs* regs);

extern void HIDRequestInterrupt(void* param);

extern void HIDRequestInterrupt_NAK(void* param);

extern void HIDRequestCSplit(void* param);

extern void USBHIDSetNAKNormal(bool nak, bool normal);

#endif
