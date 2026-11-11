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

#ifndef __RASPBERRY_PI3B_H__
#define __RASPBERRY_PI3B_H__

#ifdef __TARGET_BOARD_RPI3__

#include <stdint.h>
#include <Hal/AA64/aa64cpu.h>
#include <aurora.h>


#define INT_SRC_CNTPSIRQ (1ULL<<0)
#define INT_SRC_CNTPNSIRQ (1ULL<<1)
#define INT_SRC_CNTHPIRQ (1ULL<<2)
#define INT_SRC_CNTVIRQ (1ULL<<3)
#define INT_SRC_MAILBOX0 (1ULL<<4)
#define INT_SRC_MAILBOX1 (1ULL<<5)
#define INT_SRC_MAILBOX2 (1ULL<<6)
#define INT_SRC_MAILBOX3 (1ULL<<7)
#define INT_SRC_GPU (1ULL<<8)
#define INT_SRC_PMU (1ULL<<9)
#define INT_SRC_AXI (1ULL<<10)

#define TIMER_IRQ_ENABLE (1ULL<<1)
#define TIMER_FIQ_ENABLE (1ULL<<3)

/* Mailbox Tags */
#define TAG_GET_BOARD_REV 0x00010002
#define TAG_ALLOCATE_BUFFER 0x00040001
#define TAG_RELEASE_BUFFER 0x00048001
#define TAG_BLANK_SCREEN 0x00040002
#define TAG_SET_PHYS_WH 0x00048003
#define TAG_SET_VIRT_WH 0x00048004
#define TAG_SET_DEPTH 0x00048005
#define TAG_SET_PIXEL_ORDER 0x00048006
#define TAG_GET_PITCH 0x00040008
#define TAG_SET_VIRT_OFFSET 0x00048009
#define TAG_SET_BACKLIGHT 0x0004800F
#define TAG_GET_TOUCHBUF 0x0004000F
#define TAG_SET_TOUCHBUF 0x0004801F
#define TAG_GET_DISPLAY_ID 0x00040013
#define TAG_SET_DSI_TIMING 00x00048014
#define TAG_LAST 0x00000000


enum rpi_power_domain{
	RPI_POWERUP_SDCARD,
	RPI_POWERUP_UART0,
	RPI_POWERUP_UART1,
	RPI_POWERUP_USB_HCD,
	RPI_POWERUP_I2C0,
	RPI_POWERUP_I2C1,
	RPI_POWERUP_I2C2,
	RPI_POWERUP_SPI,
	RPI_POWERUP_CCP2TX
};

/*
 * AuRPI3LocalIRQGetPending -- interrupt sources (INT_SRC_*) pending on
 * the calling core
 */
extern uint32_t AuRPI3LocalIRQGetPending();

/*
 * AuRPI3PeripheralIRQGetPending1/2 -- raw BCM2835 pending registers
 */
extern uint32_t AuRPI3PeripheralIRQGetPending2();
extern uint32_t AuRPI3PeripheralIRQGetPending1();

/*
 * AuRPI3PeripheralIRQPending -- pending and enabled peripheral irqs,
 * bit n = BCM2835 irq n
 */
extern uint64_t AuRPI3PeripheralIRQPending();

/*
 * AuRPI3Initialize -- initialize the basic board requirement
 */
extern void AuRPI3Initialize();

/*
 * AuRPI3PostSchedulerInitialize -- start board driver threads
 */
extern void AuRPI3PostSchedulerInitialize();

/* firmware clock ids (mailbox property interface) */
#define RPI_FW_CLOCK_UART 2
#define RPI_FW_CLOCK_CORE 4

/* firmware GPIO expander: line n is gpio 128 + n. On the 3B+ line 0 is
 * BT_ON (Bluetooth regulator enable) and line 1 is WL_ON */
#define RPI_EXP_GPIO_BASE  128
#define RPI_EXP_GPIO_BT_ON (RPI_EXP_GPIO_BASE + 0)

/*
 * AuRPIGetClockRate -- firmware clock rate in Hz (4 = CORE), 0 on failure
 */
AU_EXTERN AU_EXPORT uint32_t AuRPIGetClockRate(uint32_t clock_id);

/*
 * AuRPISetClockRate -- ask the firmware to run a clock at rate Hz,
 * returns the rate it actually set, 0 on failure
 */
AU_EXTERN AU_EXPORT uint32_t AuRPISetClockRate(uint32_t clock_id, uint32_t rate);

/*
 * AuRPISetExpGPIO -- drive a firmware expander gpio (128 + n) as an
 * output at the given level, returns 1 on success
 */
AU_EXTERN AU_EXPORT int AuRPISetExpGPIO(uint32_t gpio, uint32_t level);

/*
 * AuRPIGetBoardSerial -- 64-bit board serial number, 0 on failure
 */
AU_EXTERN AU_EXPORT uint64_t AuRPIGetBoardSerial();

/* AuRPI3ICInit -- initialize RPI3 local interrupt controller (boot core) */
extern void AuRPI3ICInit();

/*
 * AuRPI3LocalCoreInit -- reset the calling core's local interrupt state
 */
extern void AuRPI3LocalCoreInit();

/*
 * AuRPI3EnableLocalIRQ -- enable the calling core's timer (27) or IPI
 * mailbox (irq < 16) interrupt
 */
extern void AuRPI3EnableLocalIRQ(uint32_t irq);

/*
 * AuRPI3SendIPI -- set bit 'ipi' in core 'core's local mailbox 0
 */
extern void AuRPI3SendIPI(uint32_t core, uint32_t ipi);

/*
 * AuRPI3IPIReadClear -- read and acknowledge the calling core's IPIs
 */
extern uint32_t AuRPI3IPIReadClear();

/*
 * AuRPI3GetCoreID -- physical core number of the caller
 */
extern uint32_t AuRPI3GetCoreID();

/*
 * AuRPI3PeripheralIRQEnable -- enable particular peripheral irq
 * @param irq_num -- irq number of the specific peripheral
 */
AU_EXTERN AU_EXPORT void AuRPI3PeripheralIRQEnable(uint32_t irq_num);

/*
 * AuRPI3PeripheralIRQDisable -- disable particular peripheral irq
 */
extern void AuRPI3PeripheralIRQDisable(uint32_t irq_num);

extern void AuVC4DSIInit();

extern void AuRPI3DelayUS(uint32_t us);

extern void AuRPIDelayMS(uint32_t ms);

/*
 * AuRPIInitializeFramebuffer -- initializes framebuffer
 * @param width -- fb width by pixels
 * @param height -- fb height by pixels
 * @param depth -- fb depth
 */
bool AuRPIInitializeFramebuffer(uint32_t width, uint32_t height, uint32_t depth);

/**
 * @brief AuRPISetPowerState -- set the power state
 * of a device through mailbox interface
 * @param device_id -- device id to power on
 */
AU_EXTERN AU_EXPORT int AuRPISetPowerState(uint32_t device_id);

#endif


#endif