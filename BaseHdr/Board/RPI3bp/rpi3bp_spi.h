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
#ifndef __RPI3BP_SPI_H__
#define __RPI3BP_SPI_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * BCM2835/BCM2837 SPI0 master ("spi-bcm2835" in Linux), polled.
 *
 * Pins (ALT0): GPIO8 CE0, GPIO7 CE1, GPIO9 MISO, GPIO10 MOSI, GPIO11 SCLK.
 * Both chip selects are driven by the controller, active low.
 */

/* SPI clock modes (CPOL << 1 | CPHA), as in Linux */
#define RPI_SPI_MODE_0 0
#define RPI_SPI_MODE_1 1
#define RPI_SPI_MODE_2 2
#define RPI_SPI_MODE_3 3

/* return codes */
#define RPI_SPI_OK		  0
#define RPI_SPI_ERR_NODEV (-1) /* controller not initialized / not present */
#define RPI_SPI_ERR_ARG	  (-2)
#define RPI_SPI_ERR_TIME  (-3) /* FIFO or DONE never came: no controller */

/*
 * One device on the bus: which chip select it sits on, its clock mode and
 * the clock it wants. The controller rounds the clock down to the next
 * divider it can produce.
 */
typedef struct _rpi_spi_device_ {
	uint8_t chip_select; /* 0 = CE0 (GPIO8), 1 = CE1 (GPIO7) */
	uint8_t mode;		 /* RPI_SPI_MODE_x */
	uint32_t speed_hz;
} AuRPISPIDevice;

/*
 * AuRPISPIInitialize -- map SPI0, route its pins, power it through the
 * firmware and read the core clock it divides. Safe to call twice.
 * @return RPI_SPI_OK or an error code
 */
extern int AuRPISPIInitialize();

/*
 * AuRPISPIPresent -- true once AuRPISPIInitialize has succeeded
 */
extern bool AuRPISPIPresent();

/*
 * AuRPISPITransfer -- one full duplex transfer with chip select held
 * active for all of it
 * @param dev -- target device
 * @param tx -- bytes to send, NULL sends zeros
 * @param rx -- buffer for the received bytes, NULL discards them
 * @param len -- number of bytes
 * @return RPI_SPI_OK or an error code
 */
extern int AuRPISPITransfer(const AuRPISPIDevice* dev, const uint8_t* tx, uint8_t* rx,
							uint32_t len);

/*
 * AuRPISPIGetCoreClock -- clock SPI0 divides (VPU core clock), in Hz
 */
extern uint32_t AuRPISPIGetCoreClock();

#endif
