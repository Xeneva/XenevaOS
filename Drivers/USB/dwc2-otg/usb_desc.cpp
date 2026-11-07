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

#include "usb_desc.h"
#include <Drivers/uart.h>

/**
 * @brief dwc2_get_descriptor -- find a descriptor within descriptor config
 * area
 * @param desc -- pointer to config descriptor
 * @param type -- type of the descriptor
 */
usb_descriptor_t* dwc2_get_descriptor(void* desc, uint8_t type) {
	usb_config_desc_t* config = (usb_config_desc_t*)desc;
	usb_if_desc_t* interface_desc = raw_offset<usb_if_desc_t*>(config, config->bLength);
	usb_descriptor_t* endp = raw_offset<usb_descriptor_t*>(interface_desc, interface_desc->bLength);
	while (raw_diff(endp, config) < config->wTotalLength) {
		if (endp->bDescriptorType == type) {
			UARTDebugOut("Descriptor found %x\r\n", type);
			return endp;
		}
		endp = raw_offset<usb_descriptor_t*>(endp, endp->bLength);
	}
}