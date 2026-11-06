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

#ifndef __IOAPIC_H__
#define __IOAPIC_H__

#include <stdint.h>

#define IOAPIC_REG_ID  0x00
#define IOAPIC_REG_VER 0x01
#define IOAPIC_REG_ARB 0x02
#define IOAPIC_REG_RED_TBL_BASE 0x10

/*
* IOAPICInitialise -- initialise ioapic
* @param address -- ioapic base address
*/
extern void IOAPICInitialise(void* address);

/*
* IOAPICMaskIRQ -- masks an irq
* @param irq -- Interrupt Request number
* @param value -- if true, then mask, if false unmask
*/
extern void IOAPICMaskIRQ(uint8_t irq, bool value);

/*
* IOAPICRegisterIRQ -- Maps an IOAPIC interrupt to a interrupt vector
* @param vector -- vector number
* @param fn -- function handler
* @param irq -- IRQ number
* @param level -- is this a level interrupt ?
*/
extern void IOAPICRegisterIRQ(size_t vector, void(*fn)(size_t, void*), uint8_t irq, bool level);

extern void IOAPICRedirect(uint8_t irq, uint32_t gsi, uint16_t flags, uint8_t apic);

#endif