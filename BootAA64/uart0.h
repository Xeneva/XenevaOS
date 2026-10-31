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

#ifndef __UART0_H__
#define __UART0_H__

#include "xnldr.h"

#define UART_BASE  (0x3F000000 + 0x201000)
#define UART0_DR   ((volatile uint32_t*)(UART_BASE + 0x00))
#define UART0_FR   ((volatile uint32_t*)(UART_BASE + 0x18))
#define UART0_IBRD ((volatile uint32_t*)(UART_BASE + 0x24))
#define UART0_FBRD ((volatile uint32_t*)(UART_BASE + 0x28))
#define UART0_LCRH ((volatile uint32_t*)(UART_BASE + 0x2C))
#define UART0_CR   ((volatile uint32_t*)(UART_BASE + 0x30))
#define UART0_IMSC ((volatile uint32_t*)(UART_BASE + 0x38))
#define UART0_ICR  ((volatile uint32_t*)(UART_BASE + 0x44))

#define UARTFR (UART_BASE + 0x18)
#define UARTDR (UART_BASE + 0x00)

extern void XEUartInitialize();

#include <stdarg.h>

/*
 * XEUARTPrintV -- core formatted output to UART using va_list
 * @param format -- formated string
 * @param args -- va_list of arguments
 */
extern void XEUARTPrintV(const char* format, va_list args);

/*
 * XEUARTPrint -- print formated text using UART
 * @param format -- formated string
 */
extern void XEUARTPrint(const char* format, ...);

#endif