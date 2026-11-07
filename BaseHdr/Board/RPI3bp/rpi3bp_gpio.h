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

#ifndef __RPI3BP_GPIO_H__
#define __RPI3BP_GPIO_H__

#include <stdint.h>
#include <aurora.h>

/*
 * AuRPI3BPGpioMap -- map the GPIO base to kernel
 * highrr half address
 */
extern void AuRPI3BPGpioMap();


/*
 * AuRPIGPIOSetFunction -- assign function to
 * given pin
 * @param pin -- pin number
 * @param function -- function type
 */
extern void AuRPIGPIOSetFunction(uint8_t pin, uint8_t function);

/*
 * AuRPIGPIOSet -- set a specific pin
 * @param pin -- Pin number
 */
extern void AuRPIGPIOSet(uint8_t pin);

/*
 * AuRPIGPIOClear -- clear a pin
 * @param pin -- Pin number to clear
 */
extern void AuRPIGPIOClear(uint8_t pin);

extern void AuRPIGPIOPullUpsDown();

extern void AuRPIGPIOPullUP(uint8_t pin);

extern void AuRPIGPIOEnableInterrupt(uint8_t pin);

extern bool AuRPIGPIOCheckInterrupt(uint8_t pin);

extern uint32_t AuRPIGPIOGetEvents();

extern void AuRPIGPIOClearEvent(uint8_t pin);

extern bool AuRPIGPIOPinLevelLow(uint8_t pin);


extern void AuRPIGPIODisableInterrupt(uint8_t pin);

/*
 * AuRPIGPIOWrite -- write boolean value
 * to specific pin
 * @param pin -- Pin number
 * @param value -- value to write
 */
extern void AuRPIGPIOWrite(uint8_t pin, bool value);


#endif
