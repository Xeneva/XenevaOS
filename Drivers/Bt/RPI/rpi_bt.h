/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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
#ifndef __RPI_BT_H__
#define __RPI_BT_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * Raspberry Pi 3B+ on-board Bluetooth: the Bluetooth half of the
 * Cypress CYW43455 combo chip (Broadcom name BCM4345C0), wired to the
 * PL011 UART on GPIO30-33 with RTS/CTS. Its regulator enable (BT_ON) is
 * line 0 of the firmware GPIO expander.
 *
 * External driver (rpibt.dll), loaded from /board.cnf. AuDriverMain only
 * arms a timer; the bring-up runs on a kernel thread once the scheduler
 * is up:
 *
 *   1. PL011 at 115200 8N1, RTS/CTS, UART clock 48 MHz
 *   2. power cycle the controller through BT_ON
 *   3. HCI Reset, HCI Read Local Version (picks the patch file)
 *   4. vendor "update UART baud rate" to BT_OPER_BAUD, PL011 follows
 *   5. vendor "download minidriver", then the .hcd patch file: a plain
 *      list of HCI vendor commands (write RAM ... launch RAM), each sent
 *      as is and acknowledged by a Command Complete
 *   6. the controller restarts in its patched firmware at 115200:
 *      PL011 back to 115200, HCI Reset, baud rate up again
 *   7. vendor "write BD_ADDR" with an address derived from the board
 *      serial (b8:27:eb:xx:xx:xx, the Raspberry Pi OUI)
 *   8. AuBtSetLocalName("XenevaOS") + AuBtH4UartReady(pl011): the kernel
 *      host stack (KernelAA64/Bt/host.c) takes the UART, brings the
 *      controller up and makes it discoverable
 *
 * The patch lives in controller RAM, so it is loaded on every boot. A
 * missing patch file is not fatal: the controller then runs its ROM
 * firmware.
 */

/* patch files in the initramfs root (8.3 names for the FAT driver) */
#define RPI_BT_FW_4345C0 "/bt43455.hcd" /* Pi 3B+ (CYW43455) */
#define RPI_BT_FW_43430A1 "/bt43430.hcd" /* Pi 3B  (BCM43438), optional */

/* LMP subversions the ROM firmware reports */
#define RPI_BT_SUBVER_4345C0 0x6119
#define RPI_BT_SUBVER_43430A1 0x2209

#define BT_INIT_BAUD 115200
#define BT_OPER_BAUD 921600
#define BT_UART_CLOCK 48000000

#endif
