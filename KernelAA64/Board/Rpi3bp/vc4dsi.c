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

#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/rpi3.h>
#include <Mm/pmmngr.h>
#include <aucon.h>
#include <string.h>
#include <Hal/AA64/aa64lowlevel.h>

#if 0 // __TARGET_BOARD_RPI3__

#define MAILBOX_BASE (RPI_MMIO_BASE + 0x00b880)
#define DSI0_BASE	 (RPI_MMIO_BASE + 0x209000)
#define DSI1_BASE	 (RPI_MMIO_BASE + 0x700000)
#define GPIO_BASE	 (RPI_MMIO_BASE + 0x200000)
#define SPI0_BASE	 (RPI_MMIO_BASE + 0x204000)

#define MAILBOX_READ   (MAILBOX_BASE + 0x00)
#define MAILBOX_STATUS (MAILBOX_BASE + 0x18)
#define MAILBOX_WRITE  (MAILBOX_BASE + 0x20)

#define MAILBOX_FULL	0x80000000
#define MAILBOX_EMPTY	0x40000000
#define MAILBOX_CH_PROP 8

#define DSI_CTRL		0x00
#define DSI_STAT		0x04
#define DSI_HSTX_TO_CNT 0x08
#define DSI_LPRX_TO_CNT 0x0C
#define DSI_TA_TO_CNT	0x10
#define DSI_PR_TO_CNT	0x14
#define DSI_DISP0_CTRL	0x18
#define DSI_DISP1_CTRL	0x1C
#define DSI_INT_STAT	0x20
#define DSI_INT_EN		0x24
#define DSI_PHYC		0x28
#define DSI_HS_CLT0		0x2C
#define DSI_HS_CLT1		0x30
#define DSI_HS_CLT2		0x34
#define DSI_HS_DLT3		0x38
#define DSI_HS_DLT4		0x3C
#define DSI_HS_DLT5		0x40
#define DSI_HS_DLT6		0x44
#define DSI_HS_DLT7		0x48

#define GPFSEL0 (GPIO_BASE + 0x00)
#define GPFSEL1 (GPIO_BASE + 0x04)
#define GPFSEL2 (GPIO_BASE + 0x08)
#define GPSET0	(GPIO_BASE + 0x1C)
#define GPCLR0	(GPIO_BASE + 0x28)

#define GPIO_DC	 25
#define GPIO_RST 24 //25
#define GPIO_LED 23 //18

#define SPI0_CS	  (SPI0_BASE + 0x00)
#define SPI0_FIFO (SPI0_BASE + 0x04)
#define SPI0_CLK  (SPI0_BASE + 0x08)

#define SPI_CS_TXD		(1ULL << 18)
#define SPI_CS_RXD		(1ULL << 17)
#define SPI_CS_DONE		(1ULL << 16)
#define SPI_CS_TA		(1ULL << 7)
#define SPI_CS_CLEAR_RX (1ULL << 5)
#define SPI_CS_CLEAR_TX (1ULL << 4)
#define SPI_CS_CS		(3ULL << 0)
#define SPI_CS_CS0		(0ULL << 0)
#define SPI_CS_CS1		(1ULL << 0)
#define SPI_CS_CS2		(2ULL << 0)

#define TAG_GET_BOARD_REV	0x00010002
#define TAG_ALLOCATE_BUFFER 0x00040001
#define TAG_RELEASE_BUFFER	0x00048001
#define TAG_BLANK_SCREEN	0x00040002
#define TAG_SET_PHYS_WH		0x00048003
#define TAG_SET_VIRT_WH		0x00048004
#define TAG_SET_DEPTH		0x00048005
#define TAG_SET_PIXEL_ORDER 0x00048006
#define TAG_GET_PITCH		0x00040008
#define TAG_SET_VIRT_OFFSET 0x00048009
#define TAG_SET_BACKLIGHT	0x0004800F
#define TAG_GET_TOUCHBUF	0x0004000F
#define TAG_SET_TOUCHBUF	0x0004801F
#define TAG_GET_DISPLAY_ID	0x00040013
#define TAG_SET_DSI_TIMING	00x00048014
#define TAG_LAST			0x00000000

#define DCS_SOFT_RESET		   0x01
#define DCS_GET_DISPLAY_ID	   0x04
#define DCS_ENTER_SLEEP		   0x10
#define DCS_EXIT_SLEEP		   0x11
#define DCS_SET_DISPLAY_OFF	   0x28
#define DCS_SET_DISPLAY_ON	   0x29
#define DCS_SET_COLUMN_ADDR	   0x2A
#define DCS_SET_PAGE_ADDR	   0x2B
#define DCS_WRITE_MEMORY_START 0x2C
#define DCS_SET_TEAR_OFF	   0x34
#define DCS_SET_TEAR_ON		   0x35
#define DCS_SET_PIXEL_FORMAT   0x3A
#define DCS_SET_BRIGHTNESS	   0x51

#define DSI_DT_DCS_SHORT_WRITE_0 0x05
#define DSI_DT_DCS_SHORT_WRITE_1 0x15
#define DSI_DT_DCS_LONG_WRITE	 0x39
#define DSI_DT_DCS_READ			 0x06
#define DSI_DT_SET_MAX_RET_PKT	 0x37

#define LBTFT_WIDTH					 320
#define LBTFT_HEIGHT				 480

uint32_t* mailBOX;

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

static inline void vc4write(uint32_t addr, uint32_t val) {
	data_cache_flush((uint64_t)addr);
	*(volatile uint32_t*)addr = val;
	dsb_ish();
	isb_flush();
}

static inline uint32_t vc4read(uint32_t addr) {
	data_cache_flush((uint64_t)addr);
	return *(volatile uint32_t*)addr;
	dsb_ish();
	isb_flush();
}


static inline void vc4delay(uint32_t count) {
	for (volatile uint32_t i = 0; i < count; i++);
}

static uint32_t mailbox_read(uint8_t channel) {
	uint32_t data;
	while (1) {
		while (vc4read(MAILBOX_STATUS) & MAILBOX_EMPTY);
		data = vc4read(MAILBOX_READ);
		if ((data & 0xF) == channel)
			return data & 0xFFFFFFF0;
	}
}


static void mailbox_write(uint32_t data, uint8_t channel) {
	data_cache_flush((uint64_t*)data);
	while (vc4read(MAILBOX_STATUS) & MAILBOX_FULL);
	AuTextOut("[aurora]: writing mbox data : %x \r\n", (data & 0xFFFFFFF0));
	vc4write(MAILBOX_WRITE, (data & 0xFFFFFFF0ULL) | (channel & 0xFULL));
	dsb_ish();
}

void DSIWriteReg(uint32_t dsiBase, uint32_t reg, uint32_t val) {
	vc4write(dsiBase + reg, val);
}


uint32_t DSIReadReg(uint32_t dsiBase, uint32_t reg) {
	return vc4read(dsiBase + reg);
}

void DSISendShortCMD(uint32_t dsiBase, uint8_t cmd, uint8_t param, bool hasParam) {
	uint32_t pktType = hasParam ? DSI_DT_DCS_SHORT_WRITE_1 : DSI_DT_DCS_SHORT_WRITE_0;
	uint32_t data = (param << 16) | (cmd << 8) | pktType;
	while (DSIReadReg(dsiBase, DSI_STAT) & (1 << 3));
	DSIWriteReg(dsiBase, DSI_CTRL, data);
	vc4delay(10000);
}

void DSISendLongCMD(uint32_t dsiBase, uint8_t cmd, const uint8_t* params, uint32_t len) {
	uint32_t wc = len + 1;
	uint32_t header = (wc << 8) | DSI_DT_DCS_LONG_WRITE;

	while (DSIReadReg(dsiBase, DSI_STAT) & (1 << 3));

	DSIWriteReg(dsiBase, DSI_CTRL, header);

	DSIWriteReg(dsiBase, DSI_CTRL, cmd);

	for (uint32_t i = 0; i < len; i++)
		DSIWriteReg(dsiBase, DSI_CTRL, params[i]);

	vc4delay(10000);
}

bool DSIInitPanel(uint32_t dsiBase, uint32_t width, uint32_t height) {
	uint32_t phyc = DSIReadReg(dsiBase, DSI_PHYC);
	phyc |= (1 << 0);
	DSIWriteReg(dsiBase, DSI_PHYC, phyc);
	vc4delay(100000);

	DSIWriteReg(dsiBase, DSI_HSTX_TO_CNT, 0xFFFFFFFF);
	DSIWriteReg(dsiBase, DSI_LPRX_TO_CNT, 0xFFFFFFFF);
	DSIWriteReg(dsiBase, DSI_TA_TO_CNT, 0xFFFFFFFF);
	DSIWriteReg(dsiBase, DSI_PR_TO_CNT, 0xFFFFFFFF);

	DSIWriteReg(dsiBase, DSI_DISP0_CTRL, (1 << 31) | (0 << 30) | (width << 0));

	vc4delay(100000);

	DSISendShortCMD(dsiBase, DCS_SOFT_RESET, 0, false);
	vc4delay(1000000);

	DSISendShortCMD(dsiBase, DCS_SET_PIXEL_FORMAT, 0x77, true);
	vc4delay(10000);

	DSISendShortCMD(dsiBase, DCS_SET_DISPLAY_ON, 0, false);
	vc4delay(100000);

	return true;
}

void GPIOSetFunction(uint8_t pin, uint8_t function) {
	uint32_t reg = pin / 10;
	uint32_t shift = (pin % 10) * 3;
	uint32_t addr = GPFSEL0 + (reg * 4);
	AuTextOut("GPIO Addr : %x \r\n", addr);
	uint32_t val = vc4read(addr);
	val &= ~(7 << shift);
	val |= (function << shift);
	vc4write(addr, val);
}

void GPIOSet(uint8_t pin) {
	vc4write(GPSET0, 1 << pin);
}

void GPIOClear(uint8_t pin) {
	vc4write(GPCLR0, 1 << pin);
}

void GPIOWrite(uint8_t pin, bool value) {
	if (value)
		GPIOSet(pin);
	else
		GPIOClear(pin);
}

void SPIInit() {
	/* configure pin 9,10,11,8 to alt function 0*/
	GPIOSetFunction(9, 0b100);
	GPIOSetFunction(10, 0b100);
	GPIOSetFunction(11, 0b100);
	GPIOSetFunction(8, 0b100);

	GPIOSetFunction(GPIO_DC, 1);
	GPIOSetFunction(GPIO_RST, 1);
	GPIOSetFunction(GPIO_LED, 1);
	//volatile unsigned int* gPFSEL0 = (unsigned int*)(GPIO_BASE + 0x00);
	//volatile unsigned int* gPFSEL1 = (unsigned int*)(GPIO_BASE + 0x04);

	//*gPFSEL1 &= ~((7 << 27) | (7 << 24) | (7 << 21));
	//*gPFSEL1 |= (4 << 27) | (4 << 24) | (4 << 21);

	//*gPFSEL0 &= ~((7 << 21) | (7 << 24));
	//*gPFSEL0 |= (4 << 21) | (4 << 24);

	vc4write(SPI0_CLK, 64);

	uint32_t cs = SPI_CS_CLEAR_RX | SPI_CS_CLEAR_TX | SPI_CS_CS0 | (1<<7);
	AuTextOut("SPI CS Set to : %x \r\n", cs);
	vc4write(SPI0_CS, cs);
	vc4delay(100000000);
	//vc4write(SPI0_CS, SPI_CS_CS0);
	uint32_t csa = vc4read(SPI0_CS);
	uint32_t clk = vc4read(SPI0_CLK);
	AuTextOut("SPI CS After : %x \r\n", csa);
	AuTextOut("SPI0_CLK : %d \r\n", clk);
	uint32_t gps0 = vc4read(GPFSEL0);
	uint32_t gps1 = vc4read(GPFSEL1);
	AuTextOut("GPSEL0 : %x \r\n", gps0);
	AuTextOut("GPSEL1 : %x \r\n", gps1);
	AuTextOut("[aurora]: SPI initialized \r\n");
}

void SPITransfer(uint8_t data) {
	uint32_t cs = SPI_CS_TA | SPI_CS_CS0 | SPI_CS_CLEAR_TX | SPI_CS_CLEAR_RX;
	vc4write(SPI0_CS, cs);
	//AuTextOut("SPITransferring data \r\n");

    while (!((vc4read(SPI0_CS) & SPI_CS_TXD)));
	//AuTextOut("TXD is empty \r\n");

	vc4write(SPI0_FIFO, data);

	//AuTextOut("FIFO Data written \r\n");

	while (!((vc4read(SPI0_CS) & SPI_CS_DONE)));

	//AuTextOut("CS Done \r\n");

	while (!((vc4read(SPI0_CS) & SPI_CS_RXD))) {
		vc4read(SPI0_FIFO);
	}
	//cs = ~SPI_CS_TA | SPI_CS_CS0;
	vc4write(SPI0_CS,SPI_CS_CS0);
}

void SPITransferBuffer(const uint8_t* data, uint32_t len) {
	uint32_t cs = SPI_CS_TA | SPI_CS_CS0;

	vc4write(SPI0_CS,cs);

	for (uint32_t i = 0; i < len; i++) {
		while (!(vc4read(SPI0_CS) & SPI_CS_TXD));
		vc4write(SPI0_FIFO, data[i]);
		while (vc4read(SPI0_CS) & SPI_CS_RXD)
			vc4read(SPI0_FIFO);
	}

	while (!(vc4read(SPI0_CS) & SPI_CS_DONE));

	while(vc4read(SPI0_CS) & SPI_CS_RXD)
		vc4read(SPI0_FIFO);
	vc4write(SPI0_CS, SPI_CS_CS0);
}

void LCDWriteCommand(uint8_t cmd) {
	GPIOClear(GPIO_DC);
	SPITransfer(cmd);
}

void LCDWriteData(uint8_t data) {
	GPIOSet(GPIO_DC);
	SPITransfer(data);
}

void LCDWriteDataBuffer(const uint8_t* data, uint32_t len) {
	GPIOSet(GPIO_DC);
	SPITransferBuffer(data, len);
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
	//LCDWriteCommand(0xB0);
	//LCDWriteData(0x00);
	LCDWriteCommand(0x01);
	vc4delay(120000000);

	LCDWriteCommand(MIPI_DCS_EXIT_SLEEP_MODE);
	vc4delay(100000000);
	LCDWriteCommand(MIPI_DCS_SET_PIXEL_FORMAT);
	LCDWriteData(0x55);

	AuTextOut("ILI9486 Software reset completed \r\n");

	//LCDWriteCommand(0x28);
	LCDWriteCommand(0xC0);
	LCDWriteData(0x09);
	LCDWriteData(0x09);
	vc4delay(1200000);
	AuTextOut("Display turned off \r\n");

	LCDWriteCommand(0xC1);
	LCDWriteData(0x41);
	LCDWriteData(0x00);

	LCDWriteCommand(0xC2);
	LCDWriteData(0x44);

	vc4delay(1200000);
	LCDWriteCommand(0xC5);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);

	vc4delay(1200000);
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
	vc4delay(1200000);
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
	vc4delay(1200000);
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
	vc4delay(1200000);
	LCDWriteCommand(0x3A);
	LCDWriteData(0x55);
	vc4delay(1200000);
	LCDWriteCommand(0x36);
	LCDWriteData(0x28);
	vc4delay(1200000);
	LCDWriteCommand(0x11);
	vc4delay(1200000);
	vc4delay(1200000);
	LCDWriteCommand(0x29);
	vc4delay(200000);
}

void ILI9341Init() {
	LCDWriteCommand(0x01);
	vc4delay(12000000);

	LCDWriteCommand(0xCF);
	LCDWriteData(0x00);
	LCDWriteData(0xC1);
	LCDWriteData(0x30);

	LCDWriteCommand(0xED);
	LCDWriteData(0x64);
	LCDWriteData(0x03);
	LCDWriteData(0x12);
	LCDWriteData(0x81);

	LCDWriteCommand(0xCB);
	LCDWriteData(0x39);
	LCDWriteData(0x2C);
	LCDWriteData(0x00);
	LCDWriteData(0x34);
	LCDWriteData(0x02);

	LCDWriteCommand(0xC0);
	LCDWriteData(0x23);

	LCDWriteCommand(0xC1);
	LCDWriteData(0x10);
	
	LCDWriteCommand(0xC5);
	LCDWriteData(0x3E);
	LCDWriteData(0x28);

	LCDWriteCommand(0xC7);
	LCDWriteData(0x86);

	LCDWriteCommand(0x36);
	LCDWriteData(0x48);

	LCDWriteCommand(0x3A);
	LCDWriteData(0x55);

	LCDWriteCommand(0xB1);
	LCDWriteData(0x00);
	LCDWriteData(0x18);

	LCDWriteCommand(0x11);
	vc4delay(1200000);

	LCDWriteCommand(0x29);

}
void LCDInit() {
	GPIOSet(GPIO_RST);
	vc4delay(100000000);

	GPIOClear(GPIO_RST);
	vc4delay(100000000);

	GPIOSet(GPIO_RST);
	vc4delay(120000000);
	AuTextOut("[aurora]: Initializing LCD \r\n");
	ILI9486Init();
	//ILI9341Init();
	GPIOSet(GPIO_LED);
	vc4delay(100000000);
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
	uint8_t color_bytes[2];
	color_bytes[0] = (color >> 8);
	color_bytes[1] = color & 0xFF;

	GPIOSet(GPIO_DC);
	for (uint32_t i = 0; i < 480 * 320; i++) {
		SPITransfer(color_bytes[0]);
		SPITransfer(color_bytes[1]);
	}
}

void LCDPutPixel(uint16_t x, uint16_t y, uint16_t color) {
	LCDSetWindow(x, y, x, y);
	LCDWriteU16(color);
}

typedef enum {
	DISPLAY_TYPE_HDMI = 0,
	DISPLAY_TYPE_DSI = 1
}display_type_t;

typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t virtWidth;
	uint32_t virtHeight;
	uint32_t pitch;
	uint32_t depth;
	uint32_t x_offset;
	uint32_t y_offset;
	uint32_t framebuffer_addr;
	uint32_t framebuffer_size;
	display_type_t display_type;
	uint8_t displayID;
}vc4_fb_t;

static vc4_fb_t fb;

bool vc4InitDisplay(uint32_t width, uint32_t height, uint32_t depth, display_type_t display_type) {
	uint64_t pa = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	pa = (pa + 0xFULL) & ~0xFULL;
	mailBOX = (uint32_t*)pa;
	memset(mailBOX, 0, 4096);
	AuTextOut("[aurora]: Physical Address of MBOX : %x \r\n", pa);
	uint32_t idx = 0;
	uint32_t fbwidthptr = 0;
	uint32_t fbheightptr = 0;
	fb.display_type = display_type;
	fb.displayID = (display_type == DISPLAY_TYPE_DSI) ? 7 : 0;
	
	mailBOX[idx++] = 0;
	mailBOX[idx++] = 0;

	if (display_type == DISPLAY_TYPE_DSI) {
		mailBOX[idx++] = TAG_GET_DISPLAY_ID;
		mailBOX[idx++] = 4;
		mailBOX[idx++] = 4;
		mailBOX[idx++] = fb.displayID;
	}

	mailBOX[idx++] = TAG_SET_PHYS_WH;
	mailBOX[idx++] = 8;
	mailBOX[idx++] = 0;
	fbwidthptr = idx;
	mailBOX[idx++] = width;
	fbheightptr = idx;
	mailBOX[idx++] = height;

	//set virtual width/height
	mailBOX[idx++] = TAG_SET_VIRT_WH;
	mailBOX[idx++] = 8;
	mailBOX[idx++] = 8;
	mailBOX[idx++] = width;
	mailBOX[idx++] = height;

	//set depth
	mailBOX[idx++] = TAG_SET_DEPTH;
	mailBOX[idx++] = 4;
	mailBOX[idx++] = 4;
	mailBOX[idx++] = depth;

	//set pixel order
	mailBOX[idx++] = TAG_SET_PIXEL_ORDER;
	mailBOX[idx++] = 4;
	mailBOX[idx++] = 4;
	mailBOX[idx++] = 1;

	mailBOX[idx++] = TAG_ALLOCATE_BUFFER;
	mailBOX[idx++] = 8;
	mailBOX[idx++] = 4;
	uint32_t fbptr = idx;
	mailBOX[idx++] = 16;
	mailBOX[idx++] = 0;

	//allocate fb
	mailBOX[idx++] = TAG_GET_PITCH;
	mailBOX[idx++] = 4;
	mailBOX[idx++] = 4;
	uint32_t fbpitch = idx;
	mailBOX[idx++] = 0;

	//set backlight
	if (display_type == DISPLAY_TYPE_DSI) {
		mailBOX[idx++] = TAG_SET_BACKLIGHT;
		mailBOX[idx++] = 4;
		mailBOX[idx++] = 4;
		mailBOX[idx++] = 255;
	}

	mailBOX[idx++] = TAG_LAST;
	mailBOX[0] = idx * 4;

	mailbox_write((uint32_t)mailBOX, MAILBOX_CH_PROP);

	mailbox_read(MAILBOX_CH_PROP);

	if (mailBOX[1] != 0x80000000)
		return false;

	uint32_t respIDx = display_type = DISPLAY_TYPE_DSI ? 11 : 5;
	fb.width = mailBOX[respIDx];
	fb.height = mailBOX[respIDx + 1];
	fb.virtWidth = mailBOX[respIDx + 5];
	fb.virtHeight = mailBOX[respIDx + 6];
	fb.depth = mailBOX[respIDx + 10];
	fb.framebuffer_addr = mailBOX[fbptr] & 0x3FFFFFFF;
	fb.framebuffer_size = mailBOX[fbpitch];
	fb.pitch = mailBOX[respIDx + 23];
	fb.x_offset = 0;
	fb.y_offset = 0;

	AuTextOut("[aurora]: VC4 FB Addr : %x , SZ %d \r\n", fb.framebuffer_addr, fb.framebuffer_size);
	AuTextOut("[aurora]: pitch : %d \r\n", fb.pitch);

	if (display_type == DISPLAY_TYPE_DSI) {
		if (!DSIInitPanel(DSI1_BASE, width, height)) {
			AuTextOut("[aurora]: failed to initialize DSI display \r\n");
		}
	}
	AuTextOut("[aurora]: VC4 Mailbox response successfull \r\n");
	return true;

}

bool vc4InitDSI(uint32_t width, uint32_t height, uint32_t depth) {
	return vc4InitDisplay(width, height, depth, DISPLAY_TYPE_DSI);
}

void VC4ClearScreen(uint32_t color) {
	uint32_t* fbptr = (uint32_t*)fb.framebuffer_addr;
	uint32_t pixels = (fb.pitch * 320) / 4;
	for (uint32_t i = 0; i < pixels; i++)
		fbptr[i] = color;

	data_cache_flush((uint64_t*)fbptr);
}

void DSISetBacklight(uint8_t brightness) {
	uint32_t idx = 0;
	mailBOX[idx++] = 8 * 4;
	mailBOX[idx++] = 0;
	mailBOX[idx++] = TAG_SET_BACKLIGHT;
	mailBOX[idx++] = 4;
	mailBOX[idx++] = 4;
	mailBOX[idx++] = brightness;
	mailBOX[idx++] = TAG_LAST;
	mailbox_write((uint32_t)mailBOX, MAILBOX_CH_PROP);
}

uint32_t vc4rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

uint16_t rgb888_to_rgb565(uint32_t color) {
	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = color & 0xFF;

	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t rgb_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b << 3);
}
void AuVC4DSIInit() {
	if (!vc4InitDSI(480, 320, 32)) {
		AuTextOut("[aurora]: failed to initialize DSI display \r\n");
	}

	VC4ClearScreen(vc4rgb(255, 255, 0));

	SPIInit();

	LCDInit();
	LCDClear(rgb_to_rgb565(255,0,64));
	//vc4delay(10000);
	LCDPutPixel(30, 40, rgb_to_rgb565(255, 255, 255));

	AuTextOut("[aurora]: VC4 DSI Display initialized \r\n");
}

#endif