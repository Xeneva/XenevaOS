/**
* @file imx8mp_uart.h
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

#ifndef __IMX8MP_UART_H__
#define __IMX8MP_UART_H__


#include <stdint.h>
/**
 * UART Register Map for NXP.IMX8MP SoC family, can be found in
 * iMX8MPRM -- Processor Reference Manual
 */

#define IMX8MP_UART1_BASE_ADDRESS 0x30860000
#define IMX8MP_UART3_BASE_ADDRESS 0x30880000
#define IMX8MP_UART2_BASE_ADDRESS 0x30890000


#define IMX8MP_UART_URXD(base)   base  /* UART Receiver Register (URDX) -- 32 bit width*/
#define IMX8MP_UART_UTXD(base)   (base + 0x40) /** UART Transmitter Register (UTXD) -- 32 bit width */
#define IMX8MP_UART_UCR1(base)   (base + 0x80) /** UART1 Control register */
#define IMX8MP_UART_UCR2(base)   (base + 0x84)
#define IMX8MP_UART_UCR3(base)   (base + 0x88)
#define IMX8MP_UART_UCR4(base)   (base + 0x8C)
#define IMX8MP_UART_UFCR(base)   (base + 0x90) /** UART1 FIFO Control Register **/
#define IMX8MP_UART_USR1(base)   (base + 0x94) /** UART1 Status Regster 1 **/
#define IMX8MP_UART_USR2(base)   (base + 0x98) /** UART1  Status Register 2 **/
#define IMX8MP_UART_UESC(base)   (base + 0x9C) /** UART1 Escape Character Register **/
#define IMX8MP_UART_UTIM(base)   (base + 0xA0) /** UART1 Escape Timer Register **/
#define IMX8MP_UART_UBIR(base)   (base + 0xA4) /** UART1 BRM Incremental Register **/
#define IMX8MP_UART_UBMR(base)   (base + 0xA8) /** UART1 BRM Modulator Register **/
#define IMX8MP_UART_UBRC(base)   (base + 0xAC) /** UART1 Baude Rate Count Register **/
#define IMX8MP_UART_ONEMS(base)  (base + 0xB0) /** UART1 One Millisecond Register **/
#define IMX8MP_UART_UTS(base)    (base + 0xB4) /** UART1 Test Register **/
#define IMX8MP_UART_UMCR(base)   (base + 0xB8) /** UART1 RS-485 Mode Control Register **/

 /* ── UCR1 bits ────────────────────────────────────────────────────────── */
#define UCR1_UARTEN     (1u << 0)   /* UART enable                        */
#define UCR1_DOZE       (1u << 1)   /* Doze mode enable                   */
#define UCR1_ATDMAEN    (1u << 2)   /* Aging DMA timer enable             */
#define UCR1_TXDMAEN    (1u << 3)   /* Transmitter DMA enable             */
#define UCR1_SNDBRK     (1u << 4)   /* Send BREAK                         */
#define UCR1_RTSDEN     (1u << 5)   /* RTS delta interrupt enable         */
#define UCR1_TXMPTYEN   (1u << 6)   /* Tx FIFO empty interrupt enable     */
#define UCR1_IREN       (1u << 7)   /* IrDA enable                        */
#define UCR1_RXDMAEN    (1u << 8)   /* Receiver DMA enable                */
#define UCR1_RRDYEN     (1u << 9)   /* Receiver ready interrupt enable    */
#define UCR1_ICD_MASK   (3u << 10)  /* Idle condition detect mask         */
#define UCR1_IDEN       (1u << 12)  /* Idle condition detected IRQ enable */
#define UCR1_TRDYEN     (1u << 13)  /* Transmitter ready interrupt enable */
#define UCR1_ADBR       (1u << 14)  /* Automatic detection of baud rate   */
#define UCR1_ADEN       (1u << 15)  /* Auto baud rate detection IRQ en    */

/* ── UCR2 bits ────────────────────────────────────────────────────────── */
#define UCR2_SRST       (1u << 0)   /* SW reset (active low, write 1 to clear) */
#define UCR2_RXEN       (1u << 1)   /* Receiver enable                    */
#define UCR2_TXEN       (1u << 2)   /* Transmitter enable                 */
#define UCR2_ATEN       (1u << 3)   /* Aging timer enable                 */
#define UCR2_RTSEN      (1u << 4)   /* RTS interrupt enable               */
#define UCR2_WS         (1u << 5)   /* Word size: 0=7-bit, 1=8-bit        */
#define UCR2_STPB       (1u << 6)   /* Stop bits: 0=1 stop, 1=2 stop      */
#define UCR2_PROE       (1u << 7)   /* Parity odd enable                  */
#define UCR2_PREN       (1u << 8)   /* Parity enable                      */
#define UCR2_RTEC_MASK  (3u << 9)   /* Request-to-send edge control mask  */
#define UCR2_ESCEN      (1u << 11)  /* Escape sequence interrupt enable   */
#define UCR2_CTS        (1u << 12)  /* CTS status                         */
#define UCR2_CTSC       (1u << 13)  /* CTS pin controlled by receiver     */
#define UCR2_IRTS       (1u << 14)  /* Ignore RTS pin                     */
#define UCR2_ESCI       (1u << 15)  /* Escape sequence interrupt enable   */

/* ── UCR3 bits ────────────────────────────────────────────────────────── */
#define UCR3_RXDMUXSEL  (1u << 2)   /* Mux RXD: must be set on i.MX8M    */

/* ── UCR4 bits ────────────────────────────────────────────────────────── */
#define UCR4_DREN       (1u << 0)   /* Receive data ready interrupt enable */
#define UCR4_OREN       (1u << 1)   /* Receiver overrun interrupt enable  */
#define UCR4_BKEN       (1u << 2)   /* BREAK condition interrupt enable   */
#define UCR4_TCEN       (1u << 3)   /* Transmit complete interrupt enable */
#define UCR4_INVR       (1u << 9)   /* Inverted infrared reception        */

/* ── UFCR bits ────────────────────────────────────────────────────────── */
#define UFCR_RXTL_SHIFT 0           /* RX trigger level [5:0]             */
#define UFCR_RXTL_MASK  (0x3Fu)
#define UFCR_DCEDTE     (1u << 6)   /* DCE/DTE mode select                */
#define UFCR_RFDIV_SHIFT 7          /* Reference frequency divider [9:7]  */
#define UFCR_RFDIV_MASK (7u << 7)
#define UFCR_TXTL_SHIFT 10          /* TX trigger level [15:10]           */
#define UFCR_TXTL_MASK  (0x3Fu << 10)

/* UFCR_RFDIV encoding: 000=6, 001=5, 010=4, 011=3, 100=2, 101=1, 110=7 */
#define UFCR_RFDIV(n)   (((n) & 7u) << UFCR_RFDIV_SHIFT)

/* ── USR1 bits ────────────────────────────────────────────────────────── */
#define USR1_AWAKE      (1u << 4)   /* Awake interrupt flag               */
#define USR1_AIRINT     (1u << 5)   /* Async IR interrupt flag            */
#define USR1_RXDS       (1u << 6)   /* Receiver idle condition            */
#define USR1_DTRD       (1u << 7)   /* DTR delta                          */
#define USR1_AGTIM      (1u << 8)   /* Aging timer interrupt flag         */
#define USR1_RRDY       (1u << 9)   /* Receiver ready                     */
#define USR1_FRAMERR    (1u << 10)  /* Frame error in RX FIFO             */
#define USR1_ESCF       (1u << 11)  /* Escape sequence flag               */
#define USR1_RTSD       (1u << 12)  /* RTS delta                          */
#define USR1_TRDY       (1u << 13)  /* Transmitter ready                  */
#define USR1_RTSS       (1u << 14)  /* RTS pin status                     */
#define USR1_PARITYERR  (1u << 15)  /* Parity error                       */

/* ── USR2 bits ────────────────────────────────────────────────────────── */
#define USR2_RDR        (1u << 0)   /* Receive data ready                 */
#define USR2_ORE        (1u << 1)   /* Overrun error                      */
#define USR2_BRCD       (1u << 2)   /* BREAK condition detected           */
#define USR2_TXDC       (1u << 3)   /* Transmitter complete (FIFO + SR empty) */
#define USR2_RTSF       (1u << 4)   /* RTS edge triggered interrupt flag  */
#define USR2_DCDIN      (1u << 5)   /* Data carrier detect               */
#define USR2_DCDDELT    (1u << 6)   /* Data carrier detect delta         */
#define USR2_WAKE       (1u << 7)   /* Wake                               */
#define USR2_IRINT      (1u << 8)   /* Serial infrared interrupt flag     */
#define USR2_RIIN       (1u << 9)   /* Ring indicator input               */
#define USR2_RIDELT     (1u << 10)  /* Ring indicator delta               */
#define USR2_ACST       (1u << 11)  /* Auto-baud counter stopped          */
#define USR2_IDLE       (1u << 12)  /* Idle condition                     */
#define USR2_DTRF       (1u << 13)  /* DTR edge triggered interrupt flag  */
#define USR2_TXFE       (1u << 14)  /* Transmit FIFO empty                */
#define USR2_ADET       (1u << 15)  /* Automatic baud-rate detect complete */

/* ── UTS bits ─────────────────────────────────────────────────────────── */
#define UTS_SOFTRST     (1u << 0)   /* Software reset                     */
#define UTS_RXFULL      (1u << 3)   /* RX FIFO full                       */
#define UTS_TXFULL      (1u << 4)   /* TX FIFO full                       */
#define UTS_RXEMPTY     (1u << 5)   /* RX FIFO empty                      */
#define UTS_TXEMPTY     (1u << 6)   /* TX FIFO empty                      */
#define UTS_RXDBG       (1u << 9)   /* RX FIFO debug mode                 */
#define UTS_LOOPIR      (1u << 10)  /* Loop IR test                       */
#define UTS_DBGEN       (1u << 11)  /* Debug enable                       */
#define UTS_LOOP        (1u << 12)  /* Loop TX->RX (loopback test)        */
#define UTS_FRCPERR     (1u << 13)  /* Force parity error                 */

/* ── URXD bits ────────────────────────────────────────────────────────── */
#define URXD_RX_DATA    0x00FFu     /* Received character [7:0]           */
#define URXD_PRERR      (1u << 10)  /* Parity error                       */
#define URXD_BRK        (1u << 11)  /* BREAK detected                     */
#define URXD_FRMERR     (1u << 12)  /* Frame error                        */
#define URXD_OVRRUN     (1u << 13)  /* Overrun error                      */
#define URXD_ERR        (1u << 14)  /* Any error bit set                  */
#define URXD_CHARRDY    (1u << 15)  /* Character ready in FIFO            */


#define IMX_PARITY_NONE 0
#define IMX_PARITY_EVEN 1
#define IMX_PARITY_ODD  2


/**
 * @brief imx8mp_uart_putc -- put a single character on the UART
 * @param c -- character to put
 */
extern void au_imx8mp_uart_putc(char c);
/**
 * @brief imx8mp_uart_puts -- put a collection of character on the UART
 * @param s -- String to send
 */
extern void au_imx8mp_uart_puts(const char* s);


/**
 * @brief imx8mp_uart_getc -- receive a single character from
 * the UART
 */
extern int au_imx8mp_uart_getc();

extern void au_imx8np_uart_initialize(uint64_t base);


#endif
