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
#include <Board/RPI3bp/rpi3bp_spi.h>
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/rpi3.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/vmmngr.h>
#include <Sync/spinlock.h>
#include <Drivers/uart.h>
#include <aucon.h>

#ifdef __TARGET_BOARD_RPI3__

/*
 * BCM2835/BCM2837 SPI0 master, polled.
 *
 * Register layout, bits and the transfer sequence follow the BCM2835 ARM
 * Peripherals manual, section 10 (cross-checked against Linux's
 * spi-bcm2835 driver; no code is taken from it):
 *   - reset: CS = CLEAR_RX | CLEAR_TX, everything else 0
 *   - per transfer: CLK = even divider of the core clock, CS = chip select |
 *     CPOL/CPHA | CLEAR_RX | CLEAR_TX, then the same | TA to start
 *   - fill the TX FIFO while TXD, drain the RX FIFO while RXD, until every
 *     byte is sent and received, then wait for DONE and drop TA
 */

#define SPI_CS	 0x00
#define SPI_FIFO 0x04
#define SPI_CLK	 0x08
#define SPI_DLEN 0x0C
#define SPI_LTOH 0x10
#define SPI_DC	 0x14

#define SPI_CS_LEN_LONG 0x02000000
#define SPI_CS_DMA_LEN	0x01000000
#define SPI_CS_CSPOL2	0x00800000
#define SPI_CS_CSPOL1	0x00400000
#define SPI_CS_CSPOL0	0x00200000
#define SPI_CS_RXF		0x00100000
#define SPI_CS_RXR		0x00080000
#define SPI_CS_TXD		0x00040000
#define SPI_CS_RXD		0x00020000
#define SPI_CS_DONE		0x00010000
#define SPI_CS_LEN		0x00002000
#define SPI_CS_REN		0x00001000
#define SPI_CS_ADCS		0x00000800
#define SPI_CS_INTR		0x00000400
#define SPI_CS_INTD		0x00000200
#define SPI_CS_DMAEN	0x00000100
#define SPI_CS_TA		0x00000080
#define SPI_CS_CSPOL	0x00000040
#define SPI_CS_CLEAR_RX 0x00000020
#define SPI_CS_CLEAR_TX 0x00000010
#define SPI_CS_CPOL		0x00000008
#define SPI_CS_CPHA		0x00000004
#define SPI_CS_CS_10	0x00000002
#define SPI_CS_CS_01	0x00000001

/* firmware clock id of the VPU core clock SPI0 is fed from */
#define RPI_CLOCK_ID_CORE 4
/* used when the mailbox does not answer: core_freq default with
 * enable_uart=1 on the Pi 3 */
#define RPI_CORE_CLOCK_DEFAULT 250000000u

/* a byte at the slowest divider (65536) of a 250 MHz clock takes ~2 ms */
#define SPI_TIMEOUT_US 100000

static volatile uint8_t* spi_base;
static uint32_t spi_core_clock;
static bool spi_ready;
static Spinlock* spi_lock;

static inline uint32_t spi_read(uint32_t off) {
	return *(volatile uint32_t*)(spi_base + off);
}

static inline void spi_write(uint32_t off, uint32_t val) {
	*(volatile uint32_t*)(spi_base + off) = val;
}

static inline uint64_t spi_now_us() {
	return (get_cntpct_el0() * 1000000ULL) / get_cntfrq_el0();
}

/**
 * @brief AuRPISPIGetCoreClock -- clock SPI0 divides, in Hz
 */
uint32_t AuRPISPIGetCoreClock() {
	return spi_core_clock;
}

/**
 * @brief AuRPISPIPresent -- true once the controller is set up
 */
bool AuRPISPIPresent() {
	return spi_ready;
}

/*
 * spi_reset -- stop any transfer and empty both FIFOs (BCM2835 manual
 * bcm2835_spi_reset_hw)
 */
static void spi_reset() {
	uint32_t cs = spi_read(SPI_CS);
	cs &= ~(SPI_CS_INTR | SPI_CS_INTD | SPI_CS_DMAEN | SPI_CS_TA);
	cs |= SPI_CS_CLEAR_RX | SPI_CS_CLEAR_TX;
	spi_write(SPI_CS, cs);
	spi_write(SPI_DLEN, 0);
}

/*
 * spi_divider -- CDIV for a target clock: SCLK = core / CDIV, CDIV even,
 * 0 meaning 65536 (BCM2835 manual 10.5, CLK register)
 */
static uint32_t spi_divider(uint32_t speed_hz) {
	if (speed_hz == 0)
		return 0;
	if (speed_hz >= spi_core_clock / 2)
		return 2;
	uint32_t cdiv = (spi_core_clock + speed_hz - 1) / speed_hz;
	cdiv += (cdiv % 2);
	if (cdiv >= 65536)
		cdiv = 0;
	return cdiv;
}

/**
 * @brief AuRPISPIInitialize -- bring SPI0 up
 */
int AuRPISPIInitialize() {
	if (spi_ready)
		return RPI_SPI_OK;

	if (!spi_base)
		spi_base = (volatile uint8_t*)AuMapMMIO(SPI0, 1);
	if (!spi_lock)
		spi_lock = AuCreateSpinlock(false);

	/* the firmware power domain for SPI; the Pi 3 firmware keeps it on,
	 * asking again is harmless and covers firmware that does not */
	if (!AuRPISetPowerState(RPI_POWERUP_SPI))
		AuTextOut("[spi0]: firmware did not confirm SPI power, continuing \r\n");

	spi_core_clock = AuRPIGetClockRate(RPI_CLOCK_ID_CORE);
	if (spi_core_clock == 0)
		spi_core_clock = RPI_CORE_CLOCK_DEFAULT;

	/* GPIO7..11 to ALT0: CE1, CE0, MISO, MOSI, SCLK */
	for (uint8_t pin = 7; pin <= 11; pin++)
		AuRPIGPIOSetFunction(pin, RPI_GPIO_FUNC_ALT0);

	spi_reset();
	/* chip selects active low, default length/DMA timing */
	spi_write(SPI_CS, SPI_CS_CLEAR_RX | SPI_CS_CLEAR_TX);

	/* the controller is there if TXD (TX FIFO can accept data) reads set
	 * after a reset: QEMU's raspi3b, for one, has no SPI0 model */
	if (!(spi_read(SPI_CS) & SPI_CS_TXD)) {
		AuTextOut("[spi0]: controller not responding, SPI disabled \r\n");
		return RPI_SPI_ERR_NODEV;
	}

	spi_ready = true;
	AuTextOut("[spi0]: ready, core clock %d Hz, mmio %x \r\n", spi_core_clock, spi_base);
	return RPI_SPI_OK;
}

/**
 * @brief AuRPISPITransfer -- full duplex polled transfer
 */
int AuRPISPITransfer(const AuRPISPIDevice* dev, const uint8_t* tx, uint8_t* rx, uint32_t len) {
	if (!spi_ready)
		return RPI_SPI_ERR_NODEV;
	if (!dev || dev->chip_select > 1 || len == 0)
		return RPI_SPI_ERR_ARG;

	uint32_t cs = SPI_CS_CLEAR_RX | SPI_CS_CLEAR_TX;
	cs |= dev->chip_select; /* CS field: 0 = CE0, 1 = CE1 */
	if (dev->mode & 2)
		cs |= SPI_CS_CPOL;
	if (dev->mode & 1)
		cs |= SPI_CS_CPHA;

	AuAcquireSpinlock(spi_lock);
	spi_write(SPI_CLK, spi_divider(dev->speed_hz));
	/* program mode and chip select with the FIFOs cleared, then start */
	spi_write(SPI_CS, cs);
	spi_write(SPI_CS, cs | SPI_CS_TA);

	uint32_t tx_done = 0, rx_done = 0;
	int ret = RPI_SPI_OK;
	uint64_t start = spi_now_us();
	while (rx_done < len) {
		uint32_t st = spi_read(SPI_CS);
		/* keep the TX FIFO no more than 16 bytes ahead of RX, so the RX
		 * FIFO (64 bytes) can never overflow */
		while (tx_done < len && (st & SPI_CS_TXD) && (tx_done - rx_done) < 16) {
			spi_write(SPI_FIFO, tx ? tx[tx_done] : 0);
			tx_done++;
			st = spi_read(SPI_CS);
		}
		while (rx_done < len && (st & SPI_CS_RXD)) {
			uint8_t b = (uint8_t)spi_read(SPI_FIFO);
			if (rx)
				rx[rx_done] = b;
			rx_done++;
			st = spi_read(SPI_CS);
		}
		if (spi_now_us() - start > SPI_TIMEOUT_US + (uint64_t)len * 2000) {
			ret = RPI_SPI_ERR_TIME;
			break;
		}
	}

	if (ret == RPI_SPI_OK) {
		start = spi_now_us();
		while (!(spi_read(SPI_CS) & SPI_CS_DONE)) {
			if (spi_now_us() - start > SPI_TIMEOUT_US) {
				ret = RPI_SPI_ERR_TIME;
				break;
			}
		}
	}

	/* drop TA (raises chip select) and leave the FIFOs empty */
	spi_reset();
	AuReleaseSpinlock(spi_lock);

	if (ret != RPI_SPI_OK)
		UARTDebugOut("[spi0]: transfer of %d bytes on CE%d timed out \r\n", len,
					 dev->chip_select);
	return ret;
}

#endif
