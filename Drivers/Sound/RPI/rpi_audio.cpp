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
#include "rpi_audio.h"
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/sched.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Sound/sound.h>
#include <Sync/spinlock.h>
#include <Drivers/uart.h>
#include <timer.h>
#include <aucon.h>
#include <string.h>
#include <_null.h>

/*
 * Analog audio on the Raspberry Pi 3 headphone jack -- external Aurora
 * driver (rpisnd.dll), loaded by the driver manager from /board.cnf.
 *
 * Written from the BCM2835 ARM Peripherals manual: PWM (chapter 9), the
 * general purpose clock manager (6.3), DMA (chapter 4) and the GPIO
 * alternate function table (6.2: GPIO40 ALT0 = PWM0, GPIO41 ALT0 = PWM1,
 * the jack's two channels on the Pi 3B/3B+).
 *
 *   clock  PLLD (500 MHz) / 2 = 250 MHz into the PWM block
 *   PWM    both channels in PWM (not mark/space) mode, fed from the shared
 *          FIFO, which takes channel 1 and channel 2 words alternately;
 *          range = 250 MHz / 48 kHz = 5208 steps (~12.3 bit)
 *   DMA    channel 5, paced by the PWM's DREQ (peripheral 5), loops over
 *          AUDIO_PERIODS control blocks of one period each
 *
 * write() turns a 4096 byte block (1024 frames of signed 16-bit stereo,
 * what the sound layer hands every card) into PWM levels in the next free
 * period and sleeps while all periods are queued: the card is the clock
 * the audio daemon runs on. A 5 ms kernel timer follows the DMA position
 * and puts silence into every period that finished without new data, so
 * an underrun is quiet instead of a repeating buzz.
 */

#define RPI_CM_BASE	   0x3F101000ULL
#define RPI_PWM_BASE   0x3F20C000ULL
#define RPI_DMA_BASE   0x3F007000ULL
#define RPI_PERIPH_BUS 0x7E000000u /* peripherals as DMA sees them */
#define RPI_MEM_BUS	   0xC0000000u /* SDRAM as DMA sees it, L2 uncached alias */

/* clock manager, PWM clock */
#define CM_PWMCTL	  0xA0
#define CM_PWMDIV	  0xA4
#define CM_PASSWORD	  0x5A000000u
#define CM_CTL_SRC_PLLD 6u
#define CM_CTL_ENAB	  (1u << 4)
#define CM_CTL_KILL	  (1u << 5)
#define CM_CTL_BUSY	  (1u << 7)
#define CM_DIV_DIVI(x) ((uint32_t)(x) << 12)

/* PWM */
#define PWM_CTL	 0x00
#define PWM_STA	 0x04
#define PWM_DMAC 0x08
#define PWM_RNG1 0x10
#define PWM_FIF1 0x18
#define PWM_RNG2 0x20
#define PWM_CTL_PWEN1 (1u << 0)
#define PWM_CTL_USEF1 (1u << 5)
#define PWM_CTL_CLRF1 (1u << 6)
#define PWM_CTL_PWEN2 (1u << 8)
#define PWM_CTL_USEF2 (1u << 13)
#define PWM_STA_ERRS  0x1FCu /* WERR1, RERR1, GAPO1..4, BERR */
#define PWM_DMAC_ENAB (1u << 31)
#define PWM_DMAC_PANIC(x) ((uint32_t)(x) << 8)
#define PWM_DMAC_DREQ(x)  ((uint32_t)(x))

/* DMA */
#define DMA_CHANNEL	  5
#define DMA_CH(off)	  (DMA_CHANNEL * 0x100 + (off))
#define DMA_CS		  0x00
#define DMA_CONBLK_AD 0x04
#define DMA_ENABLE	  0xFF0
#define DMA_CS_ACTIVE (1u << 0)
#define DMA_CS_END	  (1u << 1)
#define DMA_CS_INT	  (1u << 2)
#define DMA_CS_ERROR  (1u << 8)
#define DMA_CS_PRIORITY(x)		 ((uint32_t)(x) << 16)
#define DMA_CS_PANIC_PRIORITY(x) ((uint32_t)(x) << 20)
#define DMA_CS_WAIT_WRITES (1u << 28)
#define DMA_CS_RESET  (1u << 31)
#define DMA_TI_WAIT_RESP (1u << 3)
#define DMA_TI_DEST_DREQ (1u << 6)
#define DMA_TI_SRC_INC	 (1u << 8)
#define DMA_TI_PERMAP(x) ((uint32_t)(x) << 16)
#define DMA_TI_NO_WIDE_BURSTS (1u << 26)
#define DMA_PERMAP_PWM 5

#define AUDIO_RATE			48000u
#define AUDIO_PWM_CLOCK		250000000u
#define AUDIO_FRAMES		1024u /* per period: one 4096 byte block */
#define AUDIO_PERIODS		6u
#define AUDIO_PERIOD_WORDS	(AUDIO_FRAMES * 2) /* one word per channel */
#define AUDIO_PERIOD_BYTES	(AUDIO_PERIOD_WORDS * 4)
#define AUDIO_TRACK_MS		5
#define AUDIO_WRITE_TIMEOUT_MS 200

/* BCM2835 DMA control block, 32 byte aligned */
typedef struct {
	uint32_t ti;
	uint32_t source_ad;
	uint32_t dest_ad;
	uint32_t txfr_len;
	uint32_t stride;
	uint32_t nextconbk;
	uint32_t reserved[2];
} dma_cb_t;

static volatile uint8_t* cm;
static volatile uint8_t* pwm;
static volatile uint8_t* dma;

static uint64_t audio_phys;		   /* periods, then the control blocks */
static uint32_t* audio_period[AUDIO_PERIODS];
static dma_cb_t* audio_cb;
static uint32_t audio_cb_bus;
static uint32_t audio_range;
static uint32_t audio_silence;
static uint8_t audio_volume = 100;

/* position tracking (see the comment at the top) */
static Spinlock* audio_lock;
static uint64_t audio_played;	   /* periods finished since start */
static uint64_t audio_written;	   /* periods handed to the DMA */
static uint32_t audio_last_slot;   /* slot the DMA was on at the last look */
static uint64_t audio_slot_gen[AUDIO_PERIODS]; /* period index last written to a slot */
static bool audio_ready;
static AuSound audio_card;

static inline uint32_t rd(volatile uint8_t* base, uint32_t off) {
	return *(volatile uint32_t*)(base + off);
}

static inline void wr(volatile uint8_t* base, uint32_t off, uint32_t v) {
	*(volatile uint32_t*)(base + off) = v;
	dsb_sy_barrier();
}

static inline uint64_t audio_now_ms() {
	uint64_t cnt, frq;
	__asm__ volatile("isb\n mrs %0, cntpct_el0" : "=r"(cnt));
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	return (cnt / frq) * 1000ULL + ((cnt % frq) * 1000ULL) / frq;
}

static void audio_fill_silence(uint32_t slot) {
	uint32_t* p = audio_period[slot];
	for (uint32_t i = 0; i < AUDIO_PERIOD_WORDS; i++)
		p[i] = audio_silence;
	aa64_dc_cvac_range(p, AUDIO_PERIOD_BYTES);
}

/* slot of the control block the DMA engine is working on */
static uint32_t audio_dma_slot() {
	uint32_t conblk = rd(dma, DMA_CH(DMA_CONBLK_AD));
	if (conblk < audio_cb_bus)
		return audio_last_slot;
	uint32_t slot = (conblk - audio_cb_bus) / sizeof(dma_cb_t);
	return slot < AUDIO_PERIODS ? slot : audio_last_slot;
}

/*
 * audio_track -- advance the play counter to the DMA position and silence
 * every finished period that holds nothing newer than what just played.
 * Caller holds audio_lock
 */
static void audio_track() {
	uint32_t slot = audio_dma_slot();
	while (audio_last_slot != slot) {
		uint32_t done = audio_last_slot;
		uint64_t finished = audio_played; /* period index that just played */
		if (audio_slot_gen[done] <= finished)
			audio_fill_silence(done);
		audio_played++;
		audio_last_slot = (audio_last_slot + 1) % AUDIO_PERIODS;
	}
	/* an underrun: the writer is behind the DMA, start again just
	 * ahead of it */
	if (audio_written <= audio_played)
		audio_written = audio_played + 1;
}

static void audio_track_timer(void* param) {
	(void)param;
	if (!audio_ready)
		return;
	uint64_t f = AuAcquireSpinlockIrqSave(audio_lock);
	audio_track();
	AuReleaseSpinlockIrqRestore(audio_lock, f);
}

/*
 * audio_write -- sound layer write hook: 4096 bytes of S16LE stereo
 */
static int audio_write(uint8_t* buffer, size_t length) {
	if (!audio_ready || !buffer)
		return 1;
	const int16_t* in = (const int16_t*)buffer;
	uint32_t frames = (uint32_t)(length / 4);
	if (frames > AUDIO_FRAMES)
		frames = AUDIO_FRAMES;

	/* wait for a free period; this is what paces the audio daemon */
	uint64_t start = audio_now_ms();
	uint64_t f;
	for (;;) {
		f = AuAcquireSpinlockIrqSave(audio_lock);
		audio_track();
		if (audio_written - audio_played < AUDIO_PERIODS - 1)
			break;
		AuReleaseSpinlockIrqRestore(audio_lock, f);
		if (audio_now_ms() - start > AUDIO_WRITE_TIMEOUT_MS) {
			UARTDebugOut("[rpi3-audio]: DMA stopped advancing, block dropped \r\n");
			return 1;
		}
		/* sleep, letting the scheduler hand the BKL to others */
		if (AuSchedYieldMs(2) < 0)
			AA64SleepUS(2000);
	}
	uint64_t index = audio_written++;
	uint32_t slot = (uint32_t)(index % AUDIO_PERIODS);
	audio_slot_gen[slot] = index;
	AuReleaseSpinlockIrqRestore(audio_lock, f);

	/* S16 -> 0..range, scaled by the volume */
	uint32_t* out = audio_period[slot];
	uint32_t vol = audio_volume;
	for (uint32_t i = 0; i < frames * 2; i++) {
		int32_t s = in[i];
		if (vol < 100)
			s = (s * (int32_t)vol) / 100;
		out[i] = (uint32_t)(((uint64_t)(s + 32768) * audio_range) >> 16);
	}
	for (uint32_t i = frames * 2; i < AUDIO_PERIOD_WORDS; i++)
		out[i] = audio_silence;
	aa64_dc_cvac_range(out, AUDIO_PERIOD_BYTES);
	return 0;
}

static int audio_read(uint8_t* buffer, size_t length) {
	(void)buffer;
	(void)length;
	return 0;
}

/* the DMA runs for good (silence when idle): no pop at every start/stop */
static int audio_start() {
	return 0;
}

static int audio_stop() {
	return 0;
}

static int audio_set_vol(uint8_t vol) {
	audio_volume = vol > 100 ? 100 : vol;
	return 0;
}

static int audio_control(void* data, int code) {
	(void)data;
	(void)code;
	return 0;
}

/* PWM clock: stop, wait for !BUSY, set divider, select source, enable
 * (manual 6.3: never change source/divider while BUSY) */
static bool audio_clock_init() {
	wr(cm, CM_PWMCTL, CM_PASSWORD | (rd(cm, CM_PWMCTL) & ~CM_CTL_ENAB & 0xFFFu));
	uint64_t t = audio_now_ms();
	while (rd(cm, CM_PWMCTL) & CM_CTL_BUSY) {
		if (audio_now_ms() - t > 20) {
			/* KILL as the manual's last resort */
			wr(cm, CM_PWMCTL, CM_PASSWORD | CM_CTL_KILL);
			break;
		}
	}
	wr(cm, CM_PWMDIV, CM_PASSWORD | CM_DIV_DIVI(500000000u / AUDIO_PWM_CLOCK));
	wr(cm, CM_PWMCTL, CM_PASSWORD | CM_CTL_SRC_PLLD);
	wr(cm, CM_PWMCTL, CM_PASSWORD | CM_CTL_SRC_PLLD | CM_CTL_ENAB);
	t = audio_now_ms();
	while (!(rd(cm, CM_PWMCTL) & CM_CTL_BUSY)) {
		if (audio_now_ms() - t > 20)
			return false;
	}
	return true;
}

/**
 * @brief rpi_audio_initialize -- bring the headphone jack up
 */
static bool rpi_audio_initialize() {
	if (audio_ready)
		return true;
	cm = (volatile uint8_t*)AuMapMMIO(RPI_CM_BASE, 1);
	pwm = (volatile uint8_t*)AuMapMMIO(RPI_PWM_BASE, 1);
	dma = (volatile uint8_t*)AuMapMMIO(RPI_DMA_BASE, 1);
	audio_lock = AuCreateSpinlock(false);

	/* period buffers and control blocks, physically contiguous */
	uint32_t pages = (AUDIO_PERIODS * AUDIO_PERIOD_BYTES) / PAGE_SIZE + 1;
	audio_phys = AuPmmngrAllocPages(pages, 1, 0, AURORA_PAGE_KERNEL);
	if (audio_phys == PMM_INVALID_PHYS) {
		UARTDebugOut("[rpi3-audio]: no memory for the DMA buffers \r\n");
		return false;
	}
	uint8_t* base = (uint8_t*)P2V(audio_phys);
	for (uint32_t i = 0; i < AUDIO_PERIODS; i++)
		audio_period[i] = (uint32_t*)(base + i * AUDIO_PERIOD_BYTES);
	audio_cb = (dma_cb_t*)(base + AUDIO_PERIODS * AUDIO_PERIOD_BYTES);
	audio_cb_bus = (uint32_t)(audio_phys + AUDIO_PERIODS * AUDIO_PERIOD_BYTES) | RPI_MEM_BUS;

	audio_range = AUDIO_PWM_CLOCK / AUDIO_RATE;
	audio_silence = audio_range / 2;
	for (uint32_t i = 0; i < AUDIO_PERIODS; i++)
		audio_fill_silence(i);

	/* jack pins to the PWM */
	AuRPI3BPGpioMap();
	AuRPIGPIOSetFunction(40, RPI_GPIO_FUNC_ALT0);
	AuRPIGPIOSetFunction(41, RPI_GPIO_FUNC_ALT0);

	if (!audio_clock_init()) {
		UARTDebugOut("[rpi3-audio]: PWM clock did not start \r\n");
		return false;
	}

	/* PWM: off, ranges, clear the FIFO, both channels from the FIFO */
	wr(pwm, PWM_CTL, 0);
	AA64SleepUS(10);
	wr(pwm, PWM_STA, PWM_STA_ERRS);
	wr(pwm, PWM_RNG1, audio_range);
	wr(pwm, PWM_RNG2, audio_range);
	/* the range registers read back on real hardware; an emulator
	 * without a PWM model (QEMU raspi3b) reads 0, and its DMA model would
	 * spin forever on the looping control blocks below */
	if (rd(pwm, PWM_RNG1) != audio_range) {
		UARTDebugOut("[rpi3-audio]: no PWM block, no audio \r\n");
		return false;
	}
	wr(pwm, PWM_CTL, PWM_CTL_CLRF1);
	AA64SleepUS(10);
	wr(pwm, PWM_DMAC, PWM_DMAC_ENAB | PWM_DMAC_PANIC(7) | PWM_DMAC_DREQ(7));
	wr(pwm, PWM_CTL, PWM_CTL_PWEN1 | PWM_CTL_USEF1 | PWM_CTL_PWEN2 | PWM_CTL_USEF2);

	/* control blocks: period i -> PWM FIFO, then the next, looping */
	for (uint32_t i = 0; i < AUDIO_PERIODS; i++) {
		dma_cb_t* cb = &audio_cb[i];
		memset(cb, 0, sizeof(*cb));
		cb->ti = DMA_TI_PERMAP(DMA_PERMAP_PWM) | DMA_TI_DEST_DREQ | DMA_TI_SRC_INC |
				 DMA_TI_WAIT_RESP | DMA_TI_NO_WIDE_BURSTS;
		cb->source_ad = (uint32_t)(audio_phys + i * AUDIO_PERIOD_BYTES) | RPI_MEM_BUS;
		cb->dest_ad = RPI_PERIPH_BUS | (uint32_t)(RPI_PWM_BASE & 0xFFFFFF) | PWM_FIF1;
		cb->txfr_len = AUDIO_PERIOD_BYTES;
		cb->nextconbk = audio_cb_bus + ((i + 1) % AUDIO_PERIODS) * sizeof(dma_cb_t);
	}
	aa64_dc_cvac_range(audio_cb, AUDIO_PERIODS * sizeof(dma_cb_t));

	/* DMA channel: enable, reset, point at the first block, go */
	wr(dma, DMA_ENABLE, rd(dma, DMA_ENABLE) | (1u << DMA_CHANNEL));
	wr(dma, DMA_CH(DMA_CS), DMA_CS_RESET);
	AA64SleepUS(10);
	wr(dma, DMA_CH(DMA_CS), DMA_CS_END | DMA_CS_INT);
	wr(dma, DMA_CH(DMA_CONBLK_AD), audio_cb_bus);
	wr(dma, DMA_CH(DMA_CS), DMA_CS_ACTIVE | DMA_CS_PRIORITY(8) | DMA_CS_PANIC_PRIORITY(8) |
								DMA_CS_WAIT_WRITES);

	/* the DMA has to be moving, or there is no audio (no PWM, e.g. QEMU) */
	audio_last_slot = audio_dma_slot();
	uint64_t t = audio_now_ms();
	uint32_t first = audio_last_slot;
	bool moving = false;
	while (audio_now_ms() - t < 100) {
		if (audio_dma_slot() != first) {
			moving = true;
			break;
		}
	}
	if (!moving) {
		uint32_t cs = rd(dma, DMA_CH(DMA_CS));
		wr(dma, DMA_CH(DMA_CS), DMA_CS_RESET);
		wr(pwm, PWM_CTL, 0);
		UARTDebugOut("[rpi3-audio]: PWM DMA not advancing (cs %x, pwm sta %x), no audio \r\n", cs,
				  rd(pwm, PWM_STA));
		return false;
	}
	/* period numbers map to slots as index % AUDIO_PERIODS: start the
	 * count at the slot the DMA is on now */
	audio_last_slot = audio_dma_slot();
	audio_played = audio_last_slot;
	audio_written = audio_played + 1;
	memset(audio_slot_gen, 0, sizeof(audio_slot_gen));

	memset(&audio_card, 0, sizeof(audio_card));
	{
		static const char name[] = "rpi3-pwm";
		for (unsigned i = 0; i < sizeof(name); i++)
			audio_card.name[i] = name[i];
	}
	audio_card.write = audio_write;
	audio_card.read = audio_read;
	audio_card.start_output = audio_start;
	audio_card.stop_output = audio_stop;
	audio_card.set_vol = audio_set_vol;
	audio_card.control = audio_control;
	audio_card._force_write = true;

	audio_ready = true;
	AuroraTimerStartPeriodic(AUDIO_TRACK_MS, audio_track_timer, NULL);
	if (AuSoundRegisterCard(&audio_card)) {
		audio_ready = false;
		UARTDebugOut("[rpi3-audio]: sound layer refused the card \r\n");
		return false;
	}
	UARTDebugOut("[rpi3-audio]: headphone jack ready, %d Hz, range %d, DMA channel %d \r\n",
			  AUDIO_RATE, audio_range, DMA_CHANNEL);
	return true;
}

/*
 * AuDriverMain -- driver entry. Runs from the driver manager, after the
 * sound layer and the kernel timers are initialized and before the audio
 * daemon starts.
 */
AU_EXTERN AU_EXPORT int AuDriverMain(void* drv) {
	(void)drv;
	return rpi_audio_initialize() ? 0 : 1;
}

/*
 * AuDriverUnload -- the DMA keeps reading the period buffers, so the
 * driver stays resident
 */
AU_EXTERN AU_EXPORT int AuDriverUnload(void* drv) {
	(void)drv;
	return 0;
}
