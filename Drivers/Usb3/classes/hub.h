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

#ifndef __USB_HUB_H__
#define __USB_HUB_H__

#include <stdint.h>
#include "../usb3.h"
#include "../xhci.h"
#include "../xhci_cmd.h"

/* HUB Commands */
#define HUB_CMD_GET_STATUS        0
#define HUB_CMD_CLEAR_FEATURE     1
#define HUB_CMD_SET_FEATURE       3
#define HUB_CMD_GET_DESCRIPTOR    6
#define HUB_CMD_SET_DESCRIPTOR    7
#define HUB_CMD_CLEAR_TT_BUFFER   8
#define HUB_CMD_RESET_TT          9
#define HUB_CMD_GET_TT_STATE      10
#define HUB_CMD_STOP_TT           11
#define HUB_CMD_SET_HUB_DEPTH     12
#define HUB_CMD_GET_PORT_ERR_COUNT  13

#define HUB_FEATURE_PORT_CONNECTION 0
#define HUB_FEATURE_PORT_ENABLE     1
#define HUB_FEATURE_PORT_SUSPEND      2
#define HUB_FEATURE_PORT_OVER_CURRENT 3
#define HUB_FEATURE_PORT_RESET        4
#define HUB_FEATURE_PORT_LINK_STATE   5
#define HUB_FEATURE_PORT_POWER         8
#define HUB_FEATURE_PORT_LOW_SPEED     9
#define HUB_FEATURE_C_PORT_CONNECTION  0x10
#define HUB_FEATURE_C_PORT_ENABLE   0x11
#define HUB_FEATURE_C_PORT_RESET    0x14

#define HUB_DESCRIPTOR  0x29
#define HUB_DESCRIPTOR_3 0x2A
#define HUB_PORT_RESET (1<<4)

#pragma pack(push,1)
typedef struct _usb_hub_descriptor_ {
	uint8_t bDescriptorLength;
	uint8_t bDescriptorType;
	uint8_t bNumberOfPorts;
	uint16_t wHubCharacteristics;
	uint8_t bPowerOnToPowerGood;
	uint8_t bHubControlCurrent;
	uint8_t bRemoveAndPowerMask[];
}USBHubDescriptor; 
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _usb3_hub_ {
	uint8_t bDescriptorLength;
	uint8_t bDescriptorType;
	uint8_t bNumberOfPorts;
	uint16_t wHubCharacteristics;
	uint8_t bPowerOnToPowerGood;
	uint8_t bHubControlCurrent;
	uint8_t hubHdrDecodeLatency;
	uint16_t hubDelay;
	uint16_t devRemovable;
}USB3HubDescriptor;
#pragma pack(pop)

void USBHubInitialise(USBDevice* dev, XHCISlot* slot);
#endif