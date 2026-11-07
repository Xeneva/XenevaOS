/**
* @file dwc2_usbdev.h
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

#ifndef __DWC2_USBDEV_H__
#define __DWC2_USBDEV_H__

#include "dwc2.h"
#include "dwc2_reg.h"
#include "usb_desc.h"

enum _transfer_stage_ {
	TRANSFER_SSPLT,
	TRANSFER_CSPLT
};

typedef struct _dwc2_usb_dev_ {
	char name[32];
	dwc2_usb_endpoint_t ep;
	usb_dev_desc_t* device_desc;
	usb_config_desc_t* config_desc;
	usb_if_desc_t* interface_desc;
	usb_hub_desc_t* hub_desc;
	uint8_t dev_address;
	uint8_t hub_address;
	uint8_t hub_port;
	void* scratchBuff;
	uint8_t stage;
	int kernelTimerHandle;
	int kernelTimerHandle2;
}dwc2_usb_device;


/**
 * dwc2_usbdev_initialize -- initializing usb device
 * @param port -- port number
 * @param hub_addr -- hub device address
 * @param hub_port -- hub's port number
 * @param speed -- speed of the port
 */
extern bool dwc2_usbdev_initialize(dwc2_core_regs* regs, uint8_t port, uint8_t hub_addr, uint8_t hub_port, uint8_t speed);

#endif
