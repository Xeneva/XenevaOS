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

#ifndef __X86_64_PIC_H__
#define __X86_64_PIC_H__

#include <stdint.h>

#define ICW1_ICW4  0x01
#define ICW1_SINGLE  0x02
#define ICW1_INTERVAL4  0x04
#define ICW1_LEVEL  0x08
#define ICW1_INIT   0x10

#define ICW4_8086  0x01
#define ICW4_AUTO  0x02
#define ICW4_BUF_SLAVE 0x08
#define ICW4_SFNM   0x10

#define PIC1  0x20
#define PIC2  0xA0
#define PIC1_COMMAND  PIC1
#define PIC1_DATA     (PIC1+1)
#define PIC2_COMMAND  PIC2
#define PIC2_DATA  (PIC2+1)

#define PIC_EOI 0x20

void AuInitialisePIC();

void AuPICSetMask(uint8_t irq);
void AuPICClearMask(uint8_t irq);
void AuPITSleepMS(uint32_t ms);
void AuPICInterruptEOI(unsigned int intno);
void AuPITOneShotMode();
void AuPitOneShotWait();

#endif