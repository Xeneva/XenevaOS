/**
* @file rpi3bpSPI.c
* 
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

#include <Board/RPI3bp/rpi3bp_spi.h>
#include <Hal/AA64/rpi3.h>
#include <aucon.h>
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/vmmngr.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Mm/pmmngr.h>
#include <Drivers/uart.h>
#include <Fs/Dev/devinput.h>

#ifdef __TARGET_BOARD_RPI3__

static uint64_t* spi0Base;

#define SPI0_CS	  0x00
#define SPI0_FIFO 0x04
#define SPI0_CLK  0x08

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
#define SPI_CS_CPOL		0x00000008
#define SPI_CS_CPHA		0x00000004

#define SPI0_GPIO_DC  25
#define SPI0_GPIO_RST 24 //25
#define SPI0_GPIO_LED 23 //18

#define SPI_MMIO_READ(off) *(volatile uint32_t*)((uint64_t)spi0Base + off)

#define SPI_MMIO_WRITE(off, val)                                                                   \
	do {                                                                                           \
		(*(volatile uint32_t*)((uint64_t)spi0Base + off)) = (val);                                 \
		dsb_sy_barrier();                                                                          \
		isb_flush();                                                                               \
	} while (0)

/**
 * @brief AuRPI3SPI0Map -- map the spi0 to kernel higher half address
 */
void AuRPI3SPI0Map() {
	spi0Base = (uint64_t*)AuMapMMIO(SPI0, 1);
	AuTextOut("[aurora]: RPI3 SPI0 mapped to %x address \r\n", spi0Base);
}

extern void AuRPIGPIOScanAll();
/**
 * @brief AuRPI3SPI0Init -- initialize SPI0 of RPI3bp
 */
void AuRPI3SPI0Init() {
	/* configure pin 9,10,11,8 to alt function 0*/
	AuRPIGPIOSetFunction(7, 0b100);
	AuRPIGPIOSetFunction(9, 0b100);
	AuRPIGPIOSetFunction(10, 0b100);
	AuRPIGPIOSetFunction(11, 0b100);
	AuRPIGPIOSetFunction(8, 0b100);

	AuRPIGPIOSetFunction(SPI0_GPIO_DC, 1);
	AuRPIGPIOSetFunction(SPI0_GPIO_RST, 1);
	AuRPIGPIOSetFunction(SPI0_GPIO_LED, 1);

	uint32_t cs_1 = SPI_MMIO_READ(SPI0_CS);
	AuTextOut("[aurora]:RPI3 SPI0 default cs-- %x\r\n", cs_1);

	SPI_MMIO_WRITE(SPI0_CS, (3ULL << 4));
	for (int i = 0; i < 10000000; i++)
		;

	uint32_t div = 256;
	*(volatile uint32_t*)((uint64_t)spi0Base + SPI0_CLK) = div;
	dsb_ish();
	isb_flush();

	if ((SPI_MMIO_READ(SPI0_CS) & SPI_CS_TA)) {
		AuTextOut("[aurora]: spi0 INIT ta is already active \r\n");
		SPI_MMIO_WRITE(SPI0_CS, 0);
		for (int i = 0; i < 10000000; i++)
			;
	}

	uint32_t cs = SPI_MMIO_READ(SPI0_CS); // | SPI_CS_TA;
	cs &= ~((1ULL << 2) | (1ULL << 3));
	cs |= (0ULL << 0);
	SPI_MMIO_WRITE(SPI0_CS, cs);

	for (int i = 0; i < 10000000; i++)
		;

	cs = SPI_MMIO_READ(SPI0_CS);
	cs &= ~(SPI_CS_CPOL | SPI_CS_CPHA);
	cs &= ~0x3;
	cs |= 0;
	SPI_MMIO_WRITE(SPI0_CS, cs);

	cs = SPI_MMIO_READ(SPI0_CS);
	SPI_MMIO_WRITE(SPI0_CS, cs | SPI_CS_DONE | SPI_CS_TXD);
	AuTextOut("SPI CS Set to : %x \r\n", cs);

	uint32_t cs_ = SPI_MMIO_READ(SPI0_CS);
	AuTextOut("[aurora]:RPI3 SPI0 initialized cs-- %x\r\n", cs_);
}

/**
 * @brief AuRPISPITransfer -- transfer a single
 * 1 byte data
 * @todo cs selection
 * @param data -- data to transfer
 */
void AuRPISPITransfer(uint8_t data) {
	while (!((SPI_MMIO_READ(SPI0_CS) & SPI_CS_TXD)))
		;
	AuTextOut("TXD is empty \r\n");

	SPI_MMIO_WRITE(SPI0_FIFO, data);

	AuTextOut("FIFO Data written \r\n");
	while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_RXD))
		SPI_MMIO_READ(SPI0_FIFO);

	while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_DONE))
		;
	AuTextOut("CS Done \r\n");

	while (SPI_MMIO_READ(SPI0_CS) & SPI_CS_RXD)
		SPI_MMIO_READ(SPI0_FIFO);
}

/**
 * @brief AuRPISPITransferBuffer -- transfer multiple 1 byte
 * aligned buffer data 
 * @param data -- pointer to buffer
 * @param len -- Length of total datas
 */
void AuRPISPITransferBuffer(const uint8_t* data, uint32_t len) {
	uint32_t cs = 0;
	SPI_MMIO_WRITE(SPI0_CS, cs);
	cs = SPI_CS_CLEAR_TX | SPI_CS_CLEAR_RX;
	SPI_MMIO_WRITE(SPI0_CS, cs);
	for (int i = 0; i < 1000000; i++)
		;
	cs = SPI_CS_TA | SPI_CS_CS0; // SPI_CS_CLEAR_TX | SPI_CS_CLEAR_RX;
	SPI_MMIO_WRITE(SPI0_CS, cs);

	for (uint32_t i = 0; i < len; i++) {
		while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_TXD))
			;
		SPI_MMIO_WRITE(SPI0_FIFO, data[i]);
		if (SPI_MMIO_READ(SPI0_CS) & SPI_CS_RXD)
			SPI_MMIO_READ(SPI0_FIFO);
	}

	while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_DONE))
		;

	while (SPI_MMIO_READ(SPI0_CS) & SPI_CS_RXD)
		SPI_MMIO_READ(SPI0_FIFO);

	cs = SPI_MMIO_READ(SPI0_CS);
	cs &= ~SPI_CS_TA;
	SPI_MMIO_WRITE(SPI0_CS, cs);
}

void AuRPISPIFifoWrite(uint32_t data) {
	SPI_MMIO_WRITE(SPI0_FIFO, data);
}

void AuRPISPITransferStart() {
	if ((SPI_MMIO_READ(SPI0_CS) & SPI_CS_TA)) {
		AuTextOut("[aurora]: spi0 ta is already active \r\n");
	}
	uint32_t css = SPI_MMIO_READ(SPI0_CS);
	if (!(css & SPI_CS_TXD))
		AuTextOut("TXD is full \r\n");
	uint32_t cs = css;
	cs |= SPI_CS_CLEAR_RX | SPI_CS_CLEAR_TX;
	cs &= ~0x3;
	cs |= 1ULL;
	cs |= SPI_CS_TA;
	SPI_MMIO_WRITE(SPI0_CS, cs);
}

void AuRPISPITrasnferWrite(uint32_t data) {
	while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_TXD))
		;

	/*if ((SPI_MMIO_READ(SPI0_CS) & SPI_CS_TA)) {
		AuTextOut("[aurora]: ## spi0 ta is already active \r\n");
	}*/

	SPI_MMIO_WRITE(SPI0_FIFO, data);
	if (SPI_MMIO_READ(SPI0_CS) & SPI_CS_RXD)
		SPI_MMIO_READ(SPI0_FIFO);

	while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_DONE))
		;
}

void AuRPISPITransferStop() {
	while (SPI_MMIO_READ(SPI0_CS) & SPI_CS_RXD)
		SPI_MMIO_READ(SPI0_FIFO);

	uint32_t cs = SPI_MMIO_READ(SPI0_CS);
	cs &= ~SPI_CS_TA;
	SPI_MMIO_WRITE(SPI0_CS, cs);
}

#define ADS_START		 (1 << 7)
#define ADS_A2A1A0_d_y	 (1 << 4)
#define ADS_A2A1A0_D_Z1	 (3 << 4)
#define ADS_A2A1A0_D_Z2	 (4 << 4)
#define ADS_A2A1A0_D_X	 (5 << 4)
#define ADS_A2A1A0_TEMP0 (0 << 4)
#define ADS_A2A1A0_VBATT (2 << 4)
#define ADS_A2A1A0_VAUX	 (6 << 4)
#define ADS_A2A1A0_TEMP1 (7 << 4)
#define ADS_8_BIT		 (1 << 3)
#define ADS_12_BIT		 (0 << 3)
#define ADS_SER			 (1 << 2)
#define ADS_DFR			 (0 << 2)
#define ADS_PD10_PDOWN	 (0 << 0)
#define ADS_PD10_ADC_ON	 (1 << 0)
#define ADS_PD10_REF_ON	 (2 << 0)
#define ADS_PD10_ALL_ON	 (3 << 0)
#define MAX_12BIT		 ((1 << 12) - 1)

#define READ_12BIT_DFR (x, adc, cref)(A)

#define XPT2046_CMD_TEMP0  0x84
#define XPT2046_CMD_TEMP1  0xF4
#define XPT2046_CMD_XPOS   0xD1 // 0x90
#define XPT2046_CMD_YPOS   0x91 //0xD0
#define XPT2046_CMD_Z1_POS 0xB1
#define XPT2046_CMD_Z2_POS 0xC1

static inline void spi_wait_txd() {
	while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_TXD))
		;
}

static inline void spi_wait_rxd() {
	while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_RXD))
		;
}

static inline uint8_t spi_fifo_read() {
	return *(volatile uint32_t*)((uint64_t)spi0Base + SPI0_FIFO);
}

static inline void spi_fifo_write(uint8_t v) {
	*(volatile uint32_t*)((uint64_t)spi0Base + SPI0_FIFO) = v;
}

uint16_t XPT2046Read(uint8_t command) {
	uint8_t hi, lo;

	uint32_t cs = SPI_MMIO_READ(SPI0_CS);
	cs |= SPI_CS_CLEAR_RX | SPI_CS_CLEAR_TX;
	SPI_MMIO_WRITE(SPI0_CS, cs);

	AuRPISPITransferStart();

	spi_wait_txd();
	spi_fifo_write(command);

	spi_wait_rxd();
	spi_fifo_read();

	spi_wait_txd();
	spi_fifo_write(0x00);
	spi_wait_rxd();
	hi = spi_fifo_read();

	spi_wait_txd();
	spi_fifo_write(0x00);
	spi_wait_rxd();
	lo = spi_fifo_read();

	while (!(SPI_MMIO_READ(SPI0_CS) & SPI_CS_DONE))
		;
	AuRPISPITransferStop();

	for (int i = 0; i < 200; i++)
		;

	uint16_t v = ((hi << 8) | lo) >> 3;
	return v & 0x0FFF;
}

uint16_t XPT2046ReadSimple(uint8_t command) {
	uint8_t buffer[3];
	buffer[0] = command;
	buffer[1] = 0x00;
	buffer[2] = 0x00;

	AuRPISPITransferBuffer(buffer, 3);

	return 0;
}

#define PENIRQ_PIN	25
#define TOUCH_X_MIN 300
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN 300
#define TOUCH_Y_MAX 3900

int touch_x;
int touch_y;

void XPT2046Initialise() {
	AuTextOut("XPT2046 initializing \r\n");
	touch_x = 0;
	touch_y = 0;
	/*uint16_t temp0 = XPT2046Read(XPT2046_CMD_TEMP0);
	AuTextOut("TEMP0 = %x - %d\r\n", temp0, temp0);

	uint16_t temp1 = XPT2046Read(XPT2046_CMD_TEMP1);
	AuTextOut("Temp1: %x - %d \r\n", temp1, temp1);

	uint16_t x = XPT2046Read(XPT2046_CMD_XPOS);
	uint16_t y = XPT2046Read(XPT2046_CMD_YPOS);
	uint16_t z1 = XPT2046Read(XPT2046_CMD_Z1_POS);
	uint16_t z2 = XPT2046Read(XPT2046_CMD_Z2_POS);*/

	//AuTextOut("Position - X: %d Y : %d  \r\n", x, y);
	AuRPIGPIOSetFunction(PENIRQ_PIN, 0b000);
	AuRPIGPIOPullUP(PENIRQ_PIN);
	AuRPIGPIOEnableInterrupt(PENIRQ_PIN);
	AuRPI3PeripheralIRQEnable(49);
}

#define Z_MIN			   80
#define Z_MAX			   2000
#define PRESSURE_THRESHOLD 300
#define PRESSURE_MAX	   3000
#define NUM_SAMPLES		   7

static inline int pressurevalid(int z) {
	return (z > Z_MIN && z < Z_MAX);
}

#define RAW_X_MIN 200
#define RAW_X_MAX 4095

#define RAW_Y_MIN 200
#define RAW_Y_MAX 4095

static inline int touchToScrX(uint16_t rx) {
	if (rx < RAW_X_MIN)
		rx = RAW_X_MIN;
	if (rx > RAW_X_MAX)
		rx = RAW_X_MAX;

	return ((RAW_X_MAX - rx) * 800) / (RAW_X_MAX - RAW_X_MIN);
}

static inline int touchToScrY(uint16_t ry) {
	if (ry < RAW_Y_MIN)
		ry = RAW_Y_MIN;
	if (ry > RAW_Y_MAX)
		ry = RAW_Y_MAX;

	return ((RAW_Y_MAX - ry) * 420) / (RAW_Y_MAX - RAW_Y_MIN);
}

static inline void swap(uint16_t* a, uint16_t* b) {
	uint16_t t = *a;
	*a = *b;
	*b = t;
}

static inline void sort(uint16_t* arr, int n) {
	for (int i = 0; i < n - 1; i++) {
		for (int j = 0; j < n - i - 1; j++) {
			if (arr[j] > arr[j + 1]) {
				uint16_t temp = arr[j];
				arr[j] = arr[j + 1];
				arr[j + 1] = temp;
			}
		}
	}
}

uint16_t XPT2046ReadAxis(uint8_t cmd) {
	uint8_t s[5];
	XPT2046Read(cmd);

	for (int i = 0; i < 5; i++) {
		s[i] = XPT2046Read(cmd);
		AuTextOut("CMD : %x val[%d]: %d \r\n", cmd, i, s[i]);
	}

	return s[2];
}

void XPT2046ReadSamples(uint16_t* x_out, uint16_t* y_out, int32_t* pressout) {
	uint16_t x_samples[NUM_SAMPLES];
	uint16_t y_samples[NUM_SAMPLES];
	uint16_t z1_samples[NUM_SAMPLES];
	uint16_t z2_samples[NUM_SAMPLES];

	uint16_t z1 = XPT2046Read(XPT2046_CMD_Z1_POS);
	uint16_t z2 = XPT2046Read(XPT2046_CMD_Z2_POS);
	*pressout = z1 + 4095 - z2;

	XPT2046Read(XPT2046_CMD_YPOS);
	AuRPI3DelayUS(20);

	for (int i = 0; i < NUM_SAMPLES; i++) {
		y_samples[i] = XPT2046Read(XPT2046_CMD_YPOS); //0x91
		AuRPI3DelayUS(20);
		x_samples[i] = XPT2046Read(XPT2046_CMD_XPOS);
		AuRPI3DelayUS(20);
	}

	XPT2046Read(0x90);

	sort(x_samples, NUM_SAMPLES);
	sort(y_samples, NUM_SAMPLES);
	sort(z1_samples, NUM_SAMPLES);
	sort(z2_samples, NUM_SAMPLES);

	uint16_t x_median = x_samples[NUM_SAMPLES / 2];
	uint16_t y_median = y_samples[NUM_SAMPLES / 2];
	//uint16_t z1_median = z1_samples[NUM_SAMPLES / 2];
	//uint16_t z2_median = z2_samples[NUM_SAMPLES / 2];

	*x_out = x_median;
	*y_out = y_median;
	//if (z1_median > 0) {
	//	*pressout = (x_median * (z2_median - z1_median)) / z1_median;
	//}
	//else {
	//	*pressout = 0;
	//}
}
void XPT2046ReadTouch() {
	AA64SleepMS(20);

	uint16_t x;
	uint16_t y;
	uint32_t z;
	XPT2046ReadSamples(&x, &y, &z);

	x = 4095 - x;
	y = 4095 - y;
	//AuTextOut("Control x comm val : %x y - %x\r\n",XPT2046_READ_X, XPT2046_READ_Y);
	//int screen_x = ((x - TOUCH_X_MIN) * 800) / (TOUCH_X_MAX - TOUCH_X_MIN);
	//int screen_y = ((y - TOUCH_Y_MIN) * 480) / (TOUCH_Y_MAX - TOUCH_Y_MIN);
	//if (z > PRESSURE_THRESHOLD && z < PRESSURE_MAX) {
	if (z > PRESSURE_THRESHOLD) {
		touch_x = touchToScrX(x);
		touch_y = touchToScrY(y);
		/*touch_x = 800 - 1 - touch_x;
		touch_y = 420 - 1 - touch_y;*/

		//}
		AuInputMessage im;
		im.type = AU_INPUT_TOUCH;
		im.xpos = touch_x;
		im.ypos = touch_y;
		AuDevWriteMice(&im);
	}
}

#endif