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
#include <Board/RPI3bp/rpi_ili9486.h>
#include <Board/RPI3bp/rpi3bp_spi.h>
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <aucon.h>
#include <_null.h>

#ifdef __TARGET_BOARD_RPI3__

/*
 * ILI9486 over SPI. Commands from the ILI9486 datasheet (MIPI DCS);
 * the power/VCOM/gamma register values are the panel vendors' published
 * init data for these 3.5" modules. Reset: asserted for tens of us, then
 * 120 ms before the first command (datasheet reset timing).
 */

#define MIPI_DCS_SOFT_RESET		   0x01
#define MIPI_DCS_EXIT_SLEEP_MODE   0x11
#define MIPI_DCS_SET_DISPLAY_ON	   0x29
#define MIPI_DCS_SET_COLUMN_ADDR   0x2A
#define MIPI_DCS_SET_PAGE_ADDR	   0x2B
#define MIPI_DCS_WRITE_MEMORY	   0x2C
#define MIPI_DCS_SET_ADDRESS_MODE  0x36
#define MIPI_DCS_SET_PIXEL_FORMAT  0x3A
#define ILI9486_IFMODE			   0xB0
#define ILI9486_PWCTRL3			   0xC2
#define ILI9486_VMCTRL1			   0xC5
#define ILI9486_PGAMCTRL		   0xE0
#define ILI9486_NGAMCTRL		   0xE1
#define ILI9486_DGAMCTRL1		   0xE2

#define ILI_CHUNK 2048

AuRPIILI9486Config AuRPIILI9486Cfg = {
	.chip_select = 0,
	.dc_gpio = 24,
	.reset_gpio = 25,
	.backlight_gpio = 0xFF,
	.speed_hz = 16000000,
	.width = 480,
	.height = 320,
	.madctl = 0x28,
	.reg16 = true,
};

static AuRPISPIDevice ili_spi;
static bool ili_ready;
static uint8_t ili_buf[ILI_CHUNK];

static void ili_send(bool data, const uint8_t* bytes, uint32_t len) {
	AuRPIGPIOWrite(AuRPIILI9486Cfg.dc_gpio, data);
	if (!AuRPIILI9486Cfg.reg16) {
		AuRPISPITransfer(&ili_spi, bytes, NULL, len);
		return;
	}
	/* 16-bit register interface: every byte goes out as 0x00, byte */
	while (len) {
		uint32_t n = 0;
		while (len && n + 2 <= ILI_CHUNK) {
			ili_buf[n++] = 0;
			ili_buf[n++] = *bytes++;
			len--;
		}
		AuRPISPITransfer(&ili_spi, ili_buf, NULL, n);
	}
}

/*
 * ili_reg -- one command followed by its parameters
 */
static void ili_reg(uint8_t cmd, const uint8_t* params, uint32_t n) {
	ili_send(false, &cmd, 1);
	if (n)
		ili_send(true, params, n);
}

static void ili_window(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye) {
	uint8_t col[4] = {(uint8_t)(xs >> 8), (uint8_t)xs, (uint8_t)(xe >> 8), (uint8_t)xe};
	uint8_t page[4] = {(uint8_t)(ys >> 8), (uint8_t)ys, (uint8_t)(ye >> 8), (uint8_t)ye};
	ili_reg(MIPI_DCS_SET_COLUMN_ADDR, col, 4);
	ili_reg(MIPI_DCS_SET_PAGE_ADDR, page, 4);
	ili_reg(MIPI_DCS_WRITE_MEMORY, 0, 0);
	/* pixel data follows with DC high */
	AuRPIGPIOSet(AuRPIILI9486Cfg.dc_gpio);
}

/**
 * @brief AuRPIILI9486Initialize -- reset and init the panel
 */
bool AuRPIILI9486Initialize() {
	if (ili_ready)
		return true;
	if (!AuRPISPIPresent())
		return false;

	ili_spi.chip_select = AuRPIILI9486Cfg.chip_select;
	ili_spi.mode = RPI_SPI_MODE_0;
	ili_spi.speed_hz = AuRPIILI9486Cfg.speed_hz;

	AuRPIGPIOSetFunction(AuRPIILI9486Cfg.dc_gpio, RPI_GPIO_FUNC_OUTPUT);
	AuRPIGPIOSetFunction(AuRPIILI9486Cfg.reset_gpio, RPI_GPIO_FUNC_OUTPUT);
	if (AuRPIILI9486Cfg.backlight_gpio != 0xFF) {
		AuRPIGPIOSetFunction(AuRPIILI9486Cfg.backlight_gpio, RPI_GPIO_FUNC_OUTPUT);
		AuRPIGPIOClear(AuRPIILI9486Cfg.backlight_gpio);
	}

	/* hardware reset (fbtft_reset) */
	AuRPIGPIOClear(AuRPIILI9486Cfg.reset_gpio);
	AuRPI3DelayUS(40);
	AuRPIGPIOSet(AuRPIILI9486Cfg.reset_gpio);
	AuRPIDelayMS(120);

	static const uint8_t ifmode[] = {0x00};
	static const uint8_t pixfmt[] = {0x55}; /* 16 bpp RGB565 */
	static const uint8_t pwctrl3[] = {0x44};
	static const uint8_t vmctrl1[] = {0x00, 0x00, 0x00, 0x00};
	static const uint8_t pgamma[] = {0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
									 0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00};
	static const uint8_t ngamma[] = {0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
									 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00};

	ili_reg(ILI9486_IFMODE, ifmode, 1);
	ili_reg(MIPI_DCS_EXIT_SLEEP_MODE, 0, 0);
	AuRPIDelayMS(250);
	ili_reg(MIPI_DCS_SET_PIXEL_FORMAT, pixfmt, 1);
	ili_reg(ILI9486_PWCTRL3, pwctrl3, 1);
	ili_reg(ILI9486_VMCTRL1, vmctrl1, 4);
	ili_reg(ILI9486_PGAMCTRL, pgamma, sizeof(pgamma));
	ili_reg(ILI9486_NGAMCTRL, ngamma, sizeof(ngamma));
	ili_reg(ILI9486_DGAMCTRL1, ngamma, sizeof(ngamma));
	ili_reg(MIPI_DCS_EXIT_SLEEP_MODE, 0, 0);
	ili_reg(MIPI_DCS_SET_DISPLAY_ON, 0, 0);
	ili_reg(MIPI_DCS_SET_ADDRESS_MODE, &AuRPIILI9486Cfg.madctl, 1);

	if (AuRPIILI9486Cfg.backlight_gpio != 0xFF)
		AuRPIGPIOSet(AuRPIILI9486Cfg.backlight_gpio);

	ili_ready = true;
	AuTextOut("[ili9486]: %dx%d panel on SPI0 CE%d \r\n", AuRPIILI9486Cfg.width,
			  AuRPIILI9486Cfg.height, AuRPIILI9486Cfg.chip_select);
	return true;
}

/**
 * @brief AuRPIILI9486FillRect -- solid fill
 */
void AuRPIILI9486FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565) {
	if (!ili_ready || !w || !h)
		return;
	ili_window(x, y, x + w - 1, y + h - 1);
	for (uint32_t i = 0; i < ILI_CHUNK; i += 2) {
		ili_buf[i] = rgb565 >> 8;
		ili_buf[i + 1] = rgb565 & 0xFF;
	}
	uint32_t bytes = (uint32_t)w * h * 2;
	while (bytes) {
		uint32_t n = bytes < ILI_CHUNK ? bytes : ILI_CHUNK;
		AuRPISPITransfer(&ili_spi, ili_buf, NULL, n);
		bytes -= n;
	}
}

/**
 * @brief AuRPIILI9486Blit -- XRGB8888 to the panel as RGB565
 */
void AuRPIILI9486Blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint32_t* pixels,
					  uint32_t stride) {
	if (!ili_ready || !w || !h || !pixels)
		return;
	ili_window(x, y, x + w - 1, y + h - 1);
	uint32_t n = 0;
	for (uint32_t row = 0; row < h; row++) {
		const uint32_t* src = pixels + (uint64_t)row * stride;
		for (uint32_t col = 0; col < w; col++) {
			uint32_t p = src[col];
			uint16_t c = (uint16_t)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
			ili_buf[n++] = c >> 8;
			ili_buf[n++] = c & 0xFF;
			if (n == ILI_CHUNK) {
				AuRPISPITransfer(&ili_spi, ili_buf, NULL, n);
				n = 0;
			}
		}
	}
	if (n)
		AuRPISPITransfer(&ili_spi, ili_buf, NULL, n);
}

#endif
