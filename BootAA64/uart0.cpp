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

#include "uart0.h"
#include <stdarg.h>
#include "rpi3b/mbox.h"
#include "clib.h"
#include <Board/imx8mp/imx8mp_uart.h>
#include "imx8mp/imx8mp_uartc.h"
#include "xnout.h"

static bool _is_uart_initialized;
/**
 * @brief XEUartInitialize -- initialize the
 * uart controller
 */
void XEUartInitialize() {
	_is_uart_initialized = 0;
#ifdef __TARGET_BOARD_RPI3__
	//RPI3BUartInit();
	_is_uart_initialized = 0;
#elif __TARGET_BOARD_QEMU_VIRT__
	_is_uart_initialized = 1;
#elif __TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__ || (__TARGET_BOARD_IMX8MP_SOC__)
	XE_iMX8MP_UART_Initialize(IMX8MP_UART3_BASE_ADDRESS);
	_is_uart_initialized = 1;
#endif
}

/*
 * LBUartPutc -- put a character to UART
 * @param c -- Character to put
 */
void XEUartPutc(char c) {
	if (_is_uart_initialized != 1)
		return;
#if defined(__TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__) || defined(__TARGET_BOARD_IMX8MP_SOC__)
	imx8mp_uart_putc(c);
#elif __TARGET_BOARD_QEMU_VIRT__
	volatile unsigned int* uart0 = (volatile unsigned int*)0x09000000;
	while (*(uart0 + 6) & (1 << 5))
		; // Wait until FR_TXFF is clear
	*uart0 = c;
#elif __TARGET_BOARD_RPI3__
	char* uart0 = (char*)UART_BASE;
	while ((*(uart0 + 0x18) & (1 << 5)))
		;
	*uart0 = c;
#endif
}

/*
 * LBUartPutString -- print a string
 * to UART
 * @param s -- String to print
 */
void XEUartPutString(const char* s) {
	while (*s)
		XEUartPutc(*s++);
}

#include <stdarg.h>

/*
 * XEUARTPrintV -- core formatted output to UART using va_list
 * @param format -- formated string
 * @param args -- va_list of arguments
 */
void XEUARTPrintV(const char* format, va_list args) {
#ifdef __TARGET_BOARD_RPI3__
	XEGuiPrint(format);
	return;
#endif

	while (*format != '\0') {
		if (*format == '%') {
			++format;
			if (*format == 'd') {
				size_t width = 0;
				if (format[1] == '.') {
					for (size_t i = 2; format[i] >= '0' && format[i] <= '9'; ++i) {
						width *= 10;
						width += format[i] - '0';
					}
				}
				size_t i = va_arg(args, size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				sztoa(i, buffer, 10);
				size_t len = strlen(buffer);
				while (len++ < width)
					XEUartPutString("0");
				XEUartPutString(buffer);
			} else if (*format == 'c') {
				int c = va_arg(args, int);
				XEUartPutc((char)c);
			} else if (*format == 'x') {
				size_t x = va_arg(args, size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				sztoa(x, buffer, 16);
				XEUartPutString(buffer);
			} else if (*format == 's') {
				char* x = va_arg(args, char*);
				XEUartPutString(x);
			} else if (*format == 'S') {
				char* x = va_arg(args, char*);
				XEUartPutString(x);
			} else if (*format == '%') {
				XEUartPutString(".");
			} else {
				char buf[3];
				buf[0] = '%';
				buf[1] = *format;
				buf[2] = '\0';
				XEUartPutString(buf);
			}
		} else {
			char buf[2];
			buf[0] = *format;
			buf[1] = '\0';
			XEUartPutString(buf);
		}
		++format;
	}
}

/*
 * XEUARTPrint -- print formated text using graphics
 * @param format -- formated string
 */
void XEUARTPrint(const char* format, ...) {
	va_list args;
	va_start(args, format);
	XEUARTPrintV(format, args);
	va_end(args);
}
