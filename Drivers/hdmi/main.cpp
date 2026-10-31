/**
* @file main.cpp
* 
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
#include <Hal/AA64/aa64lowlevel.h>
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>
#include "imx8mp_hdmi_tx_c.h"

static uint64_t _base;

#define HDMI_REG(base,off)  (*(volatile uint8_t*)(base + off))

extern int fsl_samsung_hdmi_phy_init();

/*
* AuDriverUnload -- deattach the driver from
* aurora system
*/
AU_EXTERN AU_EXPORT int AuDriverUnload() {

	return 0;
}

/*
* AuDriverMain -- Main entry for hdmi driver
*/
AU_EXTERN AU_EXPORT int AuDriverMain() {
	UARTDebugOut("[aurora_hdmi]: initializing tx controller \r\n");
	//_base = (uint64_t)AuMapMMIO(IMX8MP_HDMI_TX_CONTROLLER_BASE, 1);

	//uint8_t designID = HDMI_REG(_base, DESIGN_ID);
	//uint8_t revID = HDMI_REG(_base, 0x0001);
	//uint8_t productID = HDMI_REG(_base, 0x0002);
	//uint8_t productID1 = HDMI_REG(_base, 0x0003);

	//UARTDebugOut("[aurora_hdmi]: design id : %x,  revision : %x \r\n", designID, revID);
	fsl_samsung_hdmi_phy_init();

	UARTDebugOut("[aurora_hdmi]: hdmi initialized \r\n");
	return 0;
}