/**
* @file imx8mp_pll.h
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

#ifndef __IMX8MP_PLL_H__
#define __IMX8MP_PLL_H__

#include <bordoisila_bits.h>

/** This file is all about PLL interface, which is used to 
 *  change or modify PLL clock source from CCM module,
 * by default, values are set according to the suitable environment
 * so directly PLL can be used on target registers
 */
#define __IMX8MP_CCM_BASE__ 0x30360000 //hardcoded from IMX8MP reference PRM, CCM_ANALOG_BASE
/** GROUP A table **/
#define __IMX8MP_AUDIO_PLL1_GEN_CTRL   __IMX8MP_CCM_BASE__
#define __IMX8MP_AUDIO_PLL1_FDIV_CTL0  (__IMX8MP_CCM_BASE__ + 0x4)
#define __IMX8MP_AUDIO_PLL1_FDIV_CTL1  (__IMX8MP_CCM_BASE__ + 0x8)
#define __IMX8MP_AUDIO_PLL1_SSCG_CTRL  (__IMX8MP_CCM_BASE__ + 0xC)
#define __IMX8MP_AUDIO_PLL1_MNIT_CTRL  (__IMX8MP_CCM_BASE__ + 0x10)


#define __IMX8MP_AUDIO_PLL2_GEN_CTRL   (__IMX8MP_CCM_BASE__ + 0x14)
#define __IMX8MP_AUDIO_PLL2_FDIV_CTL0  (__IMX8MP_CCM_BASE__ + 0x18)
#define __IMX8MP_AUDIO_PLL2_FDIV_CTL1  (__IMX8MP_CCM_BASE__ + 0x1C)
#define __IMX8MP_AUDIO_PLL2_SSCG_CTRL  (__IMX8MP_CCM_BASE__ + 0x20)
#define __IMX8MP_AUDIO_PLL2_MNIT_CTRL  (__IMX8MP_CCM_BASE__ + 0x24)

#define __IMX8MP_VIDEO_PLL1_GEN_CTRL   (__IMX8MP_CCM_BASE__ + 0x28)
#define __IMX8MP_VIDEO_PLL1_FDIV_CTL0  (__IMX8MP_CCM_BASE__ + 0x2C)
#define __IMX8MP_VIDEO_PLL1_FDIV_CTL1  (__IMX8MP_CCM_BASE__ + 0x30)
#define __IMX8MP_VIDEO_PLL1_SSCG_CTRL  (__IMX8MP_CCM_BASE__ + 0x34)
#define __IMX8MP_VIDEO_PLL1_MNIT_CTRL  (__IMX8MP_CCM_BASE__ + 0x38)

#define __IMX8MP_DRAM_PLL_GEN_CTRL     (__IMX8MP_CCM_BASE__ + 0x50)
#define __IMX8MP_DRAM_PLL_FDIV_CTL0    (__IMX8MP_CCM_BASE__ + 0x54)
#define __IMX8MP_DRAM_PLL_FDIV_CTL1    (__IMX8MP_CCM_BASE__ + 0x58)
#define __IMX8MP_DRAM_PLL_SSCG_CTRL    (__IMX8MP_CCM_BASE__ + 0x5C)
#define __IMX8MP_DRAM_PLL_MNIT_CTRL    (__IMX8MP_CCM_BASE__ + 0x60)

#define __IMX8MP_GPU_PLL_GEN_CTRL      (__IMX8MP_CCM_BASE__ + 0x64)
#define __IMX8MP_GPU_PLL_FDIV_CTL0      (__IMX8MP_CCM_BASE__ + 0x68)
#define __IMX8MP_GPU_PLL_LOCKD_CTRL    (__IMX8MP_CCM_BASE__ + 0x6C)
#define __IMX8MP_GPU_PLL_MNIT_CTRL     (__IMX8MP_CCM_BASE__ + 0x70)

#define __IMX8MP_VPU_PLL_GEN_CTRL   (__IMX8MP_CCM_BASE__ + 0x74)
#define __IMX8MP_VPU_PLL_FDIV_CTL0  (__IMX8MP_CCM_BASE__ + 0x78)
#define __IMX8MP_VPU_PLL_LOCKD_CTRL (__IMX8MP_CCM_BASE__ + 0x7C)
#define __IMX8MP_VPU_PLL_MNIT_CTRL  (__IMX8MP_CCM_BASE__ + 0x80)

#define __IMX8MP_ARM_PLL_GEN_CTRL    (__IMX8MP_CCM_BASE__ + 0x84)
#define __IMX8MP_ARM_PLL_FDIV_CTL0   (__IMX8MP_CCM_BASE__ + 0x88)
#define __IMX8MP_ARM_PLL_LOCKD_CTRL  (__IMX8MP_CCM_BASE__ + 0x8C)
#define __IMX8MP_ARM_PLL_MNIT_CTRL   (__IMX8MP_CCM_BASE__ + 0x90)

#define __IMX8MP_SYS_PLL1_GEN_CTRL   (__IMX8MP_CCM_BASE__ + 0x94)
#define __IMX8MP_SYS_PLL1_FDIV_CTL0  (__IMX8MP_CCM_BASE__ + 0x98)
#define __IMX8MP_SYS_PLL1_LOCKD_CTRL (__IMX8MP_CCM_BASE__ + 0x9C)
#define __IMX8MP_SYS_PLL1_MNIT_CTRL  (__IMX8MP_CCM_BASE__ + 0x100)

#define __IMX8MP_SYS_PLL2_GEN_CTRL   (__IMX8MP_CCM_BASE__ + 0x104)
#define __IMX8MP_SYS_PLL2_FDIV_CTL0  (__IMX8MP_CCM_BASE__ + 0x108)
#define __IMX8MP_SYS_PLL2_LOCKD_CTRL (__IMX8MP_CCM_BASE__ + 0x10C)
#define __IMX8MP_SYS_PLL2_MNIT_CTRL  (__IMX8MP_CCM_BASE__ + 0x110)

#define __IMX8MP_SYS_PLL3_GEN_CTRL   (__IMX8MP_CCM_BASE__ + 0x114)
#define __IMX8MP_SYS_PLL3_FDIV_CTL0  (__IMX8MP_CCM_BASE__ + 0x118)
#define __IMX8MP_SYS_PLL3_LOCKD_CTRL (__IMX8MP_CCM_BASE__ + 0x11C)
#define __IMX8MP_SYS_PLL3_MNIT_CTRL  (__IMX8MP_CCM_BASE__ + 0x120)

#define __IMX8MP_OSC_MISC_CFG         (__IMX8MP_CCM_BASE__ + 0x124)
#define __IMX8MP_ANAMIX_PLL_MNIT_CTL  (__IMX8MP_CCM_BASE__ + 0X128)
#define __IMX8MP_CCM_ANALOG_DIGPROG   0x800


/** imx8mp uses PLL14xx modern analog hardware ip block **/
#define IMX8MP_PLL14XX_MDIV_SHIFT 12
#define IMX8MP_PLL14XX_MDIV_MASK  BORDOISILA_GENMASK(21,12)
#define IMX8MP_PLL14XX_PDIV_SHIFT 4
#define IMX8MP_PLL14XX_PDIV_MASK  BORDOISILA_GENMASK(9,4)
#define IMX8MP_PLL14XX_SDIV_SHIFT  0
#define IMX8MP_PLL14XX_SDIV_MASK  BORDOISILA_GENMASK(2, 0)
#define IMX8MP_PLL14XX_KDIV_SHIFT 0
#define IMX8MP_PLL14XX_KDIV_MASK  BORDOISILA_GENMASK(15, 0)

#define LOCK_STATUS    BORDOISILA_BIT(31)
#define LOCK_SEL_MASK  BORDOISILA_BIT(29)
#define CLKE_MASK      BORDOISILA_BIT(11)
#define RST_MASK       BORDOISILA_BIT(9)
#define BYPASS_MASK    BORDOISILA_BIT(4)


struct imx_pll14xx_rate_table {
	unsigned int rate;
	unsigned int pdiv;
	unsigned int mdiv;
	unsigned int sdiv;
	unsigned int kdiv;
};


extern void imx8mp_pll_init();

/**
 * @brief imx8mp_pll_recalc_rate -- recalculate pll rate
 * @param pllbase -- pll base source or entry
 * @param parent_rate -- clock rate, if the target root is high powered
 * device, it uses osc_24m sel otherwise osc_32k is used for low powered
 * device
 */
extern unsigned long imx8mp_pll_recalc_rate(uint64_t pllbase, unsigned long parent_rate);


extern uint32_t imx8mp_pll_get_parent_rate(uint64_t pll_base);

#endif
