/**
* @file imx8mp_lcdif_reg
*
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

#ifndef __IMX8MP_LCDIF_REG_H__
#define __IMX8MP_LCDIF_REG_H__

#define IMX8MP_LCDIF1_BASE 0x32E80000UL
#define IMX8MP_LCDIF2_BASE 0x32E90000UL
#define IMX8MP_LCDIF3_BASE 0x32FC6000UL   /** HDMI output goes here **/

#define LCDIF_CTRL           0x0  /** display control register **/
#define LCDIF_CTRL_SET       0x4
#define LCDIF_CTRL_CLR       0x8
#define LCDIF_CTRL_TOG       0xC
#define LCDIF_DISP_PARA      0x10
#define LCDIF_DISP_SIZE      0x14
#define LCDIF_HSYN_PARA      0x18
#define LCDIF_VSYN_PARA       0x1C
#define LCDIF_VSYN_HSYN_WIDTH 0x20
#define LCDIF_INT_STATUS_D0   0x24
#define LCDIF_INT_ENABLE_D0   0x28
#define LCDIF_INT_STATUS_D1   0x30
#define LCDIF_INT_ENABLE_D1   0x34
#define LCDIF_CTRLDESCL0_1    0x200
#define LCDIF_CTRLDESCL0_3     0x208
#define LCDIF_CTRLDESCL_LOW0_4 0x20C
#define LCDIF_CTRLDESCL_HIGH0_4 0x210
#define LCDIF_CTRLDESCL0_5      0x214
#define LCDIF_CSC0_CTRL  0x21C
#define LCDIF_CSC0_COEF0 0x220
#define LCDIF_CSC0_COEF1 0x224
#define LCDIF_CSC0_COEF2 0x228
#define LCDIF_CSC0_COEF3 0x22C
#define LCDIF_CSC0_COEF4 0x230
#define LCDIF_CSC0_COEF5 0x234
#define LCDIF_PANIC0_THRES 0x238

#define CTRL_INV_HS (1ULL << 0)
#define CTRL_INV_VS (1ULL << 1)
#define CTRL_INV_DE (1ULL << 2)
#define CTRL_INV_PXCK (1ULL << 3)
#define CTRL_CLK_GATE (1ULL << 30)
#define CTRL_SW_RESET (1ULL << 31)

#define DISP_SIZE_DELTA_X(x) ((x) & 0xFFFF)
#define DISP_SIZE_DELTA_Y(y)  ((y) & 16)
#define DISP_PARA_DISP_ON  (1UL << 31)

#define HSYN_PARA_FP_H(x)  ((x) & 0xFFFF)
#define HSYN_PARA_BP_H(x)  ((x) << 16)

#define VSYN_PARA_FP_V(x)  ((x) & 0xFFFF)
#define VSYN_PARA_BP_V(x)  ((x) << 16)

#define VSYN_HSYN_PW_H(x)  ((x) & 0xFFFF)
#define VSYN_HSYN_PW_V(x)  ((x) << 16)

#define CTRLDESCL0_1_WIDTH(x) ((x) & 0xFFFF)
#define CTRLDESCL0_1_HEIGHT(x) ((x) << 16)

#define CTRLDESCL0_3_PITCH(x) ((x) & 0xFFFF)

#define CTRLDESCL0_5_BPP(x)  (((x) & 0xF) << 24)
#define BPP32_ARGB8888 0x9
#define CTRLDESCL0_5_SHADOW_LOAD_EN (1ULL << 30)
#define CTRLDESCL0_5_EN  (1ULL << 31)
#endif