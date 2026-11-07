/**
* BSD 2-Clause License
*
* @file lan7800.cpp
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
#include "dwc2.h"
#include "dwc2_usbdev.h"
#include <Drivers/uart.h>
#include <string.h>
#include <Mm/pmmngr.h>
#include <Hal/AA64/aa64cpu.h>
#include <Hal/AA64/aa64lowlevel.h>

#define LAN7800_VENDOR_REQUEST_WRITE 0xA0
#define LAN7800_VENDOR_REQUEST_READ  0xA1
#define LAN7800_HW_CFG  0x010
#define LAN7800_PMT_CTL 0x014
#define LAN7800_E2P_CMD  0x040
#define LAN7800_E2P_DATA 0x044
#define LAN7800_OTP_BASE 0x01000
#define LAN7800_ADDR_FILTX 0x400
#define LAN7800_RX_ADDRH 0x118
#define LAN7800_RX_ADDRL  0x11C

#define E2P_CMD_BUSY (1U << 31)
#define E2P_CMD_READ  (0x0 << 28)
#define E2P_CMD_ADDR_MASK 0x1FF

uint32_t lan7800_read_reg(dwc2_core_regs* regs, dwc2_usb_device* dev, uint32_t reg) {
	uint32_t val = 0;
	memset(dev->scratchBuff, 0, 4096);
	if (dwc2_control_transfer(regs, &dev->ep, 0xC0, LAN7800_VENDOR_REQUEST_READ, 0, reg, dev->scratchBuff, 4)) {
		UARTDebugOut("[lan7800]: failed to read register %x \r\n", reg);
		return 0;
	}
	aa64_data_cache_clean_range(dev->scratchBuff, 0x1000);
	val = *(uint32_t*)dev->scratchBuff;
	return val;
}

void lan7800_write_reg(dwc2_core_regs* regs, dwc2_usb_device* dev, uint32_t reg, uint32_t val) {
	memset(dev->scratchBuff, 0, 4096);
	uint32_t* value = (uint32_t*)dev->scratchBuff;
	*value = val;
	aa64_data_cache_clean_range(dev->scratchBuff, 0x1000);

	if (dwc2_control_transfer(regs, &dev->ep, 0x40, LAN7800_VENDOR_REQUEST_WRITE, 0, reg, dev->scratchBuff, 4)) {
		UARTDebugOut("[lan7800]: failed to write register %x with value : %x \r\n", reg, val);
		return;
	}
}

uint8_t lan7800_eeprom_read_bytes(dwc2_core_regs* regs, dwc2_usb_device* dev, uint32_t addr) {
	uint32_t timeout = 100;
	while (lan7800_read_reg(regs, dev, LAN7800_E2P_CMD) & E2P_CMD_BUSY) {
		AA64SleepMS(1);
		if (!timeout--) return 0xFF;
	}

	uint32_t cmd = E2P_CMD_BUSY | E2P_CMD_READ | (addr & E2P_CMD_ADDR_MASK);
	lan7800_write_reg(regs, dev, LAN7800_E2P_CMD, cmd);

	timeout = 100;
	while (lan7800_read_reg(regs, dev, LAN7800_E2P_CMD) & E2P_CMD_BUSY) {
		AA64SleepMS(1);
		if (!timeout--) return 0xFF;
	}

	return lan7800_read_reg(regs, dev, LAN7800_E2P_DATA) & 0xFF;
}

void lan7800_read_mac_from_eeprom(dwc2_core_regs* regs, dwc2_usb_device* dev, uint8_t* mac) {
	uint8_t sig = lan7800_eeprom_read_bytes(regs, dev, 0);
	UARTDebugOut("lan7800 eeprom sig : %x \r\n", sig);

	if (sig == 0xA5) {
		for (int i = 0; i < 6; i++)
			mac[i]= lan7800_eeprom_read_bytes(regs, dev, i + 1);
	}
}

/**
 * @brief lan7800_initialize -- initialize SMSC lan7800 
 * @param regs -- pointer to dwc2 core registers
 * @param dev -- pointer to usb device of lan7800
 */
void lan7800_initialize(dwc2_core_regs* regs, dwc2_usb_device* dev) {
	uint32_t timeout = 100;
	dev->scratchBuff = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	while (timeout--) {
		uint32_t hw_cfg = lan7800_read_reg(regs, dev, 0x010);
		if (hw_cfg & 0x2) break;
		AA64SleepMS(10);
	}

	UARTDebugOut("starting lan7800 ethernet controller \r\n");

	uint32_t addrl = lan7800_read_reg(regs, dev, 0x118);
	uint32_t addrh = lan7800_read_reg(regs, dev, 0x11C);

	uint8_t mac[6];
	mac[0] = (addrl >> 0) & 0xFF;
	mac[1] = (addrl >> 8) & 0xFF;
	mac[2] = (addrl >> 16) & 0xFF;
	mac[3] = (addrl >> 24) & 0xFF;
	mac[4] = (addrh >> 0) & 0xFF;
	mac[5] = (addrh >> 8) & 0xFF;

	UARTDebugOut("MAC: %x:%x:%x:", mac[0], mac[1], mac[2]);
	UARTDebugOut("%x:%x:%x \r\n", mac[3], mac[4], mac[5]);

	lan7800_read_mac_from_eeprom(regs, dev, mac);

	UARTDebugOut("MAC: %x:%x:%x:", mac[0], mac[1], mac[2]);
	UARTDebugOut("%x:%x:%x \r\n", mac[3], mac[4], mac[5]);

	/* enable TX/RX */
	uint32_t mac_cr = lan7800_read_reg(regs, dev, 0x100);
	mac_cr |= (1 << 2);
	mac_cr |= (1 << 3);
	lan7800_write_reg(regs, dev, 0x100, mac_cr);

	UARTDebugOut("lan7800 initialized \r\n");
}
