/**
* @file imx8mp_uart.c
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

#if defined(__TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__) || defined(__TARGET_BOARD_IMX8MP_SOC__)

#include <Board/imx8mp/imx8mp_uart.h>
#include <stdint.h>

#define UART2_BAUD_RATE	  115200
#define UART2_CLOCK_VAL	  80000000
#define UART2_DATA_BITS	  8
#define UART2_STOP_BITS	  1
#define UART2_PARITY_NONE 0

static uint64_t __imx8mp_uart_base;
static inline uint32_t imx8mp_uart_read(uint64_t base) {
	return *((volatile uint32_t*)base);
}

static inline void imx8mp_uart_write(uint64_t base, uint32_t value) {
	*((volatile uint32_t*)base) = value;
}

static inline void imx8mp_uart_set(uint64_t base, uint32_t mask) {
	imx8mp_uart_write(base, imx8mp_uart_read(base) | mask);
}

static inline void imx8mp_uart_clr(uint64_t base, uint32_t mask) {
	imx8mp_uart_write(base, imx8mp_uart_read(base) & ~mask);
}

static void imx8mp_delay(volatile uint32_t n) {
	while (n--)
		;
}

void au_imx8np_uart_initialize(uint64_t base) {
	__imx8mp_uart_base = base;
}

/**
 * @brief imx8mp_uart_putc -- put a single character on the UART
 * @param c -- character to put
 */
void au_imx8mp_uart_putc(char c) {
	while (!(imx8mp_uart_read(IMX8MP_UART_USR1(__imx8mp_uart_base)) & USR1_TRDY))
		;
	//	imx8mp_delay(100);
	imx8mp_uart_write(IMX8MP_UART_UTXD(__imx8mp_uart_base), (uint32_t)(uint8_t)c);
}

/**
 * @brief imx8mp_uart_puts -- put a collection of character on the UART
 * @param s -- String to send
 */
void au_imx8mp_uart_puts(const char* s) {
	while (*s) {
		if (*s == '\n')
			au_imx8mp_uart_putc('\r');
		au_imx8mp_uart_putc(*s++);
	}
}

void au_imx8mp_uart_clear_errors() {
	uint32_t usr2 = imx8mp_uart_read(IMX8MP_UART_USR2(__imx8mp_uart_base));
	imx8mp_uart_write(IMX8MP_UART_USR2(__imx8mp_uart_base), usr2 | USR2_ORE | USR2_BRCD);
}

/**
 * @brief imx8mp_uart_getc -- receive a single character from
 * the UART
 */
int au_imx8mp_uart_getc() {
	uint32_t data;
	while (!(imx8mp_uart_read(IMX8MP_UART_USR2(__imx8mp_uart_base) & USR2_RDR)))
		;

	data = imx8mp_uart_read(IMX8MP_UART_URXD(__imx8mp_uart_base));

	if (data & URXD_ERR) {
		au_imx8mp_uart_clear_errors();
		return -1;
	}

	return (int)(data & URXD_RX_DATA);
}

#endif