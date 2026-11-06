/**
* BSD 2-Clause License
*
* @file usb_stdhub.cpp
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

#include "dwc2_usbdev.h"
#include "usb_desc.h"
#include <Drivers/uart.h>
#include <Mm/pmmngr.h>
#include <string.h>
#include <_null.h>
#include <Hal/AA64/aa64cpu.h>
#include <Mm/dma.h>
#include "dwc2.h"

/**
 * @brief usb_hub_initialize -- initialize usb hub
 * @param regs -- pointer to dwc2 core register
 * @param dev -- pointer to usb device struct
 */
bool usb_hub_initialize(dwc2_core_regs* regs, dwc2_usb_device* dev) {
	UARTDebugOut("usb hub initializing.... \r\n");

	uint64_t hub_phys_out = 0;
	void* hub = (void*)AuDMAGClassAlloc(dwc2_get_dma_class(), sizeof(usb_hub_desc_t), &hub_phys_out); //P2V((uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	//memset(hub, 0, 4096);
	if (dwc2_control_transfer(regs, &dev->ep, 0xA0, 0x06, 0x2900, 0, (void*)hub_phys_out/*V2P((uint64_t)hub)*/, 8)) {
		UARTDebugOut("failed to get hub descriptor \r\n");
	}
	usb_hub_desc_t* desc = (usb_hub_desc_t*)hub;
	UARTDebugOut("usb hub num ports : %d \r\n", desc->bNbrPorts);
	dev->hub_desc = desc;
	dev->hub_address = dev->ep.dev_address;


	// POWER UP all ports
	for (int i = 0; i < desc->bNbrPorts; i++) {
		if (dwc2_control_transfer(regs, &dev->ep, 0x23, 0x03, 8, i+1, NULL, 0)) {
			UARTDebugOut("hub port power failure on port : %d \r\n", i);
			continue;
		}
		AA64SleepMS(20);
	}
	AA64SleepMS(510);
	
	
	uint64_t status_phys = 0;
	void* scratchBuff = (void*)AuDMAGClassAlloc(dwc2_get_dma_class(), 8, &status_phys);//AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	
	// Time to RESET all ports yaayyy
	int numPorts = desc->bNbrPorts;
	for (unsigned i = 0; i < numPorts; i++) {
		/** Get status **/
		if (dwc2_control_transfer(regs, &dev->ep, REQUEST_IN | REQUEST_CLASS | REQUEST_TO_OTHER, GET_STATUS, 0,
			i + 1, (void*)status_phys , 4)) {
			UARTDebugOut("Failed to get status of port : %d \r\n", (i + 1));
			continue;
		}

		usb_port_status_t* status = (usb_port_status_t*)scratchBuff;
		if (!(status->wPortStatus & PORT_STATUS_POWER)) {
			UARTDebugOut("port %d, has no power \r\n", (i + 1));
			continue;
		}
		if (!(status->wPortStatus & PORT_STATUS_CONNECTION)) {
			UARTDebugOut("port %d, has no active connection \r\n", (i+1));
			continue;
		}
		UARTDebugOut("initializing port : %d \r\n", (i + 1));

		/** reset the port **/
		if (dwc2_control_transfer(regs, &dev->ep, REQUEST_OUT | REQUEST_CLASS | REQUEST_TO_OTHER,
			SET_FEATURE, PORT_RESET, i + 1, NULL, NULL)) {
			UARTDebugOut("could not reset port %d of hub \r\n", (i + 1));
			continue;
		}

		AA64SleepMS(100);

		/** Get status **/
		if (dwc2_control_transfer(regs, &dev->ep, REQUEST_IN | REQUEST_CLASS | REQUEST_TO_OTHER, GET_STATUS, 0,
			i + 1, (void*)status_phys, 4)) {
			UARTDebugOut("Failed to get second stage status of port : %d \r\n", (i + 1));
			continue;
		}

		status = (usb_port_status_t*)scratchBuff;
		if (!(status->wPortStatus & PORT_STATUS_ENABLED)) {
			UARTDebugOut("port %d is not enabled \r\n", i + 1);
			continue;
		}

		if (status->wPortStatus & PORT_STATUS_OVERCURRENT) {
			if (dwc2_control_transfer(regs, &dev->ep, REQUEST_OUT | REQUEST_CLASS | REQUEST_TO_OTHER,
				CLEAR_FEATURE, PORT_POWER, i + 1, NULL, NULL)) {
				UARTDebugOut("failed to clear feature port_power at port : %d \r\n", i + 1);
				continue;
			}
		}

		uint8_t speed = USBSpeedUnknown;
		if (status->wPortStatus & PORT_STATUS_LOW_SPEED) {
			UARTDebugOut("port : %d has low speed device \r\n", (i+1));
			speed = USBSpeedLow;
		}else if (status->wPortStatus & PORT_STATUS_HIGH_SPEED) {
			UARTDebugOut("port : %d has high speed device r\n", (i+1));
			speed = USBSpeedHigh;
		}
		else
			speed = USBSpeedFull;

		if (dwc2_usbdev_initialize(regs, i + 1, dev->hub_address, 0, speed)) {
			UARTDebugOut("failed to initialize device attached to hub port : %d \r\n", i + 1);
			for (;;);
		}
	}


	return 0;
}