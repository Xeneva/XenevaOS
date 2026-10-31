/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#ifndef __HAL_H__
#define __HAL_H__

#include <stdint.h>
#include <time.h>
#include <aurora.h>

#ifdef __cplusplus
AU_EXTERN{
#endif

/*
* AuHalInitialise -- initialise the
* hardware abstraction layer
* @param info -- kernel boot information structure
*/
extern void AuHalInitialise(KERNEL_BOOT_INFO* info);

/*
* AuHalPostInitialise -- post sybsystems
* initialisation process
*/
extern void AuHalPostInitialise();
/*
* Registers an IRQ to a vector
* @param vector -- vector number
* @param fn -- handler function pointer
* @param irq -- IRQ number
* @param level -- is this a level interrupt ?
*/
AU_EXPORT void AuHalRegisterIRQ(size_t vector, void(*fn)(size_t, void*), uint8_t irq, BOOL level);

/*
* AuHalMaskIRQ -- mask an irq
* @param irq -- IRQ number
* @param value -- if true, mask it else unmask it
*/
AU_EXPORT void AuHalMaskIRQ(uint8_t irq, BOOL value);

/*
* AuInterruptEnd -- sends end_of_interrupt to
* interrupt controller
* @param irq -- IRQ number __UNUSED__
*/
AU_EXPORT void AuInterruptEnd(uint8_t irq);

/*
* AuHalFlushCache -- flush the cache
*/
AU_EXPORT void AuHalFlushCache();

/*
* AuDisableInterrupt -- disable interrupts
*/
AU_EXPORT void AuDisableInterrupt();

/*
* AuEnableInterrupt -- enable interrupts
*/
AU_EXPORT void AuEnableInterrupt();

/*
* AuGetTimeOfTheDay -- return current time in unix
* format
* @param tv -- timeval structure
*/
AU_EXPORT int AuGetTimeOfTheDay(timeval *tv);

#ifdef __cplusplus
}
#endif

#endif