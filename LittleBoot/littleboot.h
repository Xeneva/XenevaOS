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

#ifndef __LITTLE_BOOT_H__
#define __LITTLE_BOOT_H__

/*for now, I only have QEMU */
#define RASPBERRY_PI 1

 typedef unsigned long long uint64_t;
 typedef unsigned long long uintptr_t;
 typedef unsigned char uint8_t;
 typedef unsigned short uint16_t;
 typedef unsigned int uint32_t;
 typedef uint64_t size_t;

 #define true 1
 #define false 0
 #define bool char

 #define NULL 0

#ifdef QEMU
#define UART_BASE 0x09000000
#elif RASPBERRY_PI
#define MMIO_BASE 0x3F000000
#define UART_BASE (0x3F000000 + 0x201000)
#define UART0_DR ((volatile uint32_t*)(UART_BASE + 0x00))
#define UART0_FR ((volatile uint32_t*)(UART_BASE + 0x18))
#define UART0_IBRD ((volatile uint32_t*)(UART_BASE + 0x24))
#define UART0_FBRD ((volatile uint32_t*)(UART_BASE + 0x28))
#define UART0_LCRH ((volatile uint32_t*)(UART_BASE + 0x2C))
#define UART0_CR  ((volatile uint32_t*)(UART_BASE + 0x30))
#define UART0_IMSC ((volatile uint32_t*)(UART_BASE + 0x38))
#define UART0_ICR ((volatile uint32_t*)(UART_BASE + 0x44))

/* The serial console is the mini UART (AUX) on GPIO14/15. The PL011
 * above belongs to the on-board Bluetooth controller (GPIO30-33), which
 * the kernel driver Drivers/Bt/RPI (rpibt.dll) programs; LittleBoot does
 * not touch it. Register layout: BCM2835 ARM Peripherals, chapter 2. */
#define AUX_BASE (MMIO_BASE + 0x215000)
#define AUX_ENABLES ((volatile uint32_t*)(AUX_BASE + 0x04))
#define AUX_MU_IO   ((volatile uint32_t*)(AUX_BASE + 0x40))
#define AUX_MU_IER  ((volatile uint32_t*)(AUX_BASE + 0x44))
#define AUX_MU_IIR  ((volatile uint32_t*)(AUX_BASE + 0x48))
#define AUX_MU_LCR  ((volatile uint32_t*)(AUX_BASE + 0x4C))
#define AUX_MU_MCR  ((volatile uint32_t*)(AUX_BASE + 0x50))
#define AUX_MU_LSR  ((volatile uint32_t*)(AUX_BASE + 0x54))
#define AUX_MU_CNTL ((volatile uint32_t*)(AUX_BASE + 0x60))
#define AUX_MU_BAUD ((volatile uint32_t*)(AUX_BASE + 0x68))
#define AUX_MU_LSR_TX_EMPTY (1 << 5)
#endif

#define UARTFR (UART_BASE + 0x18)
#define UARTDR (UART_BASE + 0x00)

typedef struct {
    uint32_t magic;
    uint32_t totalSize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
}fdt_header_t;

#define MAX_MEMORY_REGN 8

typedef struct {
    uint64_t base;
    uint64_t size;
    uint64_t page_count;
}memory_region_t;

#define RAW_OFFSET(type, x, offset) (type)((size_t)x + offset)

/*
 * LBUartPutc -- put a character to UART
 * @param c -- Character to put
 */
extern void LBUartPutc(char c);


/*
 * LBUartPutString -- print a string 
 * to UART
 * @param s -- String to print
 */
extern void LBUartPutString(const char* s);

/*
 * LBUartPrintHex -- prints hexadecimal value
 * using serial UART
 * @param val -- value to print
 */
extern void LBUartPrintHex(uint64_t val);

/*
 * LBUartPrintInt -- prints integer 
 * through UART
 * @param value -- value to print
 */
extern void LBUartPrintInt(uint64_t value);


/*
 * LBExceptionSetup -- install exception handlers
 */
extern void LBExceptionSetup();


#endif