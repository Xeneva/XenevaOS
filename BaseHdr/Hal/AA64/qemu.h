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

#ifndef __QEMU_H__
#define __QEMU_H__

#ifdef __TARGET_BOARD_QEMU_VIRT__

/* GIC related informations*/
#define GIC_DIST 0x08000000
#define GIC_CPU  0x08010000
#define GIC_V2M  0x08020000
#define GIC_HYP  0x08030000
#define GIC_VCPU 0x08040000
#define GIC_ITS  0x08080000
#define GIC_REDIST 0x080A0000

#define GPIO  0x09030000 
#define UART0 0x09000000
#define UART1 0x09040000
#define RTC   0x09010000
#define SMMU  0x09050000
#define VIRT_MMIO 0x0a000000
#define FW_CFG 0x09020000
#define PCIE_MMIO  0x10000000
#define PCIE_PIP   0x3eff0000
#define PCIE_ECAM  0x3f000000

/* IRQ Mappings to GIC*/
#define UART0_IRQ  1
#define RTC_IRQ 2
#define PCIE_IRQ 3
#define GPIO_IRQ 7
#define UART1_IRQ 8
#define ACPI_GED 9

//#define PCIE_ECAM 0x10000000ULL
#endif


/*
 * AuVirtIOInputInitialize -- initialize virtIO input device
 */
extern void AuVirtIOInputInitialize();
#endif