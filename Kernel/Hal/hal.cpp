/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#include <Hal/hal.h>
#include <Hal/x86_64_hal.h>
#include <Hal/x86_64_lowlevel.h>
#include <Hal/ioapic.h>
#include <Hal/apic.h>
#include <Hal/x86_64_cpu.h>
#include <Hal/basicacpi.h>

/*
 * AuHalInitialise -- initialise the
 * hardware abstraction layer
 * @param info -- kernel boot information structure
 */
void AuHalInitialise(KERNEL_BOOT_INFO *info) {
#ifdef ARCH_X64
	x86_64_hal_initialise(info);
#endif
	//! else go for other platforms
}

/*
 * AuHalPostInitialise -- post sybsystems
 * initialisation process
 */
void AuHalPostInitialise() {
#ifdef ARCH_X64
	x86_64_cpu_initialize(AuGetCPUCount());
#endif
}

/*
 * Registers an IRQ to a vector
 * @param vector -- vector number
 * @param fn -- handler function pointer
 * @param irq -- IRQ number
 * @param level -- is this a level interrupt ?
 */
void AuHalRegisterIRQ(size_t vector, void(*fn)(size_t, void*), uint8_t irq, BOOL level) {
	IOAPICRegisterIRQ(vector, fn, irq, level);
}

/*
 * AuHalMaskIRQ -- mask an irq
 * @param irq -- IRQ number
 * @param value -- if true, mask it else unmask it
 */
void AuHalMaskIRQ(uint8_t irq, BOOL value) {
	IOAPICMaskIRQ(irq, value);
}

/*
 * AuInterruptEnd -- sends end_of_interrupt to 
 * interrupt controller
 * @param irq -- IRQ number __UNUSED__
 */
void AuInterruptEnd(uint8_t irq) {
	APICLocalEOI();
}

/*
* AuHalFlushCache -- flush the cache
*/
void AuHalFlushCache() {
	cache_flush();
}

/*
 * AuDisableInterrupt -- disable interrupts
 */
AU_EXTERN AU_EXPORT void AuDisableInterrupt() {
	x64_cli();
}

/*
 * AuEnableInterrupt -- enable interrupts
 */
AU_EXTERN AU_EXPORT void AuEnableInterrupt() {
	x64_sti();
}

/*
 * AuGetTimeOfTheDay -- return current time in unix 
 * format
 * @param tv -- timeval structure
 */
AU_EXTERN AU_EXPORT int AuGetTimeOfTheDay(timeval *tv) {
#ifdef ARCH_X64
	return x86_64_gettimeofday(tv);
#else
	return 0;
#endif
}