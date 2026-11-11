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
#include <Board/RPI3bp/rpi3bp_xpt2046.h>
#include <Board/RPI3bp/rpi3bp_spi.h>
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/gic.h>
#include <Fs/Dev/devinput.h>
#include <Sync/bkl.h>
#include <Mm/vmmngr.h>
#include <Drivers/uart.h>
#include <aucon.h>
#include <string.h>

#ifdef __TARGET_BOARD_RPI3__

/*
 * XPT2046 / ADS7846 touch screen controller.
 *
 * Command byte (XPT2046 / ADS7846 datasheet, control byte table):
 *   bit 7     START
 *   bits 6..4 channel: 1 = Y, 3 = Z1, 4 = Z2, 5 = X
 *   bit 3     MODE, 0 = 12 bit
 *   bit 2     SER/DFR, 0 = differential (ratiometric, no reference needed)
 *   bits 1..0 PD: 01 = ADC on during the conversion (PENIRQ disabled),
 *                 00 = power down between conversions, PENIRQ enabled
 * Each conversion is a 3 byte transfer: the command, then 16 clocks during
 * which the 12 bit result comes out MSB first, one busy bit ahead of it and
 * three zero bits after it: value = ((rx[1] << 8 | rx[2]) >> 3) & 0xFFF.
 *
 * Read order: Y, X, Z1, Z2, then a power-down conversion so PENIRQ is
 * armed again. Touch resistance (datasheet, pressure equation 2):
 *   Rt = (z2 - z1) * x_plate / 16 * x / z1 / 256
 * Rt == 0 means no touch, Rt > pressure_max is too light a touch.
 *
 * Pen state: the pen is down while PENIRQ is low (pressure fluctuates
 * after a lift and is not trusted for that), the first read comes 1 ms
 * after PENIRQ fell, samples repeat every 5 ms, and each sample is dropped
 * if PENIRQ is high again right after it, since it was taken while
 * lifting. These timings were cross-checked against Linux's ads7846
 * driver; no code is taken from it.
 */

#define XPT_START	  0x80
#define XPT_CH_Y	  (1 << 4)
#define XPT_CH_Z1	  (3 << 4)
#define XPT_CH_Z2	  (4 << 4)
#define XPT_CH_X	  (5 << 4)
#define XPT_MODE_12	  (0 << 3)
#define XPT_DFR		  (0 << 2)
#define XPT_PD_ADC_ON 0x01
#define XPT_PD_DOWN	  0x00

#define XPT_READ(ch) (XPT_START | (ch) | XPT_MODE_12 | XPT_DFR | XPT_PD_ADC_ON)
#define XPT_CMD_Y	 XPT_READ(XPT_CH_Y)	 /* 0x91 */
#define XPT_CMD_X	 XPT_READ(XPT_CH_X)	 /* 0xD1 */
#define XPT_CMD_Z1	 XPT_READ(XPT_CH_Z1) /* 0xB1 */
#define XPT_CMD_Z2	 XPT_READ(XPT_CH_Z2) /* 0xC1 */
#define XPT_CMD_PWRDOWN (XPT_START | XPT_CH_Y | XPT_MODE_12 | XPT_DFR | XPT_PD_DOWN) /* 0x90 */

#define XPT_MAX_12BIT 0xFFF
#define XPT_SAMPLES	  5 /* per axis, the median is reported */

/* PENIRQ settles a few microseconds after the power-down conversion
 * before the pen-up recheck */
#define XPT_PENIRQ_SETTLE_US 50
/* first read after PENIRQ fell */
#define XPT_PEN_DOWN_DELAY_MS 1
/* how often the pen state is rechecked when no interrupt arrives */
#define XPT_IDLE_POLL_MS 50
/* PENIRQ low but not one usable sample for this long: give up on this
 * contact (release it) instead of waiting forever */
#define XPT_NO_SAMPLE_TIMEOUT_MS 300
/* touches whose samples are printed to the serial console */
#define XPT_LOG_TOUCHES 8

AuRPIXPT2046Config AuRPIXPT2046Cfg = {
	.chip_select = 1,
	.penirq_gpio = 25,
	.speed_hz = 50000,
	.x_min = 200,
	.x_max = 3900,
	.y_min = 200,
	.y_max = 3900,
	.swap_xy = false,
	.invert_x = false,
	.invert_y = false,
	.x_plate_ohms = 150,
	.pressure_max = 0,
	.poll_ms = 5,
};

static AuRPISPIDevice xpt_spi;
static bool xpt_ready;
static AA64Thread* xpt_thread;
static volatile uint32_t xpt_pen_irq;
static uint32_t xpt_irq_id;

/*
 * xpt_read -- one 12 bit conversion
 */
static int xpt_read(uint8_t cmd, uint16_t* out) {
	uint8_t tx[3] = {cmd, 0, 0};
	uint8_t rx[3] = {0, 0, 0};
	int ret = AuRPISPITransfer(&xpt_spi, tx, rx, 3);
	if (ret != RPI_SPI_OK)
		return ret;
	*out = (uint16_t)((((uint32_t)rx[1] << 8) | rx[2]) >> 3) & XPT_MAX_12BIT;
	return RPI_SPI_OK;
}

static uint16_t xpt_median(uint16_t* v, int n) {
	for (int i = 1; i < n; i++) {
		uint16_t key = v[i];
		int j = i;
		while (j > 0 && v[j - 1] > key) {
			v[j] = v[j - 1];
			j--;
		}
		v[j] = key;
	}
	return v[n / 2];
}

typedef struct {
	uint16_t x, y, z1, z2;
	int32_t rt;
	bool spi_ok;
} xpt_sample_t;

/*
 * xpt_sample -- read one filtered position plus the pressure inputs.
 * Always ends with the power-down conversion when the bus works, so
 * PENIRQ is armed for the pen-up check
 */
static void xpt_sample(xpt_sample_t* s) {
	uint16_t ys[XPT_SAMPLES], xs[XPT_SAMPLES], dummy;
	memset(s, 0, sizeof(*s));
	/* ~6 ms of SPI at 50 kHz: not under the BKL (a preemption may have
	 * handed the thread a level since its last sleep) */
	AuBKLReleaseAll();

	bool ok = true;
	for (int i = 0; ok && i < XPT_SAMPLES; i++)
		ok = xpt_read(XPT_CMD_Y, &ys[i]) == RPI_SPI_OK;
	for (int i = 0; ok && i < XPT_SAMPLES; i++)
		ok = xpt_read(XPT_CMD_X, &xs[i]) == RPI_SPI_OK;
	if (ok)
		ok = xpt_read(XPT_CMD_Z1, &s->z1) == RPI_SPI_OK && xpt_read(XPT_CMD_Z2, &s->z2) == RPI_SPI_OK;
	/* power down even after a failed read: never leave the controller
	 * converting with PENIRQ disabled */
	xpt_read(XPT_CMD_PWRDOWN, &dummy);
	s->spi_ok = ok;
	if (!ok)
		return;

	s->x = xpt_median(xs, XPT_SAMPLES);
	s->y = xpt_median(ys, XPT_SAMPLES);
	uint16_t x = (s->x == XPT_MAX_12BIT) ? 0 : s->x;

	int64_t rt = 0;
	if (x && s->z1) {
		rt = (int64_t)s->z2 - s->z1;
		rt *= AuRPIXPT2046Cfg.x_plate_ohms;
		rt = (rt + 8) / 16;
		rt *= x;
		rt /= s->z1;
		rt = (rt + 128) / 256;
	}
	s->rt = (int32_t)rt;
}

/* Rt == 0 is no touch, Rt above pressure_max is too light to trust */
static bool xpt_sample_valid(const xpt_sample_t* s) {
	if (!s->spi_ok || s->rt <= 0)
		return false;
	if (AuRPIXPT2046Cfg.pressure_max && s->rt > AuRPIXPT2046Cfg.pressure_max)
		return false;
	return true;
}

/*
 * xpt_scale -- raw value in [lo, hi] to [0, size)
 */
static int32_t xpt_scale(uint16_t raw, uint16_t lo, uint16_t hi, uint32_t size, bool invert) {
	if (hi <= lo || size == 0)
		return 0;
	if (raw < lo)
		raw = lo;
	if (raw > hi)
		raw = hi;
	int32_t v = (int32_t)(((uint32_t)(raw - lo) * (size - 1)) / (uint32_t)(hi - lo));
	return invert ? (int32_t)(size - 1) - v : v;
}

static void xpt_report(int32_t x, int32_t y, bool down) {
	AuInputMessage msg;
	memset(&msg, 0, sizeof(AuInputMessage));
	msg.type = AU_INPUT_TOUCH;
	msg.xpos = x;
	msg.ypos = y;
	msg.button_state = down ? LEFT_CLICK : 0;
	AuDevWriteMice(&msg);
}

static bool xpt_pen_down() {
	return AuRPIGPIOPinLevelLow(AuRPIXPT2046Cfg.penirq_gpio);
}

/*
 * xpt_wait_ms -- sleep the calling kernel thread. The thread is only
 * marked asleep; the next tick switches away from it (see the xhci poll
 * thread for why AuScheduleNext is not used from kernel threads)
 */
static void xpt_wait_ms(uint64_t ms) {
	AA64Thread* self = AuGetCurrentThread();
	/* never wait holding the BKL: every syscall in the system needs it */
	AuBKLReleaseAll();
	AuSleepThread(self, ms);
	while (*(volatile uint8_t*)&self->state == THREAD_STATE_SLEEP)
		__asm__ volatile("wfi");
	/* the scheduler resumes kernel threads with one BKL level */
	AuBKLReleaseAll();
}

/*
 * xpt_irq -- PENIRQ fell: stop edge detection until the pen is lifted
 * (the sampling thread re-arms it) and wake the thread
 */
static void xpt_irq(int irq) {
	(void)irq;
	if (!AuRPIGPIOCheckInterrupt(AuRPIXPT2046Cfg.penirq_gpio))
		return;
	AuRPIGPIODisableInterrupt(AuRPIXPT2046Cfg.penirq_gpio);
	__atomic_store_n(&xpt_pen_irq, 1, __ATOMIC_RELEASE);
	if (xpt_thread)
		AuThreadWakePoller(xpt_thread);
}

/*
 * xpt_wait_penirq_high -- after a contact, PENIRQ has to be high (pen up,
 * controller powered down) before edge detection is armed again. A line
 * that stays low is reported once and polled slowly, so a stuck or
 * miswired PENIRQ cannot keep the thread sampling forever
 */
static void xpt_wait_penirq_high() {
	bool warned = false;
	while (xpt_pen_down()) {
		if (!warned) {
			UARTDebugOut("[xpt2046]: PENIRQ (GPIO%d) stays low after the contact; check "
						 "penirq_gpio for this panel \r\n",
						 AuRPIXPT2046Cfg.penirq_gpio);
			warned = true;
		}
		uint16_t dummy;
		xpt_read(XPT_CMD_PWRDOWN, &dummy);
		xpt_wait_ms(XPT_IDLE_POLL_MS);
	}
}

static void xpt_thread_main(uint64_t arg) {
	(void)arg;
	AA64Thread* self = AuGetCurrentThread();
	/* lets xpt_irq pull this thread off the sleep list early */
	self->blocked_on_poll = 1;
	/* this thread touches only its own state, the SPI bus (own lock) and
	 * the lock-free input ring: it runs without the BKL */
	AuBKLReleaseAll();
	UARTDebugOut("[xpt2046]: sampling thread running, PENIRQ %s \r\n",
				 xpt_pen_down() ? "low" : "high");
	uint32_t touches = 0;

	for (;;) {
		/* idle: wait for PENIRQ, rechecking the line now and then in case
		 * an edge was missed */
		while (!__atomic_exchange_n(&xpt_pen_irq, 0, __ATOMIC_ACQ_REL) && !xpt_pen_down())
			xpt_wait_ms(XPT_IDLE_POLL_MS);

		/* PENIRQ toggles with every conversion: no edge detection while
		 * sampling, whichever way the pen was noticed */
		AuRPIGPIODisableInterrupt(AuRPIXPT2046Cfg.penirq_gpio);
		xpt_wait_ms(XPT_PEN_DOWN_DELAY_MS);

		bool log = touches < XPT_LOG_TOUCHES;
		touches++;
		int32_t last_x = -1, last_y = -1;
		bool reported = false;
		uint32_t samples = 0, rejected = 0;
		uint64_t last_valid_us = (get_cntpct_el0() * 1000000ULL) / get_cntfrq_el0();
		const char* why = "lifted";
		for (;;) {
			xpt_sample_t smp;
			xpt_sample(&smp);
			/* recheck PENIRQ right after the conversion: a sample taken
			 * while the finger lifts is garbage */
			AuRPI3DelayUS(XPT_PENIRQ_SETTLE_US);
			bool down = xpt_pen_down();
			bool touched = down && xpt_sample_valid(&smp);
			samples++;
			if (log && samples <= 3)
				UARTDebugOut("[xpt2046]: x %d y %d z1 %d z2 %d Rt %d %s \r\n", smp.x, smp.y,
							 smp.z1, smp.z2, smp.rt, touched ? "ok" : "rejected");
			uint64_t now_us = (get_cntpct_el0() * 1000000ULL) / get_cntfrq_el0();
			if (!touched)
				rejected++;
			else
				last_valid_us = now_us;
			if (touched) {
				uint16_t rx = smp.x, ry = smp.y;
				uint32_t w = AuConsoleGetScreenWidth();
				uint32_t h = AuConsoleGetScreenHeight();
				uint16_t ax = AuRPIXPT2046Cfg.swap_xy ? ry : rx;
				uint16_t ay = AuRPIXPT2046Cfg.swap_xy ? rx : ry;
				int32_t sx = xpt_scale(ax, AuRPIXPT2046Cfg.x_min, AuRPIXPT2046Cfg.x_max, w,
									   AuRPIXPT2046Cfg.invert_x);
				int32_t sy = xpt_scale(ay, AuRPIXPT2046Cfg.y_min, AuRPIXPT2046Cfg.y_max, h,
									   AuRPIXPT2046Cfg.invert_y);
				/* first contact always, then only real movement */
				if (!reported || sx - last_x > 1 || last_x - sx > 1 || sy - last_y > 1 ||
					last_y - sy > 1) {
					xpt_report(sx, sy, true);
					last_x = sx;
					last_y = sy;
					reported = true;
				}
			}
			if (!down)
				break;
			if (now_us - last_valid_us > XPT_NO_SAMPLE_TIMEOUT_MS * 1000ULL) {
				why = "no usable sample";
				break;
			}
			xpt_wait_ms(AuRPIXPT2046Cfg.poll_ms);
		}

		/* pen up: release where the last contact was */
		if (reported)
			xpt_report(last_x, last_y, false);
		if (log)
			UARTDebugOut("[xpt2046]: contact %d ended (%s): %d samples, %d rejected, %s \r\n",
						 touches, why, samples, rejected, reported ? "reported" : "nothing reported");
		xpt_wait_penirq_high();
		AuRPIGPIOEnableInterrupt(AuRPIXPT2046Cfg.penirq_gpio);
	}
}

/**
 * @brief AuRPIXPT2046Initialize -- configure PENIRQ and its interrupt
 */
bool AuRPIXPT2046Initialize() {
	if (xpt_ready)
		return true;
	if (!AuRPISPIPresent()) {
		AuTextOut("[xpt2046]: SPI0 not available, touch disabled \r\n");
		return false;
	}

	xpt_spi.chip_select = AuRPIXPT2046Cfg.chip_select;
	xpt_spi.mode = RPI_SPI_MODE_0;
	xpt_spi.speed_hz = AuRPIXPT2046Cfg.speed_hz;

	uint8_t pin = AuRPIXPT2046Cfg.penirq_gpio;
	AuRPIGPIOSetFunction(pin, RPI_GPIO_FUNC_INPUT);
	AuRPIGPIOSetPull(pin, RPI_GPIO_PULL_UP);

	/* power-down conversion: leaves PENIRQ enabled in the controller */
	uint16_t dummy;
	if (xpt_read(XPT_CMD_PWRDOWN, &dummy) != RPI_SPI_OK) {
		AuTextOut("[xpt2046]: no response on SPI0 CE%d, touch disabled \r\n",
				  AuRPIXPT2046Cfg.chip_select);
		return false;
	}

	/* PENIRQ shares the GPIO bank interrupt */
	xpt_irq_id = 32 + (pin < 32 ? RPI_GPIO_BANK0_IRQ : RPI_GPIO_BANK1_IRQ);
	GICRegisterSPIHandler((void*)xpt_irq, (int)xpt_irq_id);
	AuRPIGPIOEnableInterrupt(pin);
	GICEnableSPIIRQ(xpt_irq_id);

	xpt_ready = true;
	AuTextOut("[xpt2046]: touch on SPI0 CE%d, PENIRQ GPIO%d (irq %d), %d Hz \r\n",
			  AuRPIXPT2046Cfg.chip_select, pin, xpt_irq_id, AuRPIXPT2046Cfg.speed_hz);
	return true;
}

/**
 * @brief AuRPIXPT2046Start -- start the sampling thread
 */
void AuRPIXPT2046Start() {
	if (!xpt_ready || xpt_thread)
		return;
	xpt_thread = AuCreateKthread(xpt_thread_main, AuGetRootPageTable(), "xpt2046");
	if (!xpt_thread)
		AuTextOut("[xpt2046]: could not start the sampling thread \r\n");
}

#endif
