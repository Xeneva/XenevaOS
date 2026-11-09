/**
* @file uart.c
* 
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

#if defined(__GNUC__) || defined(__clang__)
#ifndef __cplusplus
#include <stdbool.h>
#endif
#endif
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64percpu.h>
#include <Mm/vmmngr.h>
#include <aucon.h>
#include <va_list.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <Board/imx8mp/imx8mp_uart.h>

uint64_t* uartMMIO;
bool _uart_mapped = false;

static inline uint32_t uart_read_reg(uint32_t reg_offset) {
	return *(volatile uint32_t*)((uint64_t)uartMMIO + reg_offset);
}

static inline void uart_write_reg(uint32_t reg_offset, uint32_t value) {
	*(volatile uint32_t*)((uint64_t)uartMMIO + reg_offset) = value;
}

/**
 * @brief UARTInitialize -- initialize uart serial output
 */
void UARTInitialize() {
	uartMMIO = (uint64_t*)AuMapMMIO(UART0_BASE, 1);
	AuTextOut("UART MMIO -> %x \n", uartMMIO);

#ifdef __TARGET_BOARD_QEMU_VIRT__
	uart_write_reg(UART_CR, 0);
	uart_write_reg(UART_IBRD, 13);
	uart_write_reg(UART_FBRD, 1);

	uart_write_reg(UART_LCR_H, UART_LCR_H_WLEN_8BIT | UART_LCR_H_FEN);

	uart_write_reg(UART_CR, UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);

	uart_write_reg(UART_ICR, 0xffff);

	//enable irq
	uart_write_reg(UART_IMSC, UART_IMSC_RXIM | UART_IMSC_RTIM);
#elif __TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__ || (__TARGET_BOARD_IMX8MP_SOC__)
	au_imx8np_uart_initialize((uint64_t*)uartMMIO);
#endif

	_uart_mapped = true;
}

bool is_uart_initialized() {
	return _uart_mapped;
}

/**
 * @brief uartPutc -- put a single character to uart
 * @param c -- character to print
 */
void uartPutc(char c) {
#if defined(__TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__) || defined(__TARGET_BOARD_IMX8MP_SOC__)
	au_imx8mp_uart_putc(c);
#else

	uint64_t* mmioBase = 0;
	if (_uart_mapped)
		mmioBase = uartMMIO;
	else
		mmioBase = (uint64_t*)UART0_BASE;
	volatile char* uart0 = (volatile char*)mmioBase;
	while ((*(uart0 + 0x18) & (1 << 5)))
		;
	*uart0 = c;
#endif
}

/**
 * @brief uartPuts --serial output interface
 * @param s -- String
 */
void uartPuts(const char* s) {
	while (*s)
		uartPutc(*s++);
}

extern void AuUartPutString(const char* s);
/**
 * @brief UARTDebugOut -- standard text printing function
 * for early kernel using UART
 * @param text -- text to output
 */
static void UARTDebugOut_CallUnlocked(const char* format, void* reg_save_area, void* entry_sp) {
#ifdef __GNUC__
	uint64_t* arg_ptr = (uint64_t*)((uint8_t*)reg_save_area + 8);
#define AU_VA_ARG(type) ((type)(*arg_ptr++))
#else
	va_list args = ((va_list)reg_save_area + 8);
#define AU_VA_ARG(type) va_arg(args, type)
#endif
	while (*format) {
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
				size_t i = AU_VA_ARG(size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				//	size_t len
				if ((int)i < 0) {
					uartPuts("-");
					i = ((int)i * -1);
					sztoa(i, buffer, 10);
				} else {
					sztoa(i, buffer, 10);
					size_t len = strlen(buffer);
				}
				/*	while (len++ < width)
				puts("0");*/
				uartPuts(buffer);
			} else if (*format == 'c') {
				int c = AU_VA_ARG(int);
				//char buffer[sizeof(size_t) * 8 + 1];
				//sztoa(c, buffer, 10);
				//puts(buffer);
				uartPutc(c);
			} else if (*format == 'x') {
				size_t x = AU_VA_ARG(size_t);
				char buffer[sizeof(size_t) * 8 + 1];
				sztoa(x, buffer, 16);
				//puts("0x");
				uartPuts(buffer);
			} else if (*format == 's') {
				char* x = AU_VA_ARG(char*);
				if (!x)
					return;
				uartPuts(x);
			} else if (*format == 'f') {
				double x = AU_VA_ARG(double);
				uartPuts(ftoa(x, 2));
			} else if (*format == '%') {
				uartPuts(".");
			} else {
				char buf[3];
				buf[0] = '%';
				buf[1] = *format;
				buf[2] = '\0';
				uartPuts(buf);
			}
		} else {
			char buf[2];
			buf[0] = *format;
			buf[1] = '\0';
			uartPuts(buf);
		}
		++format;
	}
#ifndef __GNUC__
	va_end(args);
#endif
}

/* UART print lock: one message at a time across cores. Recursive on the
 * owning core (a fault while printing still gets its report out) and IRQ
 * safe (interrupt handlers print too) */
extern void aa64_spinlock_acquire(uint32_t* lock);
extern void aa64_spinlock_release(uint32_t* lock);
static uint32_t s_uart_lock_word;
static volatile uint32_t s_uart_owner;
static uint32_t s_uart_depth;

void UARTDebugOut_Call(const char* format, void* reg_save_area, void* entry_sp) {
	uint64_t d = read_daif();
	mask_irqs();
	uint32_t me = AA64GetCPUNum() + 1;
	if (s_uart_owner == me) {
		s_uart_depth++;
	} else {
		aa64_spinlock_acquire(&s_uart_lock_word);
		s_uart_owner = me;
		s_uart_depth = 1;
	}
	UARTDebugOut_CallUnlocked(format, reg_save_area, entry_sp);
	if (--s_uart_depth == 0) {
		s_uart_owner = 0;
		aa64_spinlock_release(&s_uart_lock_word);
	}
	restore_daif(d);
}
