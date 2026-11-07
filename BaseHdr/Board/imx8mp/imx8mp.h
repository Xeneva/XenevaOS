/**
* @file imx8mp.h
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

#ifndef __IMX8MP_H__
#define __IMX8MP_H__

#include <Board/imx8mp/imx8mp_uart.h>

#if defined(__TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__) || defined(__TARGET_BOARD_IMX8MP_SOC__)


/* GIC related informations*/
#define GIC_DIST  0x38800000ULL
#define GIC_CPU  
#define GIC_V2M  
#define GIC_HYP  
#define GIC_VCPU 
#define GIC_ITS  
#define GIC_REDIST 0x38880000ULL

#define GPIO  
#define SPI0  
#define UART0  IMX8MP_UART3_BASE_ADDRESS 
#define UART1  IMX8MP_UART1_BASE_ADDRESS
#define RTC   
#define SMMU 
#define VIRT_MMIO 
#define FW_CFG 
#define PCIE_MMIO  
#define PCIE_PIP   
#define PCIE_ECAM  
#define RTC

/* IRQ Mappings to GIC*/
#define UART0_IRQ  1
#define RTC_IRQ 2
#define PCIE_IRQ 3
#define GPIO_IRQ 7
#define UART1_IRQ 8
#define ACPI_GED 9

#endif


#endif
