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
#ifndef __RPI_ILI9486_H__
#define __RPI_ILI9486_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * ILI9486 SPI LCD controller (the 3.5" 480x320 "Pi LCD" panels whose picture
 * comes over SPI, not HDMI). Not started automatically: call
 * AuRPIILI9486Initialize() from board code on boards that have one.
 *
 * Defaults are the Waveshare 3.5" (A) wiring and Linux overlay
 * (waveshare35a: ili9486 on spi0.0, dc GPIO24, reset GPIO25, 16 MHz,
 * regwidth 16, MADCTL 0x28 = landscape, BGR).
 */
typedef struct _rpi_ili9486_config_ {
	uint8_t chip_select;   /* SPI0 CE line */
	uint8_t dc_gpio;	   /* data/command select, low = command */
	uint8_t reset_gpio;	   /* active low reset */
	uint8_t backlight_gpio; /* 0xFF: no backlight control */
	uint32_t speed_hz;
	uint16_t width;		   /* in the orientation MADCTL selects */
	uint16_t height;
	uint8_t madctl;		   /* MIPI_DCS_SET_ADDRESS_MODE value */
	bool reg16;			   /* commands/parameters framed as 16-bit words */
} AuRPIILI9486Config;

extern AuRPIILI9486Config AuRPIILI9486Cfg;

/*
 * AuRPIILI9486Initialize -- reset the panel and run the init sequence.
 * Needs SPI0 up (AuRPISPIInitialize).
 * @return true on success
 */
extern bool AuRPIILI9486Initialize();

/*
 * AuRPIILI9486FillRect -- fill a rectangle with one RGB565 colour
 */
extern void AuRPIILI9486FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565);

/*
 * AuRPIILI9486Blit -- copy 32-bit XRGB pixels (stride in pixels) to the
 * panel at (x, y), converted to RGB565
 */
extern void AuRPIILI9486Blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
							 const uint32_t* pixels, uint32_t stride);

#endif
