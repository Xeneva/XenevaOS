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
#ifndef __RPI3BP_GPIO_H__
#define __RPI3BP_GPIO_H__

#include <stdint.h>
#include <stdbool.h>
#include <aurora.h>

/* BCM2837 GPIO, 54 pins in two banks */
#define RPI_GPIO_PIN_COUNT 54

/* GPFSEL function codes */
#define RPI_GPIO_FUNC_INPUT	 0
#define RPI_GPIO_FUNC_OUTPUT 1
#define RPI_GPIO_FUNC_ALT0	 4
#define RPI_GPIO_FUNC_ALT1	 5
#define RPI_GPIO_FUNC_ALT2	 6
#define RPI_GPIO_FUNC_ALT3	 7
#define RPI_GPIO_FUNC_ALT4	 3
#define RPI_GPIO_FUNC_ALT5	 2

/* GPPUD pull modes */
#define RPI_GPIO_PULL_NONE 0
#define RPI_GPIO_PULL_DOWN 1
#define RPI_GPIO_PULL_UP   2

/* both GPIO banks raise BCM2835 peripheral irq 49 (gpio_int[0]) for bank 0
 * and 50 for bank 1; kernel interrupt ids are 32 + n on the Pi 3 */
#define RPI_GPIO_BANK0_IRQ 49
#define RPI_GPIO_BANK1_IRQ 50

/*
 * AuRPI3BPGpioMap -- map the GPIO block, safe to call twice
 */
AU_EXTERN AU_EXPORT void AuRPI3BPGpioMap();

AU_EXTERN AU_EXPORT void AuRPIGPIOSetFunction(uint8_t pin, uint8_t function);

AU_EXTERN AU_EXPORT void AuRPIGPIOSet(uint8_t pin);

AU_EXTERN AU_EXPORT void AuRPIGPIOClear(uint8_t pin);

AU_EXTERN AU_EXPORT void AuRPIGPIOWrite(uint8_t pin, bool value);

/*
 * AuRPIGPIOSetPull -- RPI_GPIO_PULL_NONE / _DOWN / _UP
 */
AU_EXTERN AU_EXPORT void AuRPIGPIOSetPull(uint8_t pin, uint8_t mode);

/* kept for older callers: AuRPIGPIOSetPull(pin, RPI_GPIO_PULL_UP) */
AU_EXTERN AU_EXPORT void AuRPIGPIOPullUP(uint8_t pin);

/*
 * AuRPIGPIOPinLevelLow -- true if the pin reads low
 */
AU_EXTERN AU_EXPORT bool AuRPIGPIOPinLevelLow(uint8_t pin);

/*
 * AuRPIGPIOEnableInterrupt -- clear the pin's event, then detect falling
 * edges on it
 */
AU_EXTERN AU_EXPORT void AuRPIGPIOEnableInterrupt(uint8_t pin);

/*
 * AuRPIGPIODisableInterrupt -- stop every event detect on the pin and
 * clear its event
 */
AU_EXTERN AU_EXPORT void AuRPIGPIODisableInterrupt(uint8_t pin);

/*
 * AuRPIGPIOCheckInterrupt -- true (and the event cleared) if the pin has
 * a detected event
 */
AU_EXTERN AU_EXPORT bool AuRPIGPIOCheckInterrupt(uint8_t pin);

/*
 * AuRPIGPIOGetEvents -- bank 0 event status (GPEDS0)
 */
AU_EXTERN AU_EXPORT uint32_t AuRPIGPIOGetEvents();

AU_EXTERN AU_EXPORT void AuRPIGPIOClearEvent(uint8_t pin);

#endif
