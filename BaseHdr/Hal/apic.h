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

#ifndef __APIC_H__
#define __APIC_H__

#include <stdint.h>
#include <aurora.h>

#define IA32_APIC_BASE_MSR  0x1B
#define IA32_APIC_BASE_MSR_BSP  0x100
#define IA32_APIC_BASE_MSR_X2APIC 0x400
#define IA32_APIC_BASE_MSR_ENABLE 0x800

#define IA32_X2APIC_REGISTER_BASE_MSR 0x800
#define IA32_APIC_SVR_ENABLE 0x100

#define IA32_APIC_LVT_MAST  0x10000

#define LAPIC_SPURIOUS   0x00f0
#define LAPIC_LVT_TIMER  0x0320
#define LAPIC_LVT_LINT0  0x0350
#define LAPIC_LVT_LINT1  0x0360

#define LAPIC_TIMER_INIT  0x0380
#define LAPIC_TIMER_CUR   0x0390
#define LAPIC_TIMER_DIV   0x03e0

#define LAPIC_REGISTER_ID  0x2
#define LAPIC_REGISTER_EOI 0xB
#define LAPIC_REGISTER_SVR 0xF
#define LAPIC_REGISTER_ICR  0x30
#define LAPIC_REGISTER_LVT_TIMER  0x32
#define LAPIC_REGISTER_LVT_ERROR  0x37

#define LAPIC_REGISTER_TMRINITCNT  0x38
#define LAPIC_REGISTER_TMRCURRCNT  0x39
#define LAPIC_REGISTER_TMRDIV      0x3E

/*
 * ReadAPICRegister -- reads a register of apic
 * @param reg -- register to read
 */
extern uint64_t ReadAPICRegister(uint16_t reg);

/*
 * WriteAPICRegister -- writes a value to a register
 * of APIC
 * @param reg -- register to select
 * @param value -- value to write
 */
extern void WriteAPICRegister(uint16_t reg, uint64_t value);
/*
* AuAPICInitialise -- initialise the APIC
* @param bsp -- is this is initialised from bsp
* processor ?
*/
extern void AuAPICInitialise(bool bsp);

/*
* APICLocalEOI -- sends end_of_interrupt message
* to APIC
*/
extern void APICLocalEOI();

AU_EXTERN AU_EXPORT void APICTimerSleep(uint32_t ms);

/*
 * X2APICSupported -- is x2apic supported ?
 */
extern bool X2APICSupported();

/*
 *AuAPICGetInterruptPeriod -- returns the current interrupt
 * period timing in microsecond (us)
 */
extern uint64_t AuAPICGetInterruptPeriod();

#endif