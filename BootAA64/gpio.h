/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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

#ifndef __GPIO_H__
#define __GPIO_H__

#define GPFSEL0	  ((volatile uint32_t*)(MMIO_BASE + 0x00200000))
#define GPFSEL1	  ((volatile uint32_t*)(MMIO_BASE + 0x00200004))
#define GPFSEL2	  ((volatile uint32_t*)(MMIO_BASE + 0x00200008))
#define GPFSEL3	  ((volatile uint32_t*)(MMIO_BASE + 0x0020000C))
#define GPFSEL4	  ((volatile uint32_t*)(MMIO_BASE + 0x00200010))
#define GPFSET0	  ((volatile uint32_t*)(MMIO_BASE + 0x0020001C))
#define GPFSET1	  ((volatile uint32_t*)(MMIO_BASE + 0x00200020))
#define GPFCLR0	  ((volatile uint32_t*)(MMIO_BASE + 0x00200028))
#define GPLEV0	  ((volatile uint32_t*)(MMIO_BASE + 0x00200034))
#define GPLEV1	  ((volatile uint32_t*)(MMIO_BASE + 0x00200038))
#define GPEDS0	  ((volatile uint32_t*)(MMIO_BASE + 0x00200040))
#define GPEDS1	  ((volatile uint32_t*)(MMIO_BASE + 0x00200044))
#define GPHEN0	  ((volatile uint32_t*)(MMIO_BASE + 0x00200064))
#define GPHEN1	  ((volatile uint32_t*)(MMIO_BASE + 0x00200068))
#define GPPUD	  ((volatile uint32_t*)(MMIO_BASE + 0x00200094))
#define GPPUDCLK0 ((volatile uint32_t*)(MMIO_BASE + 0x00200098))
#define GPPUDCLK1 ((volatile uint32_t*)(MMIO_BASE + 0x0020009C))

#endif