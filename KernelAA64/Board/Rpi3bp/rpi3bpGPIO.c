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
#include <Hal/AA64/rpi3.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Board/RPI3bp/rpi3bp_gpio.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/vmmngr.h>
#include <aucon.h>

#ifdef __TARGET_BOARD_RPI3__

/*
 * BCM2837 GPIO controller. Register map from the BCM2835 ARM Peripherals
 * manual, section 6; the pull-up/down sequence (GPPUD, wait, GPPUDCLK,
 * wait, release) and its 150-cycle timing are from the same manual
 * (GPPUDCLK description).
 */

#define GPFSEL0	  0x00
#define GPSET0	  0x1C
#define GPCLR0	  0x28
#define GPLEV0	  0x34
#define GPEDS0	  0x40
#define GPREN0	  0x4C
#define GPFEN0	  0x58
#define GPHEN0	  0x64
#define GPLEN0	  0x70
#define GPAREN0	  0x7C
#define GPAFEN0	  0x88
#define GPPUD	  0x94
#define GPPUDCLK0 0x98

static volatile uint8_t* gpio_base;

static inline uint32_t gpio_read(uint32_t off) {
	return *(volatile uint32_t*)(gpio_base + off);
}

static inline void gpio_write(uint32_t off, uint32_t val) {
	*(volatile uint32_t*)(gpio_base + off) = val;
}

/* bank register (n = pin / 32) and bit for a pin */
#define GPIO_BANK(pin) (((uint32_t)(pin) / 32) * 4)
#define GPIO_BIT(pin)  (1u << ((pin) % 32))

/**
 * @brief AuRPI3BPGpioMap -- map the GPIO block
 */
void AuRPI3BPGpioMap() {
	if (gpio_base)
		return;
	gpio_base = (volatile uint8_t*)AuMapMMIO(GPIO, 1);
	AuTextOut("[gpio]: mapped to %x \r\n", gpio_base);
}

/**
 * @brief AuRPIGPIOSetFunction -- select a pin's function (GPFSELn)
 */
void AuRPIGPIOSetFunction(uint8_t pin, uint8_t function) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return;
	uint32_t off = GPFSEL0 + (pin / 10) * 4;
	uint32_t shift = (pin % 10) * 3;
	uint32_t val = gpio_read(off);
	val &= ~(7u << shift);
	val |= ((uint32_t)(function & 7) << shift);
	gpio_write(off, val);
	dsb_sy_barrier();
}

void AuRPIGPIOSet(uint8_t pin) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return;
	gpio_write(GPSET0 + GPIO_BANK(pin), GPIO_BIT(pin));
	dsb_sy_barrier();
}

void AuRPIGPIOClear(uint8_t pin) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return;
	gpio_write(GPCLR0 + GPIO_BANK(pin), GPIO_BIT(pin));
	dsb_sy_barrier();
}

void AuRPIGPIOWrite(uint8_t pin, bool value) {
	if (value)
		AuRPIGPIOSet(pin);
	else
		AuRPIGPIOClear(pin);
}

/**
 * @brief AuRPIGPIOSetPull -- program the pin's pull resistor. The control
 * signal has to be held for 150 cycles around the clock pulse; 1 us
 * covers that at any core clock
 */
void AuRPIGPIOSetPull(uint8_t pin, uint8_t mode) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return;
	gpio_write(GPPUD, mode & 3);
	dsb_sy_barrier();
	AuRPI3DelayUS(1);
	gpio_write(GPPUDCLK0 + GPIO_BANK(pin), GPIO_BIT(pin));
	dsb_sy_barrier();
	AuRPI3DelayUS(1);
	gpio_write(GPPUD, 0);
	gpio_write(GPPUDCLK0 + GPIO_BANK(pin), 0);
	dsb_sy_barrier();
}

void AuRPIGPIOPullUP(uint8_t pin) {
	AuRPIGPIOSetPull(pin, RPI_GPIO_PULL_UP);
}

/**
 * @brief AuRPIGPIOPinLevelLow -- true if the pin reads low (GPLEVn)
 */
bool AuRPIGPIOPinLevelLow(uint8_t pin) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return false;
	return (gpio_read(GPLEV0 + GPIO_BANK(pin)) & GPIO_BIT(pin)) == 0;
}

/**
 * @brief AuRPIGPIOEnableInterrupt -- falling edge detect on a pin
 */
void AuRPIGPIOEnableInterrupt(uint8_t pin) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return;
	uint32_t bank = GPIO_BANK(pin), bit = GPIO_BIT(pin);
	/* GPEDS is write-1-to-clear: drop a stale event first */
	gpio_write(GPEDS0 + bank, bit);
	gpio_write(GPFEN0 + bank, gpio_read(GPFEN0 + bank) | bit);
	dsb_sy_barrier();
}

/**
 * @brief AuRPIGPIODisableInterrupt -- no event detect at all on a pin
 */
void AuRPIGPIODisableInterrupt(uint8_t pin) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return;
	uint32_t bank = GPIO_BANK(pin), bit = GPIO_BIT(pin);
	gpio_write(GPREN0 + bank, gpio_read(GPREN0 + bank) & ~bit);
	gpio_write(GPFEN0 + bank, gpio_read(GPFEN0 + bank) & ~bit);
	gpio_write(GPHEN0 + bank, gpio_read(GPHEN0 + bank) & ~bit);
	gpio_write(GPLEN0 + bank, gpio_read(GPLEN0 + bank) & ~bit);
	gpio_write(GPAREN0 + bank, gpio_read(GPAREN0 + bank) & ~bit);
	gpio_write(GPAFEN0 + bank, gpio_read(GPAFEN0 + bank) & ~bit);
	gpio_write(GPEDS0 + bank, bit);
	dsb_sy_barrier();
}

/**
 * @brief AuRPIGPIOCheckInterrupt -- test and clear a pin's event
 */
bool AuRPIGPIOCheckInterrupt(uint8_t pin) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return false;
	uint32_t bank = GPIO_BANK(pin), bit = GPIO_BIT(pin);
	if (gpio_read(GPEDS0 + bank) & bit) {
		gpio_write(GPEDS0 + bank, bit);
		dsb_sy_barrier();
		return true;
	}
	return false;
}

uint32_t AuRPIGPIOGetEvents() {
	if (!gpio_base)
		return 0;
	return gpio_read(GPEDS0);
}

void AuRPIGPIOClearEvent(uint8_t pin) {
	if (!gpio_base || pin >= RPI_GPIO_PIN_COUNT)
		return;
	gpio_write(GPEDS0 + GPIO_BANK(pin), GPIO_BIT(pin));
	dsb_sy_barrier();
}

#endif
