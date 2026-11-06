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

#ifndef __RTC_MMIO_H__
#define __RTC_MMIO_H__

#include <aa64hcode.h>

#define RTC_BASE RTC

#define RTCDR  (RTC_BASE + 0x00)
#define RTCMR  (RTC_BASE + 0x04)
#define RTCLR  (RTC_BASE + 0x08)
#define RTCCR  (RTC_BASE + 0x0C)
#define RTCIMSC (RTC_BASE + 0x10)
#define RTCRIS  (RTC_BASE + 0x14)
#define RTCMIS  (RTC_BASE + 0x18)
#define RTCICR  (RTC_BASE + 0x1C)

/*
 * AuPL031RTCIRQHandler -- irq handler of PL031 RTC
 */
extern void AuPL031RTCIRQHandle();


/*
 * AuPL031RTCInit -- initialize pl031 rtc
 */
extern void AuPL031RTCInit();
#endif