/**
* @file rpi3bpGPIO.c
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
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <aucon.h>

#ifdef __TARGET_BOARD_RPI3__

static uint64_t* gpioBase;

#define GPFSEL0 0x00
#define GPFSEL1 0x04
#define GPFSEL2 0x08
#define GPSET0	0x1C
#define GPCLR0	0x28
#define GPFEN0	0x58
#define GPEDS0	0x40

/**
 * @brief AuRPI3BPGpioMap -- map the GPIO base to kernel
 * highrr half address
 */
void AuRPI3BPGpioMap() {
	gpioBase = (uint64_t*)AuMapMMIO(GPIO, 2);
	AuTextOut("[aurora]: RPI3 GPIO mapped to %x address \r\n", gpioBase);

	//enable GPIO_IRQ_0
	AuRPI3PeripheralIRQEnable(49);
}

/**
 * @brief AuRPIGPIOSetFunction -- assign function to 
 * given pin
 * @param pin -- pin number 
 * @param function -- function type
 */
void AuRPIGPIOSetFunction(uint8_t pin, uint8_t function) {
	uint32_t reg = pin / 10;
	uint32_t shift = (pin % 10) * 3;
	uint64_t addr = (uint64_t)gpioBase + GPFSEL0 + (reg * 4);
	uint32_t val = *(volatile uint32_t*)addr;
	val &= ~(7 << shift);
	val |= (function << shift);
	*(volatile uint32_t*)addr = val;
	dsb_ish();
	isb_flush();
}

/**
 * @brief AuRPIGPIOScanAll -- Debug purpose function
 */
void AuRPIGPIOScanAll() {
	while (1) {
		uint32_t lev0 = *(volatile uint32_t*)(gpioBase + 0x34);
		uint32_t lev1 = *(volatile uint32_t*)(gpioBase + 0x38);

		for (int i = 0; i < 32; i++) {
			if (!(lev0 & (1 << i)))
				AuTextOut("GPIO %d LOW \r\n");
		}

		for (int i = 32; i < 54; i++) {
			if (!(lev1 & (1 << (i - 32))))
				AuTextOut("GPIO %d LOW \r\n");
		}
	}
}

/**
 * @brief AuRPIGPIOPullUpsDown --  dummy func
 */
void AuRPIGPIOPullUpsDown() {
	(*(volatile uint32_t*)((uint64_t)gpioBase + 0x94)) = 0;
	for (int i = 0; i < 15000; i++)
		;
	(*(volatile uint32_t*)((uint64_t)gpioBase + 0x98)) =
		(1 << 8) | (1 << 9) | (1 << 10) | (1 << 11);

	for (int i = 0; i < 15000; i++)
		;
	(*(volatile uint32_t*)((uint64_t)gpioBase + 0x98)) = 0;
}

/**
 * @brief AuRPIGPIOPullUP -- perform a pull up for a specific pin
 * @param pin -- Pin number
 */
void AuRPIGPIOPullUP(uint8_t pin) {
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x94) = 2;
	dsb_sy_barrier();
	isb_flush();
	for (int i = 0; i < 150; i++)
		;
	uint32_t reg = pin / 32;
	uint32_t shift = pin % 32;
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x98 + (reg * 4)) = (1 << shift);

	dsb_sy_barrier();
	isb_flush();

	for (int i = 0; i < 150; i++)
		;
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x94) = 0;
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x98 + (reg * 4)) = 0;

	dsb_sy_barrier();
	isb_flush();
}

/**
 * @brief AuRPIGPIOEnableInterrupt -- enable gpio interrupt for
 * specific pin
 * @param pin -- pin number
 */
void AuRPIGPIOEnableInterrupt(uint8_t pin) {
	uint32_t reg = pin / 32;
	uint32_t shift = pin % 32;

	*(volatile uint32_t*)((uint64_t)gpioBase + GPEDS0 + (reg * 4)) = (1 << shift);
	*(volatile uint32_t*)((uint64_t)gpioBase + GPFEN0 + (reg * 4)) |= (1 << shift);

	dsb_sy_barrier();
	isb_flush();
}

/**
 * @brief AuRPIGPIODisableInterrupt -- disable gpio interrupt for
 * specific pin
 * @param pin -- pin number
 */
void AuRPIGPIODisableInterrupt(uint8_t pin) {
	uint32_t reg = pin / 32;
	uint32_t shift = pin % 32;

	*(volatile uint32_t*)((uint64_t)gpioBase + 0x4C + (reg * 4)) &= ~(1 << shift);
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x58 + (reg * 4)) &= ~(1 << shift);
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x64 + (reg * 4)) &= ~(1 << shift);
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x70 + (reg * 4)) &= ~(1 << shift);
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x40 + (reg * 4)) &= (1 << shift);
	dsb_sy_barrier();
	isb_flush();
}

/**
 * @brief AuRPIGPIOCheckInterrpt -- check pending interrupt for
 * specific pin
 * @param pin -- pin number
 */
bool AuRPIGPIOCheckInterrupt(uint8_t pin) {
	uint32_t reg = pin / 32;
	uint32_t shift = pin % 32;

	uint32_t status = *(volatile uint32_t*)((uint64_t)gpioBase + GPEDS0 + (reg * 4));

	if (status & (1 << shift)) {
		*(volatile uint32_t*)((uint64_t)gpioBase + GPEDS0 + (reg * 4)) = (1 << shift);
		return true;
	}
	return false;
}

/**
 * @brief AuRPIGPIOPinLevelLow -- check if pin level is low
 * @param pin -- Pin number
 */
bool AuRPIGPIOPinLevelLow(uint8_t pin) {
	volatile uint32_t* gplev0 = (volatile uint32_t*)((uint64_t)gpioBase + 0x34);
	if ((*gplev0 & (1 << pin)) == 0)
		return true;
	else
		return false;
}
/**
 * @brief AuRPIGPIOSet -- set a specific pin
 * @param pin -- Pin number
 */
void AuRPIGPIOSet(uint8_t pin) {
	*(volatile uint32_t*)((uint64_t)gpioBase + GPSET0) = (1 << pin);
	dsb_ish();
	isb_flush();
}

/**
 * @brief AuRPIGPIOClear -- clear a pin
 * @param pin -- Pin number to clear
 */
void AuRPIGPIOClear(uint8_t pin) {
	*(volatile uint32_t*)((uint64_t)gpioBase + GPCLR0) = (1 << pin);
	dsb_ish();
	isb_flush();
}

/**
 * @brief AuRPIGPIOGetEvents -- reads event status 
 */
uint32_t AuRPIGPIOGetEvents() {
	return *(volatile uint32_t*)((uint64_t)gpioBase + 0x40);
}

/**
 * @brief AuRPIGPIOClearEvent -- clears event status of given pin
 * @param pin -- Pin number
 */
void AuRPIGPIOClearEvent(uint8_t pin) {
	*(volatile uint32_t*)((uint64_t)gpioBase + 0x40) = (1 << pin);
	dsb_sy_barrier();
	isb_flush();
}

/**
 * @brief AuRPIGPIOWrite -- write boolean value
 * to specific pin
 * @param pin -- Pin number
 * @param value -- value to write
 */
void AuRPIGPIOWrite(uint8_t pin, bool value) {
	if (value)
		AuRPIGPIOSet(pin);
	else
		AuRPIGPIOClear(pin);
}

#endif
