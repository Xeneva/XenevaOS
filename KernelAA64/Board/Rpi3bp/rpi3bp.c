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
#include <Hal/AA64/aa64percpu.h>
#include <aucon.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <dtb.h>
#include <string.h>
#include <Sync/spinlock.h>
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Board/RPI3bp/rpi3bp_spi.h>
#include <Board/RPI3bp/rpi3bp_xpt2046.h>

#ifdef __TARGET_BOARD_RPI3__

/*
 * BCM2837 (Raspberry Pi 3B/3B+) board support.
 *
 * Every register block is reached through AuMapMMIO (Device-nGnRnE in the
 * kernel's MMIO window); nothing here touches a physical address directly,
 * so all of it keeps working after AuVmmngrBootFree drops the identity map.
 *
 * Interrupts: the BCM2837 has no GIC. Each core has a local source register
 * in the ARM local peripheral block (0x40000000) that reports its generic
 * timers, its four mailboxes and the "GPU" line, which carries the legacy
 * BCM2835 peripheral controller (0x3F00B200) and is routed to core 0.
 * Kernel interrupt ids are mapped onto that as:
 *   0, 1            IPIs (AA64_IPI_RESCHED / AA64_IPI_DEFERRED_IRQ), sent as
 *                   bits of the target core's local mailbox 0
 *   27              per core virtual timer (CNTV), same id as the GIC PPI
 *   32 + n          BCM2835 peripheral IRQ n (0..63)
 */

#define RPI_SYSTIMER_BASE 0x3F003000ULL
#define RPI_ARMCTRL_BASE  0x3F00B000ULL /* IRQ controller + VideoCore mailbox */
#define RPI_LOCAL_BASE	  0x40000000ULL

/* ARM local peripherals */
#define LOCAL_CONTROL		  0x00
#define LOCAL_PRESCALER		  0x08
#define LOCAL_GPU_INT_ROUTING 0x0C
#define CORE_TIMER_IRQCNTL(c) (0x40 + 4 * (c))
#define CORE_MBOX_IRQCNTL(c)  (0x50 + 4 * (c))
#define CORE_IRQ_SOURCE(c)	  (0x60 + 4 * (c))
#define CORE_MBOX0_SET(c)	  (0x80 + 0x10 * (c))
#define CORE_MBOX0_RDCLR(c)	  (0xC0 + 0x10 * (c))

#define LOCAL_TIMER_CNTV_IRQ (1u << 3)
#define LOCAL_MBOX0_IRQ		 (1u << 0)

/* legacy peripheral interrupt controller, offsets in the ARMCTRL page */
#define IRQ_BASIC_PENDING  0x200
#define IRQ_PENDING_1	   0x204
#define IRQ_PENDING_2	   0x208
#define ENABLE_IRQS_1	   0x210
#define ENABLE_IRQS_2	   0x214
#define ENABLE_BASIC_IRQS  0x218
#define DISABLE_IRQS_1	   0x21C
#define DISABLE_IRQS_2	   0x220
#define DISABLE_BASIC_IRQS 0x224

/* VideoCore mailbox 0, offsets in the ARMCTRL page */
#define VCMBOX_READ	  0x880
#define VCMBOX_STATUS 0x898
#define VCMBOX_WRITE  0x8A0

/* ARM physical -> VideoCore bus address, L2 uncached alias */
#define VC_BUS_ALIAS 0xC0000000u

static volatile uint8_t* lpbase;
static volatile uint8_t* armctrl;
static volatile uint8_t* systimer;
static uint64_t irq_enabled; /* software copy of the enabled BCM2835 IRQs */

static uint64_t mbox_phys;
static volatile uint32_t* mbox;
static Spinlock* mbox_lock;

static inline void mmio_write32(volatile uint8_t* base, uint32_t off, uint32_t v) {
	*(volatile uint32_t*)(base + off) = v;
}

static inline uint32_t mmio_read32(volatile uint8_t* base, uint32_t off) {
	return *(volatile uint32_t*)(base + off);
}

/**
 * @brief AuRPI3GetCoreID -- physical core number of the caller
 */
uint32_t AuRPI3GetCoreID() {
	return (uint32_t)(read_mpidr_el1() & 0x3);
}

/**
 * @brief AuRPI3MapMMIO -- map the register blocks the kernel core needs
 */
static void AuRPI3MapMMIO() {
	if (lpbase)
		return;
	lpbase = (volatile uint8_t*)AuMapMMIO(RPI_LOCAL_BASE, 1);
	armctrl = (volatile uint8_t*)AuMapMMIO(RPI_ARMCTRL_BASE, 1);
	systimer = (volatile uint8_t*)AuMapMMIO(RPI_SYSTIMER_BASE, 1);
	AuTextOut("[rpi3]: local peripherals -> %x, armctrl -> %x, systimer -> %x \r\n",
			  lpbase, armctrl, systimer);
}

/**
 * @brief AuRPI3EnableLocalIRQ -- enable a per core interrupt (timer or
 * IPI mailbox) on the calling core
 * @param irq -- kernel interrupt id: 27 (CNTV) or an IPI number
 */
void AuRPI3EnableLocalIRQ(uint32_t irq) {
	if (!lpbase)
		return;
	uint32_t core = AuRPI3GetCoreID();
	if (irq == 27) {
		uint32_t v = mmio_read32(lpbase, CORE_TIMER_IRQCNTL(core));
		mmio_write32(lpbase, CORE_TIMER_IRQCNTL(core), v | LOCAL_TIMER_CNTV_IRQ);
	} else if (irq < 16) {
		uint32_t v = mmio_read32(lpbase, CORE_MBOX_IRQCNTL(core));
		mmio_write32(lpbase, CORE_MBOX_IRQCNTL(core), v | LOCAL_MBOX0_IRQ);
	}
	dsb_sy_barrier();
}

/**
 * @brief AuRPI3LocalCoreInit -- per core interrupt state, called on every
 * core: nothing enabled yet, stale mailbox bits cleared
 */
void AuRPI3LocalCoreInit() {
	if (!lpbase)
		return;
	uint32_t core = AuRPI3GetCoreID();
	mmio_write32(lpbase, CORE_TIMER_IRQCNTL(core), 0);
	mmio_write32(lpbase, CORE_MBOX_IRQCNTL(core), 0);
	mmio_write32(lpbase, CORE_MBOX0_RDCLR(core), 0xFFFFFFFF);
	dsb_sy_barrier();
}

/**
 * @brief AuRPI3SendIPI -- raise an IPI on another core
 * @param core -- physical core number (MPIDR Aff0)
 * @param ipi -- IPI number, 0..31
 */
void AuRPI3SendIPI(uint32_t core, uint32_t ipi) {
	if (!lpbase || core > 3)
		return;
	/* whatever the target is woken up for must be visible first */
	__asm__ volatile("dsb ishst" ::: "memory");
	mmio_write32(lpbase, CORE_MBOX0_SET(core), 1u << ipi);
}

/**
 * @brief AuRPI3IPIReadClear -- fetch and acknowledge the calling core's
 * pending IPIs
 */
uint32_t AuRPI3IPIReadClear() {
	uint32_t core = AuRPI3GetCoreID();
	uint32_t v = mmio_read32(lpbase, CORE_MBOX0_RDCLR(core));
	if (v)
		mmio_write32(lpbase, CORE_MBOX0_RDCLR(core), v);
	return v;
}

/**
 * @brief AuRPI3LocalIRQGetPending -- interrupt sources pending on the
 * calling core (INT_SRC_*)
 */
uint32_t AuRPI3LocalIRQGetPending() {
	return mmio_read32(lpbase, CORE_IRQ_SOURCE(AuRPI3GetCoreID()));
}

/**
 * @brief AuRPI3PeripheralIRQEnable -- enable a BCM2835 peripheral irq
 * @param irq_num -- peripheral irq number, 0..63
 */
void AuRPI3PeripheralIRQEnable(uint32_t irq_num) {
	if (!armctrl || irq_num >= 64)
		return;
	if (irq_num < 32)
		mmio_write32(armctrl, ENABLE_IRQS_1, 1u << irq_num);
	else
		mmio_write32(armctrl, ENABLE_IRQS_2, 1u << (irq_num - 32));
	__atomic_fetch_or(&irq_enabled, 1ULL << irq_num, __ATOMIC_RELEASE);
	dsb_sy_barrier();
}

/**
 * @brief AuRPI3PeripheralIRQDisable -- disable a BCM2835 peripheral irq
 * @param irq_num -- peripheral irq number, 0..63
 */
void AuRPI3PeripheralIRQDisable(uint32_t irq_num) {
	if (!armctrl || irq_num >= 64)
		return;
	if (irq_num < 32)
		mmio_write32(armctrl, DISABLE_IRQS_1, 1u << irq_num);
	else
		mmio_write32(armctrl, DISABLE_IRQS_2, 1u << (irq_num - 32));
	__atomic_fetch_and(&irq_enabled, ~(1ULL << irq_num), __ATOMIC_RELEASE);
	dsb_sy_barrier();
}

/**
 * @brief AuRPI3PeripheralIRQPending -- pending and enabled BCM2835
 * peripheral irqs, bit n = irq n
 */
uint64_t AuRPI3PeripheralIRQPending() {
	uint64_t p1 = mmio_read32(armctrl, IRQ_PENDING_1);
	uint64_t p2 = mmio_read32(armctrl, IRQ_PENDING_2);
	return ((p2 << 32) | p1) & __atomic_load_n(&irq_enabled, __ATOMIC_ACQUIRE);
}

uint32_t AuRPI3PeripheralIRQGetPending1() {
	return mmio_read32(armctrl, IRQ_PENDING_1);
}

uint32_t AuRPI3PeripheralIRQGetPending2() {
	return mmio_read32(armctrl, IRQ_PENDING_2);
}

/**
 * @brief AuRPI3ICInit -- initialize the BCM2836 ARM local interrupt
 * controller and the BCM2835 peripheral controller, boot core part
 */
void AuRPI3ICInit() {
	AuRPI3MapMMIO();

	/* GPU (peripheral) interrupts: IRQ on core 0 */
	mmio_write32(lpbase, LOCAL_GPU_INT_ROUTING, 0);

	/* every peripheral irq off until a driver asks for it */
	mmio_write32(armctrl, DISABLE_IRQS_1, 0xFFFFFFFF);
	mmio_write32(armctrl, DISABLE_IRQS_2, 0xFFFFFFFF);
	mmio_write32(armctrl, DISABLE_BASIC_IRQS, 0xFFFFFFFF);
	irq_enabled = 0;

	AuRPI3LocalCoreInit();
	dsb_sy_barrier();
	isb_flush();
	AuTextOut("[rpi3]: bcm2836 local interrupt controller ready, gpu irq -> core %d \r\n",
			  mmio_read32(lpbase, LOCAL_GPU_INT_ROUTING) & 0x3);
}

/* ------------------------------------------------------------------
 * delays
 * ------------------------------------------------------------------ */

static inline uint64_t systimer_gettick() {
	uint32_t hi, lo;
	do {
		hi = mmio_read32(systimer, 0x08);
		lo = mmio_read32(systimer, 0x04);
	} while (hi != mmio_read32(systimer, 0x08));
	return ((uint64_t)hi << 32) | lo;
}

/**
 * @brief AuRPI3DelayUS -- busy wait, the system timer runs at 1 MHz
 * @param us -- micro seconds to delay
 */
void AuRPI3DelayUS(uint32_t us) {
	if (!systimer) {
		uint64_t ticks = (get_cntfrq_el0() / 1000000ULL) * us;
		uint64_t start = get_cntpct_el0();
		while ((get_cntpct_el0() - start) < ticks)
			;
		return;
	}
	uint64_t start = systimer_gettick();
	while ((systimer_gettick() - start) < us)
		;
}

/**
 * AuRPIDelayMS -- delay in milli seconds
 * @param ms -- Milliseconds to delay
 */
void AuRPIDelayMS(uint32_t ms) {
	AuRPI3DelayUS(ms * 1000);
}

/* ------------------------------------------------------------------
 * VideoCore property mailbox
 * ------------------------------------------------------------------ */

static void mbox_dcache_clean_inval(void) {
	for (uint64_t va = (uint64_t)mbox; va < (uint64_t)mbox + PAGE_SIZE; va += 64)
		__asm__ volatile("dc civac, %0" ::"r"(va) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

static void mbox_dcache_inval(void) {
	for (uint64_t va = (uint64_t)mbox; va < (uint64_t)mbox + PAGE_SIZE; va += 64)
		__asm__ volatile("dc ivac, %0" ::"r"(va) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

/**
 * @brief AuRPI3MailboxCall -- run the request in the mailbox buffer
 * on a mailbox channel and wait for the VideoCore's answer
 * @param channel -- mailbox channel
 * @return true if the firmware reported success
 */
static bool AuRPI3MailboxCall(uint8_t channel) {
	uint32_t msg = ((uint32_t)mbox_phys | VC_BUS_ALIAS) | (channel & 0xF);
	/* the VideoCore reads and writes the buffer in memory */
	mbox_dcache_clean_inval();
	while (mmio_read32(armctrl, VCMBOX_STATUS) & MBOX_FULL)
		;
	mmio_write32(armctrl, VCMBOX_WRITE, msg);
	for (;;) {
		while (mmio_read32(armctrl, VCMBOX_STATUS) & MBOX_EMPTY)
			;
		if (mmio_read32(armctrl, VCMBOX_READ) == msg)
			break;
	}
	mbox_dcache_inval();
	return mbox[1] == MBOX_RESPONSE;
}

/**
 * @brief AuRPIGetMBOXBuffer -- the property mailbox buffer
 */
uint64_t* AuRPIGetMBOXBuffer() {
	return (uint64_t*)mbox;
}

/**
 * @brief AuRPI3GetBoardRevision -- board revision code from the firmware,
 * 0 on failure (also proves the mailbox works)
 */
static uint32_t AuRPI3GetBoardRevision() {
	uint64_t flags = AuAcquireSpinlockIrqSave(mbox_lock);
	memset((void*)mbox, 0, PAGE_SIZE);
	mbox[0] = 7 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = TAG_GET_BOARD_REV;
	mbox[3] = 4;
	mbox[4] = 0;
	mbox[5] = 0;
	mbox[6] = TAG_LAST;
	uint32_t rev = AuRPI3MailboxCall(MBOX_CH_PROP) ? mbox[5] : 0;
	AuReleaseSpinlockIrqRestore(mbox_lock, flags);
	return rev;
}

/**
 * @brief AuRPIInitializeFramebuffer -- allocate a framebuffer through
 * the mailbox, only used when the boot loader did not hand one over
 * @param width -- fb width by pixels
 * @param height -- fb height by pixels
 * @param depth -- fb depth
 * @return true on success false in failure
 */
bool AuRPIInitializeFramebuffer(uint32_t width, uint32_t height, uint32_t depth) {
	uint64_t flags = AuAcquireSpinlockIrqSave(mbox_lock);
	memset((void*)mbox, 0, PAGE_SIZE);
	uint32_t i = 2;
	mbox[i++] = TAG_SET_PHYS_WH; mbox[i++] = 8; mbox[i++] = 8;
	uint32_t wh = i; mbox[i++] = width; mbox[i++] = height;
	mbox[i++] = TAG_SET_VIRT_WH; mbox[i++] = 8; mbox[i++] = 8;
	mbox[i++] = width; mbox[i++] = height;
	mbox[i++] = TAG_SET_DEPTH; mbox[i++] = 4; mbox[i++] = 4;
	mbox[i++] = depth;
	mbox[i++] = TAG_SET_PIXEL_ORDER; mbox[i++] = 4; mbox[i++] = 4;
	uint32_t order = i; mbox[i++] = 0;
	mbox[i++] = TAG_ALLOCATE_BUFFER; mbox[i++] = 8; mbox[i++] = 8;
	uint32_t buf = i; mbox[i++] = 4096; mbox[i++] = 0;
	mbox[i++] = TAG_GET_PITCH; mbox[i++] = 4; mbox[i++] = 4;
	uint32_t pitch = i; mbox[i++] = 0;
	mbox[i++] = TAG_LAST;
	mbox[0] = i * 4;
	mbox[1] = MBOX_REQUEST;

	bool ok = AuRPI3MailboxCall(MBOX_CH_PROP) && mbox[buf] != 0;
	uint32_t fbw = mbox[wh], fbh = mbox[wh + 1], fbaddr = mbox[buf] & 0x3FFFFFFF;
	uint32_t fbsize = mbox[buf + 1], fbpitch = mbox[pitch], rgb = mbox[order];
	AuReleaseSpinlockIrqRestore(mbox_lock, flags);
	if (!ok) {
		AuTextOut("[rpi3]: framebuffer request failed \r\n");
		return false;
	}

	KERNEL_BOOT_INFO* info = AuGetBootInfoStruc();
	info->graphics_framebuffer = (uint32_t*)(uint64_t)fbaddr;
	info->X_Resolution = fbw;
	info->Y_Resolution = fbh;
	info->fb_size = fbsize;
	info->pixels_per_line = fbpitch / 4;
	info->redmask = rgb ? 0x000000FF : 0x00FF0000;
	info->greenmask = 0x0000FF00;
	info->bluemask = rgb ? 0x00FF0000 : 0x000000FF;
	AuTextOut("[rpi3]: framebuffer %x size %x, %dx%d pitch %d \r\n",
			  fbaddr, fbsize, fbw, fbh, fbpitch);
	return true;
}

/**
 * @brief AuRPIGetClockRate -- current rate of a firmware clock
 * @param clock_id -- firmware clock id (1 EMMC, 2 UART, 3 ARM, 4 CORE, ...)
 * @return rate in Hz, 0 on failure
 */
uint32_t AuRPIGetClockRate(uint32_t clock_id) {
	if (!mbox)
		return 0;
	uint64_t flags = AuAcquireSpinlockIrqSave(mbox_lock);
	memset((void*)mbox, 0, PAGE_SIZE);
	mbox[0] = 8 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = 0x00030002; /* get clock rate */
	mbox[3] = 8;
	mbox[4] = 0;
	mbox[5] = clock_id;
	mbox[6] = 0;
	mbox[7] = TAG_LAST;
	bool ok = AuRPI3MailboxCall(MBOX_CH_PROP) && mbox[5] == clock_id;
	uint32_t rate = ok ? mbox[6] : 0;
	AuReleaseSpinlockIrqRestore(mbox_lock, flags);
	return rate;
}

/**
 * @brief AuRPISetClockRate -- set a firmware clock
 * @param clock_id -- firmware clock id (2 UART, 4 CORE, ...)
 * @param rate -- requested rate in Hz
 * @return rate the firmware reports back, 0 on failure
 */
uint32_t AuRPISetClockRate(uint32_t clock_id, uint32_t rate) {
	if (!mbox)
		return 0;
	uint64_t flags = AuAcquireSpinlockIrqSave(mbox_lock);
	memset((void*)mbox, 0, PAGE_SIZE);
	mbox[0] = 9 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = 0x00038002; /* set clock rate */
	mbox[3] = 12;
	mbox[4] = 0;
	mbox[5] = clock_id;
	mbox[6] = rate;
	mbox[7] = 0; /* leave turbo alone */
	mbox[8] = TAG_LAST;
	bool ok = AuRPI3MailboxCall(MBOX_CH_PROP) && mbox[5] == clock_id;
	uint32_t got = ok ? mbox[6] : 0;
	AuReleaseSpinlockIrqRestore(mbox_lock, flags);
	return got;
}

/**
 * @brief AuRPISetExpGPIO -- drive one line of the firmware owned GPIO
 * expander. The line is configured as an output first, keeping the
 * polarity the firmware reports for it
 * @param gpio -- expander gpio number (128 + line)
 * @param level -- 0 or 1
 * @return 1 on success, 0 on failure
 */
int AuRPISetExpGPIO(uint32_t gpio, uint32_t level) {
	if (!mbox)
		return 0;
	uint64_t flags = AuAcquireSpinlockIrqSave(mbox_lock);

	/* get config: gpio, direction, polarity, term_en, term_pull_up */
	memset((void*)mbox, 0, PAGE_SIZE);
	mbox[0] = 11 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = 0x00030043;
	mbox[3] = 20;
	mbox[4] = 0;
	mbox[5] = gpio;
	mbox[10] = TAG_LAST;
	uint32_t polarity = 0;
	if (AuRPI3MailboxCall(MBOX_CH_PROP) && mbox[5] == 0)
		polarity = mbox[7];

	/* set config: gpio, direction (1 = out), polarity, term_en,
	 * term_pull_up, initial state */
	memset((void*)mbox, 0, PAGE_SIZE);
	mbox[0] = 12 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = 0x00038043;
	mbox[3] = 24;
	mbox[4] = 0;
	mbox[5] = gpio;
	mbox[6] = 1;
	mbox[7] = polarity;
	mbox[8] = 0;
	mbox[9] = 0;
	mbox[10] = level ? 1 : 0;
	mbox[11] = TAG_LAST;
	bool cfg_ok = AuRPI3MailboxCall(MBOX_CH_PROP) && mbox[5] == 0;

	/* set state: gpio, state */
	memset((void*)mbox, 0, PAGE_SIZE);
	mbox[0] = 8 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = 0x00038041;
	mbox[3] = 8;
	mbox[4] = 0;
	mbox[5] = gpio;
	mbox[6] = level ? 1 : 0;
	mbox[7] = TAG_LAST;
	bool ok = AuRPI3MailboxCall(MBOX_CH_PROP) && mbox[5] == 0;
	AuReleaseSpinlockIrqRestore(mbox_lock, flags);

	if (!cfg_ok)
		UARTDebugOut("[rpi3]: expander gpio %d config not confirmed \r\n", gpio);
	return ok ? 1 : 0;
}

/**
 * @brief AuRPIGetBoardSerial -- board serial number from the firmware
 * @return the 64-bit serial, 0 on failure
 */
uint64_t AuRPIGetBoardSerial() {
	if (!mbox)
		return 0;
	uint64_t flags = AuAcquireSpinlockIrqSave(mbox_lock);
	memset((void*)mbox, 0, PAGE_SIZE);
	mbox[0] = 8 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = 0x00010004; /* get board serial */
	mbox[3] = 8;
	mbox[4] = 0;
	mbox[5] = 0;
	mbox[6] = 0;
	mbox[7] = TAG_LAST;
	uint64_t serial = 0;
	if (AuRPI3MailboxCall(MBOX_CH_PROP))
		serial = ((uint64_t)mbox[6] << 32) | mbox[5];
	AuReleaseSpinlockIrqRestore(mbox_lock, flags);
	return serial;
}

/**
 * @brief AuRPISetPowerState -- power a device on through the mailbox
 * @param device_id -- device id to power on
 */
int AuRPISetPowerState(uint32_t device_id) {
	uint64_t flags = AuAcquireSpinlockIrqSave(mbox_lock);
	memset((void*)mbox, 0, PAGE_SIZE);
	mbox[0] = 9 * 4;
	mbox[1] = MBOX_REQUEST;
	mbox[2] = 0x00028001; /* set power state */
	mbox[3] = 8;
	mbox[4] = 8;
	mbox[5] = device_id;
	mbox[6] = 0x3; /* on, wait */
	mbox[7] = TAG_LAST;
	mbox[8] = 0;
	bool ok = AuRPI3MailboxCall(MBOX_CH_PROP);
	uint32_t state = mbox[6];
	AuReleaseSpinlockIrqRestore(mbox_lock, flags);

	if (!ok) {
		UARTDebugOut("[rpi3]: mailbox set-power-state failed \r\n");
		return 0;
	}
	if (!(state & 0x1)) {
		UARTDebugOut("[rpi3]: device %d didn't power on \r\n", device_id);
		return 0;
	}
	return 1;
}

/**
 * @brief AuRPI3Initialize -- initialize the basic board requirement
 */
void AuRPI3Initialize() {
	AuRPI3MapMMIO();

	mbox_lock = AuCreateSpinlock(false);
	mbox_phys = AuPmmngrAllocPage(AURORA_PAGE_KERNEL);
	if (mbox_phys == PMM_INVALID_PHYS) {
		AuTextOut("[rpi3]: no page for the mailbox \r\n");
		return;
	}
	mbox = (volatile uint32_t*)P2V(mbox_phys);

	uint32_t rev = AuRPI3GetBoardRevision();
	AuTextOut("[rpi3]: Raspberry Pi board revision %x \r\n", rev);

	/* LittleBoot normally sets the HDMI framebuffer up and passes it in
	 * the boot info like a GOP; only ask the firmware if it did not */
	KERNEL_BOOT_INFO* info = AuGetBootInfoStruc();
	if (!info->graphics_framebuffer)
		AuRPIInitializeFramebuffer(1024, 768, 32);

	/* built-in board drivers: GPIO, SPI0 and the XPT2046 touch screen on
	 * it. Each one disables itself if its hardware does not answer */
	AuRPI3BPGpioMap();
	if (AuRPISPIInitialize() == RPI_SPI_OK)
		AuRPIXPT2046Initialize();

	AuTextOut("[rpi3]: Raspberry Pi 3b+ board initialized \r\n");
}

/**
 * @brief AuRPI3PostSchedulerInitialize -- start the board's driver
 * threads; kernel threads only survive if created after
 * AuSchedulerInitialize
 */
void AuRPI3PostSchedulerInitialize() {
	AuRPIXPT2046Start();
	/* headphone audio is the external driver Drivers/Sound/RPI (rpisnd.dll,
	 * loaded from /board.cnf) */
}

#endif
