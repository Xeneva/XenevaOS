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
#ifndef __RPI3BP_XPT2046_H__
#define __RPI3BP_XPT2046_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * XPT2046 (ADS7846 compatible) resistive touch controller on SPI0.
 *
 * The defaults are the wiring and calibration of the Waveshare 5"/7" HDMI
 * LCDs, whose Linux overlay is
 *   dtoverlay=ads7846,cs=1,penirq=25,penirq_pull=2,speed=50000,
 *             swapxy=0,pmax=255,xohms=150,xmin=200,xmax=3900,ymin=200,ymax=3900
 * Other panels: adjust AuRPIXPT2046Config before AuRPIXPT2046Initialize.
 */
typedef struct _rpi_xpt2046_config_ {
	uint8_t chip_select; /* SPI0 CE line, 1 = CE1 (GPIO7) */
	uint8_t penirq_gpio; /* PENIRQ, active low */
	uint32_t speed_hz;	 /* SPI clock; the XPT2046 allows up to 2.5 MHz */
	uint16_t x_min, x_max, y_min, y_max; /* raw 12-bit range of the panel */
	bool swap_xy;
	bool invert_x;
	bool invert_y;
	uint16_t x_plate_ohms; /* for the pressure (touch resistance) estimate */
	/* samples with a touch resistance Rt above this are ignored as too
	 * light (Linux ti,pressure-max); 0 = no limit, the Linux default --
	 * the pen state itself always comes from PENIRQ */
	uint16_t pressure_max;
	uint16_t poll_ms; /* sample period while the pen is down (Linux: 5) */
} AuRPIXPT2046Config;

/* the configuration in use, writable before initialization */
extern AuRPIXPT2046Config AuRPIXPT2046Cfg;

/*
 * AuRPIXPT2046Initialize -- configure PENIRQ and its interrupt. Needs SPI0
 * up (AuRPISPIInitialize). Runs before the scheduler.
 * @return true if the touch controller is in use
 */
extern bool AuRPIXPT2046Initialize();

/*
 * AuRPIXPT2046Start -- start the sampling thread, after the scheduler is
 * initialized
 */
extern void AuRPIXPT2046Start();

#endif
