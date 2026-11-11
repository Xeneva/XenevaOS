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
#include <aurora.h>
#include <Drivers/uart.h>
#include <Board/RPI3bp/rpi3bp.h>
#include "dwc2_hw.h"
#include "usb.h"
#include "usb_hid.h"

/*
 * Synopsys DWC2 USB host driver for the Raspberry Pi 3B/3B+, loaded by
 * the driver manager from /board.cnf.
 *
 * AuDriverMain powers the controller through the VideoCore firmware,
 * initializes it, enumerates the bus (hubs included) with polled
 * transfers and binds HID boot mice; their polling starts from a kernel
 * timer once the scheduler runs (usb_hid.cpp).
 */

/*
 * AuDriverMain -- driver entry
 */
AU_EXTERN AU_EXPORT int AuDriverMain(void* drv) {
	(void)drv;
	UARTDebugOut("[dwc2]: Synopsys DWC2 USB host \r\n");

	/* the USB controller sits in a firmware power domain */
	if (!AuRPISetPowerState(RPI_POWERUP_USB_HCD))
		UARTDebugOut("[dwc2]: firmware did not confirm USB power, continuing \r\n");

	if (dwc2_hcd_init() != USB_OK) {
		UARTDebugOut("[dwc2]: controller initialization failed \r\n");
		return 1;
	}
	int ret = usb_enumerate_root();
	if (ret != USB_OK)
		UARTDebugOut("[dwc2]: enumeration stopped (%d) \r\n", ret);

	usb_hid_start();
	UARTDebugOut("[dwc2]: ready \r\n");
	return 0;
}

/*
 * AuDriverUnload -- the driver stays resident
 */
AU_EXTERN AU_EXPORT int AuDriverUnload(void* drv) {
	(void)drv;
	return 0;
}
