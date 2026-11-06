/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#if 0

#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Board/RPI3bp/rpi3bp_spi.h>
#include <Board/RPI3bp/rpi_ili9486.h>

#define GPIO_DC						 25
#define GPIO_RST					 24 //25
#define GPIO_LED					 23 //18

/* MIPI DSI Processor to Peripheral transaction types */
#define MIPI_DSI_V_SYNC_START		 0x01
#define MIPI_DSI_V_SYNC_END			 0x11
#define MIPI_DSI_H_SYNC_START		 0x21
#define MIPI_DSI_H_SYNC_END			 0x31
#define MIPI_DSI_COMPRESSION_MODE	 0x07
#define MIPI_DSI_END_OF_TRANSMISSION 0x08
#define MIPI_DSI_COLOR_MODE_OFF		 0x02
#define MIPI_DSI_COLOR_MODE_ON		 0x12
#define MIPI_DSI_SHUTDOWN_PERIPHERAL 0x22
#define MIPI_DSI_TURN_ON_PERIPHERAL	 0x32
#define MIPI_DCS_ENTER_SLEEP_MODE	 0x10
#define MIPI_DCS_EXIT_SLEEP_MODE	 0x11
#define MIPI_DCS_SET_PIXEL_FORMAT	 0x3A

#define LBTFT_WIDTH	 320
#define LBTFT_HEIGHT 480

void LCDWriteCommand(uint8_t cmd) {
	AuRPIGPIOClear(GPIO_DC);
	AuRPISPITransfer(cmd);
}

void LCDWriteData(uint8_t data) {
	AuRPIGPIOSet(GPIO_DC);
	AuRPISPITransfer(data);
}

void LCDWriteDataBuffer(const uint8_t* data, uint32_t len) {
	AuRPIGPIOSet(GPIO_DC);
	AuRPISPITransferBuffer(data, len);
}

void LCDWriteU16(uint16_t data) {
	LCDWriteData(data >> 8);
	LCDWriteData(data & 0xFF);
}

void LCDWriteU32(uint32_t data) {
	LCDWriteData((data >> 24) & 0xFF);
	LCDWriteData((data >> 16) & 0xFF);
	LCDWriteData((data >> 8) & 0xFF);
	LCDWriteData(data & 0xFF);
}


void ILI9486Init() {
	AuRPISPITransferStart();
	//LCDWriteCommand(0xB0);
	//LCDWriteData(0x00);
	LCDWriteCommand(0x01);
	LCDWriteData(0x00);
	for (int i = 0; i < 120000000; i++)
		;

	LCDWriteCommand(MIPI_DCS_EXIT_SLEEP_MODE);
	LCDWriteData(0x00);
	for (int i = 0; i < 100000000; i++)
		;
	LCDWriteCommand(MIPI_DCS_SET_PIXEL_FORMAT);
	LCDWriteData(0x55);

	AuTextOut("ILI9486 Software reset completed \r\n");

	//LCDWriteCommand(0x28);
	LCDWriteCommand(0xC0);
	LCDWriteData(0x09);
	LCDWriteData(0x09);
	for (int i = 0; i < 1200000; i++)
		;
	AuTextOut("Display turned off \r\n");

	LCDWriteCommand(0xC1);
	LCDWriteData(0x41);
	LCDWriteData(0x00);

	AuTextOut("Power control command sent \r\n");

	LCDWriteCommand(0xC2);
	LCDWriteData(0x44);

	for (int i = 0; i < 12000000; i++);
	LCDWriteCommand(0xC5);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);

	for (int i = 0; i < 12000000; i++);
	LCDWriteCommand(0xE0);
	LCDWriteData(0x0F);
	LCDWriteData(0x1F);
	LCDWriteData(0x1C);
	LCDWriteData(0x0C);
	LCDWriteData(0x0F);
	LCDWriteData(0x08);
	LCDWriteData(0x48);
	LCDWriteData(0x98);
	LCDWriteData(0x37);
	LCDWriteData(0x0A);
	LCDWriteData(0x13);
	LCDWriteData(0x04);
	LCDWriteData(0x11);
	LCDWriteData(0x0D);
	LCDWriteData(0x00);
	for (int i = 0; i < 12000000; i++);


	LCDWriteCommand(0xE1);
	LCDWriteData(0x0F);
	LCDWriteData(0x32);
	LCDWriteData(0x2E);
	LCDWriteData(0x0B);
	LCDWriteData(0x0D);
	LCDWriteData(0x05);
	LCDWriteData(0x47);
	LCDWriteData(0x75);
	LCDWriteData(0x37);
	LCDWriteData(0x06);
	LCDWriteData(0x10);
	LCDWriteData(0x03);
	LCDWriteData(0x24);
	LCDWriteData(0x20);
	LCDWriteData(0x00);
	for (int i = 0; i < 12000000; i++);

	LCDWriteCommand(0xE2);
	LCDWriteData(0x0F);
	LCDWriteData(0x32);
	LCDWriteData(0x2E);
	LCDWriteData(0x0B);
	LCDWriteData(0x0D);
	LCDWriteData(0x05);
	LCDWriteData(0x47);
	LCDWriteData(0x75);
	LCDWriteData(0x37);
	LCDWriteData(0x06);
	LCDWriteData(0x10);
	LCDWriteData(0x03);
	LCDWriteData(0x24);
	LCDWriteData(0x20);
	LCDWriteData(0x00);

	
	LCDWriteCommand(0x3A);
	LCDWriteData(0x55);
	
	LCDWriteCommand(0x36);
	LCDWriteData(0x28);

	LCDWriteCommand(0x11);
	for (int i = 0; i < 12000000; i++)
		;
	LCDWriteCommand(0x29);
	for (int i = 0; i < 12000000; i++)
		;

	AuTextOut("ILI done \r\n");
}


void LCDSetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
	LCDWriteCommand(0x2A);
	LCDWriteU16(x0);
	LCDWriteU16(x1);

	LCDWriteCommand(0x2B);
	LCDWriteU16(y0);
	LCDWriteU16(y1);

	LCDWriteCommand(0x2C);
}

void LCDClear(uint16_t color) {
	LCDSetWindow(10, 10, LBTFT_WIDTH - 1, LBTFT_HEIGHT - 1);
	AuTextOut("LCD Set Window completed \r\n");
	uint8_t color_bytes[2];
	color_bytes[0] = (color >> 8);
	color_bytes[1] = color & 0xFF;

	AuRPIGPIOSet(GPIO_DC);
	AuTextOut("color byte [0] = %x byte 1: %x \r\n", color_bytes[0], color_bytes[1]);

	for (uint32_t i = 0; i < LBTFT_WIDTH * LBTFT_HEIGHT; i++) {
		AuRPISPITrasnferWrite(color_bytes[0]);
		AuRPISPITrasnferWrite(color_bytes[1]);
	}
	AuRPISPITransferStart();
	AuRPISPITransferStop();
	AuTextOut("LCD Clear Completed \r\n");
}

static uint16_t RGB_TO_RGB565(uint8_t r,uint8_t g,uint8_t b) {
	return ((r & 0xF8) << 8) |
		((g & 0xFC) << 3) |
		(b >> 3);
}

/*
 * AuLCDInit -- initialize ili9486 pi display 
 */
void AuLCDInit() {

	AuRPIGPIOClear(GPIO_RST);
	for (int i = 0; i < 120000000; i++);

	AuRPIGPIOSet(GPIO_RST);
	for (int i = 0; i < 120000000; i++);

	AuTextOut("[aurora]: Initializing LCD \r\n");
	ILI9486Init();
	//ILI9341Init();
	AuRPIGPIOSet(GPIO_LED);
	for (int i = 0; i < 12000000; i++);

	AuTextOut("LCD Clearing \r\n");
	LCDClear(RGB_TO_RGB565(0, 16, 100));

	for (int i = 0; i < 12000000; i++);
	AuTextOut("LCD Initialized \r\n");
	AuRPISPITransferStop();

	for (;;);
}

#endif
