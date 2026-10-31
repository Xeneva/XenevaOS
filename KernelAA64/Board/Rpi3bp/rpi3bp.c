/**
* @file rpi3bp.c
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

#include <Hal/AA64/rpi3.h>
#include <Mm/vmmngr.h>
#include <Drivers/uart.h>
#include <Mm/pmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <aucon.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <dtb.h>
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Board/RPI3bp/rpi3bp_spi.h>
#include <Board/RPI3bp/rpi_ili9486.h>
#include <string.h>

#ifdef __TARGET_BOARD_RPI3__

uint64_t lpbase;
uint64_t pbase;
uint64_t systimerbase;
#define PERIPHERAL_BASE		  0x3F000000
#define LOCAL_PERIPHERAL_BASE 0x40000000

#define LOCAL_CONTROL	0x00
#define LOCAL_PRESCALER 0x08

#define CORE0_TIMER_IRQCNTL 0x40
#define CORE1_TIMER_IRQCNTL 0x44
#define CORE2_TIMER_IRQCNTL 0x48
#define CORE3_TIMER_IRQCNTL 0x4C

#define CORE0_MBOX_IRQCNTL 0x50
#define CORE1_MBOX_IRQCNTL 0x54
#define CORE2_MBOX_IRQCNTL 0x58
#define CORE3_MBOX_IRQCNTL 0x5C

#define CORE0_IRQ_SOURCE 0x60
#define CORE1_IRQ_SOURCE 0x64
#define CORE2_IRQ_SOURCE 0x68
#define CORE3_IRQ_SOURCE 0x6C

#define CORE0_FIQ_SOURCE 0x70
#define CORE1_FIQ_SOURCE 0x74
#define CORE2_FIQ_SOURCE 0x78
#define CORE3_FIQ_SOURCE 0x7C

#define LOCAL_GPU_INT_ROUTING 0x0C

/* peripheral base */
#define IRQ_BASIC_PENDING  0xb200
#define IRQ_PENDING_1	   0xb204
#define IRQ_PENDING_2	   0xb208
#define FIQ_CONTROL		   0xb20C
#define ENABLE_IRQS_1	   0xb210
#define ENABLE_IRQS_2	   0xb214
#define ENABLE_BASIC_IRQS  0xb218
#define DISABLE_IRQS_1	   0xb21C
#define DISABLE_IRQS_2	   0xb220
#define DISABLE_BASIC_IRQS 0xb224

uint32_t* mbox;
uint64_t vcmbox_mmio;

#define MBOX_READ	(VIDEOCORE_MBOX + 0x0)
#define MBOX_POLL	(VIDEOCORE_MBOX + 0x10)
#define MBOX_SENDER (VIDEOCORE_MBOX + 0x14)
#define MBOX_STATUS (VIDEOCORE_MBOX + 0x18)
#define MBOX_CONFIG (VIDEOCORE_MBOX + 0x1C)
#define MBOX_WRITE	(VIDEOCORE_MBOX + 0x20)

#define ARMCTRL_READ(addr)		 (*(volatile uint32_t*)(addr))
#define ARMCTRL_WRITE(addr, val) (*(volatile uint32_t*)(addr) = (val))

/** 
 * @brief AuRPI3GetCoreID -- get currently running core
 * number
 */
uint32_t AuRPI3GetCoreID() {
	uint64_t mpidr = read_mpidr_el1();
	return (mpidr & 0x3);
}

/**
 * @brief AuRPI3LocalIrqInit -- initialize local interrupts, i.e
 * interrupts related to each core
 */
void AuRPI3LocalIrqInit() {
	uint32_t coreID = AuRPI3GetCoreID();
	uint64_t localPbase = (uint64_t)AuMapMMIO(LOCAL_PERIPHERAL_BASE, 1);
	uint64_t pBase = (uint64_t)AuMapMMIO(PERIPHERAL_BASE, 4096);
	systimerbase = (uint64_t)AuMapMMIO(0x3F003000, 1);
	lpbase = localPbase;
	pbase = pBase;

	AuTextOut("[aurora]: coreID : %d \r\n", coreID);
	AuTextOut("[aurora]: local pheripheral mapped to : %x \r\n", lpbase);

	/* set the prescaler value to divider ratio of 1, setting the prescaler to
     * zero will stop the timer
     */
	// ARMCTRL_WRITE(lpbase + LOCAL_PRESCALER, 0x60000000);

	/* route all gpu interrupts to core 0*/
	ARMCTRL_WRITE(lpbase + LOCAL_GPU_INT_ROUTING, 0x00);

	/* initialize the core0 timer to reset value*/
	//uint64_t timer_ctrl_addr = lpbase + CORE0_TIMER_IRQCNTL + (coreID * 4);
	uint64_t mbox_ctrl_addr = lpbase + CORE0_MBOX_IRQCNTL + (coreID * 4);

	// ARMCTRL_WRITE(timer_ctrl_addr, 0);
	ARMCTRL_WRITE(mbox_ctrl_addr, 0);

	/* set the 64 bit core timer to run at crystal clock
       and increments by 1 step */
	//ARMCTRL_WRITE(lpbase + LOCAL_CONTROL, (0ULL << 8) | (0ULL < 9));

	/*uint64_t core_timer_ctrl = lpbase + 0x34;
    uint32_t reloadValue = 312500;
    ARMCTRL_WRITE(core_timer_ctrl, (1 << 29) | reloadValue);*/
}

/**
 * @brief AuRPI3LocalTimerIRQEnable -- enable timer related to each
 * core
 * @param timer_mask -- which timer type to enable or mask
 */
void AuRPI3LocalTimerIRQEnable(uint32_t timer_mask) {
	uint32_t core_id = AuRPI3GetCoreID();
	uint64_t timer_ctlr = lpbase + CORE0_TIMER_IRQCNTL + (core_id * 4);
	uint32_t ctrl = ARMCTRL_READ(timer_ctlr);
	ctrl |= timer_mask;
	ARMCTRL_WRITE(timer_ctlr, ctrl);
}

/**
 * @brief AuRPI3PeripheralIRQInit -- initialize peripheral irqs
 */
void AuRPI3PeripheralIRQInit() {
	ARMCTRL_WRITE(PERIPHERAL_BASE + DISABLE_IRQS_1, 0xFFFFFFFF);
	ARMCTRL_WRITE(PERIPHERAL_BASE + DISABLE_IRQS_2, 0xFFFFFFFF);
	ARMCTRL_WRITE(PERIPHERAL_BASE + DISABLE_BASIC_IRQS, 0xFFFFFFFF);
}

/**
 * @brief AuRPI3PeripheralIRQEnable -- enable particular peripheral irq
 * @param irq_num -- irq number of the specific peripheral
 */
void AuRPI3PeripheralIRQEnable(uint32_t irq_num) {
	if (irq_num < 32) {
		ARMCTRL_WRITE(PERIPHERAL_BASE + ENABLE_IRQS_1, 1 << irq_num);
	} else if (irq_num < 64) {
		AuTextOut("Enabling IRQ num : %d \n", irq_num);
		ARMCTRL_WRITE(PERIPHERAL_BASE + ENABLE_IRQS_2, 1 << (irq_num - 32));
	}

	dsb_sy_barrier();
	isb_flush();
}

/**
 * @brief AuRPI3PeripheralIRQDisable -- disable particular peripheral
 * irq
 * @param irq_num -- irq number of that peripheral
 */
void AuRPI3PeripheralIRQDisable(uint32_t irq_num) {
	if (irq_num < 32) {
		ARMCTRL_WRITE(PERIPHERAL_BASE + ENABLE_IRQS_1, 1 << irq_num);
	} else if (irq_num < 64) {
		ARMCTRL_WRITE(PERIPHERAL_BASE + ENABLE_IRQS_2, 1 << (irq_num - 32));
	}
}

/**
 * @brief AuRPI3LocalIRQGetPending -- read pending interrupts from
 * specific core
 */
uint32_t AuRPI3LocalIRQGetPending() {
	uint32_t core_id = AuRPI3GetCoreID();
	uint64_t irq_src_addr = lpbase + CORE0_IRQ_SOURCE + (core_id * 4);
	return ARMCTRL_READ(irq_src_addr);
}

/**
 * @brief AuRPI3PeripheralIRQGetPending2 -- read pending interrupts from
 * peripheral base group one
 */
uint32_t AuRPI3PeripheralIRQGetPending2() {
	uint64_t irq_src_addr = pbase + 0xb200 + 0x208;
	return ARMCTRL_READ(irq_src_addr);
}

/**
 * @brief AuRPI3PeripheralIRQGetPending1 -- read pending interrupts from
 * peripheral base group two
 */
uint32_t AuRPI3PeripheralIRQGetPending1() {
	uint64_t irq_src_addr = pbase + 0xb200 + 0x204;
	return ARMCTRL_READ(irq_src_addr);
}

/**
 * @brief AuRPI3TimerInit -- initialize and caliberate the
 * 64 bit periodic timer 
 * @param interval_ms -- not used
 */
void AuRPI3TimerInit(uint32_t interval_ms) {
	/*uint64_t freq = get_cntfrq_el0();
    uint64_t interval_ticks = (freq * interval_ms) / 1000;
    uint64_t current = get_cntpct_el0();
    set_cntp_cval_el0(current + interval_ticks);
    set_cntp_ctl_el0(1);*/
	setupTimerIRQ();
}

/** @brief RPI3ICInit -- initialize RPI3 local interrupt controller */
void AuRPI3ICInit() {
	AuRPI3TimerInit(10);
	AuRPI3LocalIrqInit();
	AuRPI3PeripheralIRQInit();
	AuRPI3LocalTimerIRQEnable(0x08);

	*(volatile uint32_t*)((uint64_t)lpbase + 0x0C) = (0ULL << 0) | (0LL << 2);
	dsb_sy_barrier();
	isb_flush();
	uint32_t v = *(volatile uint32_t*)(LOCAL_PERIPHERAL_BASE + LOCAL_GPU_INT_ROUTING);
	AuTextOut("Peripheral GPU is routed to %d \r\n", v);
	uint32_t v2 = *(volatile uint32_t*)(lpbase + LOCAL_GPU_INT_ROUTING);
	AuTextOut("P GPU2 : %d %d\r\n", ((v2 >> 0) & 0x3), ((v2 >> 2) & 0x3));
}
/**
 * @brief AuRPI3Initialize -- initialize the basic board requirement
 */
void AuRPI3Initialize() {
	vcmbox_mmio = (uint64_t)AuMapMMIO(VIDEOCORE_MBOX, 1);
	uint64_t pa = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	pa = (pa + 0xFULL) & ~0xFULL;
	mbox = (uint32_t*)pa;
	AuTextOut("[aurora]: Rasperry Pi 3b+ board initialized \r\n");
	AuTextOut("[aurora]: Video Core MBOX MMIO : %x \r\n", vcmbox_mmio);

	/*  map the GPIO base */
	AuRPI3BPGpioMap();

	/*  map and initialize SPI0 */
	AuRPI3SPI0Map();
	AuRPI3SPI0Init();
	AuRPIInitializeFramebuffer(800, 480, 32); //800x480  //1820x1080
											  // AuLCDInit();
											  // for (;;);
}

static inline void RPIMMIOWrite(uint32_t addr, uint32_t val) {
	*(volatile uint32_t*)addr = val;
	dsb_ish();
	isb_flush();
}

static inline uint32_t RPIMMIORead(uint32_t addr) {
	return *((volatile uint32_t*)addr);
}

/**
 * @brief AuRPI3WriteMailbox -- write to mailbox
 * @param channel -- Mailbox channel 
 */
void AuRPI3WriteMailbox(uint8_t channel) {
	data_cache_flush((uint64_t*)mbox);
	uint32_t pa = (uint32_t)mbox;
	AuTextOut("MBOX Physical address : %x \r\n", pa);
	AuTextOut("MBOX write addr : %x \r\n", MBOX_WRITE);
	while (RPIMMIORead(MBOX_STATUS) & MBOX_FULL)
		;
	RPIMMIOWrite(MBOX_WRITE, (pa & 0xFFFFFFF0ULL) | (channel & 0xFULL));
	dsb_ish();
	isb_flush();
}

/**
 * @brief AuRPI3ReadMailbox -- read from mailbox
 * @param channel -- mailbox channel
 */
uint32_t AuRPI3ReadMailbox(uint8_t channel) {
	uint32_t data;
	AuTextOut("Reading from MBOX : %x \r\n", MBOX_STATUS);
	while (1) {
		while (RPIMMIORead(MBOX_STATUS) & MBOX_EMPTY)
			;
		data = RPIMMIORead(MBOX_READ);
		if ((data & 0xF) == channel)
			return data & 0xFFFFFFF0;
	}
}

#define TIMER_CLO (*(volatile uint32_t*)((uint64_t)systimerbase + 0x04))
#define TIMER_CHI (*(volatile uint32_t*)((uint64_t)systimerbase + 0x08))

static inline uint64_t systimer_gettick() {
	uint32_t hi, lo;

	do {
		hi = *(volatile uint32_t*)((uint64_t)systimerbase + 0x08);
		lo = *(volatile uint32_t*)((uint64_t)systimerbase + 0x04);
	} while (hi != TIMER_CHI);

	return ((uint64_t)hi << 32) | lo;
}

/**
 * @brief AuRPI3DelayUS -- delay in uSeconds
 * @param us -- micro seconds to delay
 */
void AuRPI3DelayUS(uint32_t us) {
	uint64_t start = systimer_gettick();
	uint64_t end = start + us;
	if (end < start) {
		while (systimer_gettick() >= start)
			;
	}

	while (systimer_gettick() < end)
		;
}

/**
 * AuRPIDelayMS -- delay in milli seconds
 * @param ms -- Milliseconds to delay
 */
void AuRPIDelayMS(uint32_t ms) {
	AuRPI3DelayUS(ms * 1000);
}

/**
 * @brief AuRPIGetMBOXBuffer -- return currently using
 * mailbox buffer
 */
uint64_t* AuRPIGetMBOXBuffer() {
	return mbox;
}

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
	uint8_t displayID;
} rpi_fb_t;

static rpi_fb_t fb;
/**
 * @brief AuRPIInitializeFramebuffer -- initializes framebuffer
 * @param width -- fb width by pixels
 * @param height -- fb height by pixels
 * @param depth -- fb depth
 * @return true on success false in failure
 */
bool AuRPIInitializeFramebuffer(uint32_t width, uint32_t height, uint32_t depth) {
	AuTextOut("Requesting framebuffer \r\n");
	uint32_t idx = 0;
	uint32_t fbwidthptr = 0;
	uint32_t fbheightptr = 0;
	uint32_t virtwptr = 0;
	uint32_t virthptr = 0;
	uint32_t depthptr = 0;
	uint32_t fbsz = 0;

	mbox[idx++] = 35 * 4;
	mbox[idx++] = MBOX_REQUEST;

	mbox[idx++] = TAG_SET_PHYS_WH;
	mbox[idx++] = 8;
	mbox[idx++] = 0;
	fbwidthptr = idx;
	mbox[idx++] = width;
	fbheightptr = idx;
	mbox[idx++] = height;

	//set virtual width/height
	mbox[idx++] = TAG_SET_VIRT_WH;
	mbox[idx++] = 8;
	mbox[idx++] = 8;
	virtwptr = idx;
	mbox[idx++] = width;
	virthptr = idx;
	mbox[idx++] = height;

	//set depth
	mbox[idx++] = TAG_SET_DEPTH;
	mbox[idx++] = 4;
	mbox[idx++] = 4;
	depthptr = idx;
	mbox[idx++] = depth;

	//set pixel order
	mbox[idx++] = TAG_SET_PIXEL_ORDER;
	mbox[idx++] = 4;
	mbox[idx++] = 4;
	mbox[idx++] = 1;

	mbox[idx++] = TAG_ALLOCATE_BUFFER;
	mbox[idx++] = 8;
	mbox[idx++] = 4;
	uint32_t fbptr = idx;
	mbox[idx++] = 4096;
	fbsz = idx;
	mbox[idx++] = 0;

	//allocate fb
	mbox[idx++] = TAG_GET_PITCH;
	mbox[idx++] = 4;
	mbox[idx++] = 4;
	uint32_t fbpitch = idx;
	mbox[idx++] = 0;

	mbox[idx++] = TAG_LAST;
	mbox[0] = idx * 4;

	AuTextOut("Writing MBOX \r\n");
	AuRPI3WriteMailbox(MBOX_CH_PROP);
	AuTextOut("MBOX Written \r\n");
	AuRPI3ReadMailbox(MBOX_CH_PROP);

	if (mbox[1] != 0x80000000)
		return false;

	//uint32_t respIDx = display_type = DISPLAY_TYPE_DSI ? 11 : 5;
	fb.width = mbox[fbwidthptr];
	fb.height = mbox[fbheightptr];
	fb.virtWidth = mbox[virtwptr];
	fb.virtHeight = mbox[virthptr];
	fb.depth = mbox[depthptr];
	fb.framebuffer_addr = mbox[fbptr] & 0x3FFFFFFF;
	fb.framebuffer_size = mbox[fbsz];
	fb.pitch = mbox[fbpitch];
	fb.x_offset = 0;
	fb.y_offset = 0;

	AuTextOut("[aurora]: framebuffer initialized with addr : %x , Size : %x \r\n",
			  fb.framebuffer_addr,
			  fb.framebuffer_size);
	AuTextOut("[aurora]: framebuffer pitch : %d \r\n", fb.pitch);
	AuTextOut("[aurora]: framebuffer width : %d height : %d \r\n", fb.width, fb.height);

	uint32_t* fbptr_ = (uint32_t*)fb.framebuffer_addr;
	uint32_t pixels = (fb.width * fb.height) / 4;
	for (uint32_t x = 0; x < fb.width; x++)
		for (uint32_t y = 0; y < fb.height; y++)
			fbptr_[x + y * fb.width] = 0xFF7B5E5E;

	//  fbptr_[i] = 0xFFFFFFFF;

	data_cache_flush((uint64_t*)fbptr_);

	KERNEL_BOOT_INFO* info = AuGetBootInfoStruc();
	info->graphics_framebuffer = fb.framebuffer_addr;
	info->X_Resolution = fb.width;
	info->Y_Resolution = fb.height;
	info->fb_size = fb.framebuffer_size;
	info->pixels_per_line = 4096;
	info->redmask = 0x00FF0000;
	info->greenmask = 0x0000FF00;
	info->bluemask = 0x000000FF;

	AuTextOut("FB painted \r\n");
	return true;
}

/**
 * @brief AuRPISetPowerState -- set the power state
 * of a device through mailbox interface
 * @param device_id -- device id to power on
 */
int AuRPISetPowerState(uint32_t device_id) {
	memset(mbox, 0, PAGE_SIZE);
	mbox[0] = 9 * 4;
	mbox[1] = 0;

	/** set power state **/
	mbox[2] = 0x00028001;
	mbox[3] = 8;
	mbox[4] = 8;
	mbox[5] = device_id;
	mbox[6] = 0x3;

	mbox[7] = 0;
	mbox[8] = 0;
	AuRPI3WriteMailbox(MBOX_CH_PROP);

	AuRPI3ReadMailbox(MBOX_CH_PROP);

	if (mbox[1] != 0x80000000) {
		UARTDebugOut("[mailbox-set-power-state]: request failed : %x\r\n", mbox[1]);
		return 0;
	}

	if (!(mbox[6] & 0x1)) {
		UARTDebugOut("mailbox: device didn't power on \r\n");
		return 0;
	}

	UARTDebugOut("[mailbox]: USB HCD powered on , state= %x \r\n", mbox[6]);
	return 1;
}

#endif
