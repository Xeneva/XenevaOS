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

#ifndef __UART_H__
#define __UART_H__

#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif
#include <aa64hcode.h>
#include <stdint.h>
#include <aurora.h>
#include <Board/imx8mp/imx8mp.h>

#define UART_DR  0x00
#define UART_FR  0x18
#define UART_IBRD 0x24
#define UART_FBRD  0x28
#define UART_LCR_H 0x2C
#define UART_CR  0x30
#define UART_IMSC 0x38
#define UART_ICR  0x44

#define UART_FR_TXFF  (1<<5)
#define UART_FR_RXFE  (1<<4)

#define UART_CR_UARTEN (1<<0)
#define UART_CR_TXE  (1<<8)
#define UART_CR_RXE  (1<<9)


#define UART_LCR_H_WLEN_8BIT  (0x3 << 5)
#define UART_LCR_H_FEN  (1<<4)

#define UART_IMSC_RXIM (1<<4)
#define UART_IMSC_RTIM  (1 << 6)

#define UART0_BASE  UART0


extern void UARTInitialize();


/*
 * uartPutc --serial output interface
 * @param s -- String
 */
extern void uartPutc(char c);

/*
 * uartPuts --serial output interface
 * @param s -- String
 */
extern void uartPuts(const char* s);

extern bool is_uart_initialized();

/*
 * UARTDebugOut -- standard text printing function
 * for early kernel using UART
 * @param text -- text to output
 */
AU_EXTERN AU_EXPORT void UARTDebugOut(const char* format, ...);
#endif