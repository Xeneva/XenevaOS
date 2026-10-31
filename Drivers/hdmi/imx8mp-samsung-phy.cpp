/**
* @file imx8mp-samsung-phy.cpp
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include "imx8mp_hdmi_tx_c.h"
#include <Drivers/core.h>
#include <Drivers/res.h>
#include <bordoisila_bits.h>
#include <bordoisila_io.h>
#include <linux/compiler.h>
#include <Log/klog.h>
#include <_null.h>
#include <Mm/kmalloc.h>
#include <Drivers/uart.h>
#include <string.h>
#include <dtb.h>
#include <Mm/vmmngr.h>

/**
 * Reference Linux source code
 */

#define PHY_REG(reg)   (reg * 4)

#define REG01_PMS_P_MASK   BORDOISILA_GENMASK(3,0)
#define REG03_PMS_S_MASK   BORDOISILA_GENMASK(7,4)
#define REG12_CK_DIV_MASK  BORDOISILA_GENMASK(5,4)

#define REG13_TG_CODE_LOW_MASK  BORDOISILA_GENMASK(7,0)
#define REG14_TOL_MASK          BORDOISILA_GENMASK(7,4)
#define REG14_RP_CODE_MASK      BORDOISILA_GENMASK(3,1)
#define REG14_TG_CODE_HIGH_MASK   BORDOISILA_GENMASK(0,0)

#define REG21_SEL_TX_CK_INV    BORDOISILA_BIT(7)
#define REG21_PMS_S_MASK       BORDOISILA_GENMASK(3,0)

#define REG33_MODE_SET_DONE   BORDOISILA_BIT(7)
#define REG33_FIX_DA          BORDOISILA_BIT(1)

#define REG34_PHY_READY       BORDOISILA_BIT(7)
#define REG34_PLL_LOCK        BORDOISILA_BIT(6)
#define REG34_PHY_CLK_READY   BORDOISILA_BIT(5)

#ifndef MHZ
#define MHZ   (1000UL * 1000UL)
#endif

#define PHY_PLL_DIV_REGS_NUM  7

struct phy_config {
	uint32_t pixclk;
	uint8_t pll_div_regs[PHY_PLL_DIV_REGS_NUM];
};

static struct phy_config calculated_phy_pll_cfg = {
	0,
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00},
};

static const struct phy_config phy_pll_cfg[] = {
	{ 22250000, {0xd1,0x4b,0xf1,0x89,0x88,0x80,0x40}},
	{ 23750000, {0xd1,0x50,0xf1,0x86,0x85,0x80,0x40}},
	{ 24024000, {0xd1, 0x50, 0xf1, 0x99, 0x02, 0x80, 0x40}},
	{ 25175000, {0xd1, 0x54, 0xfc, 0xcc, 0x91, 0x80, 0x40}},
	{ 26750000, {0xd1, 0x5a, 0xf2, 0x89, 0x88, 0x80, 0x40}},
	{ 27027000, {0xd1, 0x5a, 0xf2, 0xfd, 0x0c, 0x80, 0x40}},
	{ 29500000, {0xd1, 0x62, 0xf4, 0x95, 0x08, 0x80, 0x40}},
	{ 30750000, {0xd1, 0x66, 0xf4, 0x82, 0x01, 0x88, 0x45}},
	{ 30888000, {0xd1, 0x66, 0xf4, 0x99, 0x18, 0x88, 0x45}},
	{ 33750000, {0xd1, 0x70, 0xf4, 0x82, 0x01, 0x80, 0x40}},
	{ 35000000, {0xd1, 0x58, 0xb8, 0x8b, 0x88, 0x80, 0x40}},
	{ 36036000, {0xd1, 0x5a, 0xb2, 0xfd, 0x0c, 0x80, 0x40}},
	{ 43243200, {0xd1, 0x5a, 0x92, 0xfd, 0x0c, 0x80, 0x40}},
	{ 44500000, {0xd1, 0x5c, 0x92, 0x98, 0x11, 0x84, 0x41}},
	{ 47000000, {0xd1, 0x62, 0x94, 0x95, 0x82, 0x80, 0x40}},
	{47500000,  {0xd1, 0x63, 0x96, 0xa1, 0x82, 0x80, 0x40}},
	{ 50349650, {0xd1, 0x54, 0x7c, 0xc3, 0x8f, 0x80, 0x40}},
	{ 53250000, {0xd1, 0x58, 0x72, 0x84, 0x03, 0x82, 0x41}},
	{ 53500000, {0xd1, 0x5a, 0x72, 0x89, 0x88, 0x80, 0x40}},
	{ 54054000, {0xd1, 0x5a, 0x72, 0xfd, 0x0c, 0x80, 0x40}},
	{ 59000000, {0xd1, 0x62, 0x74, 0x95, 0x08, 0x80, 0x40}},
	{ 59340659, {0xd1, 0x62, 0x74, 0xdb, 0x52, 0x88, 0x47}},
	{ 61500000, {0xd1, 0x66, 0x74, 0x82, 0x01, 0x88, 0x45}},
	{ 63500000, {0xd1, 0x69, 0x74, 0x89, 0x08, 0x80, 0x40}},
	{ 67500000, {0xd1, 0x54, 0x52, 0x87, 0x03, 0x80, 0x40}},
	{ 70000000, {0xd1, 0x58, 0x58, 0x8b, 0x88, 0x80, 0x40}},
	{ 72072000, {0xd1, 0x5a, 0x52, 0xfd, 0x0c, 0x80, 0x40}},
	{ 74176000, {0xd1, 0x5d, 0x58, 0xdb, 0xa2, 0x88, 0x41}},
	{ 74250000, {0xd1, 0x5c, 0x52, 0x90, 0x0d, 0x84, 0x41}},
	{ 78500000, {0xd1, 0x62, 0x54, 0x87, 0x01, 0x80, 0x40}},
	{ 82000000, {0xd1, 0x66, 0x54, 0x82, 0x01, 0x88, 0x45}},
	{ 82500000, {0xd1, 0x67, 0x54, 0x88, 0x01, 0x90, 0x49}},
	{ 89000000, {0xd1, 0x70, 0x54, 0x84, 0x83, 0x80, 0x40}},
	{ 90000000, {0xd1, 0x70, 0x54, 0x82, 0x01, 0x80, 0x40}},
	{ 94000000, {0xd1, 0x4e, 0x32, 0xa7, 0x10, 0x80, 0x40}},
	{ 95000000, {0xd1, 0x50, 0x31, 0x86, 0x85, 0x80, 0x40}},
	{ 98901099, {0xd1, 0x52, 0x3a, 0xdb, 0x4c, 0x88, 0x47}},
	{ 99000000, {0xd1, 0x52, 0x32, 0x82, 0x01, 0x88, 0x47}},
	{ 100699300, {0xd1, 0x54, 0x3c, 0xc3, 0x8f, 0x80, 0x40}},
	{ 102500000, {0xd1, 0x55, 0x32, 0x8c, 0x05, 0x90, 0x4b}},
	{ 104750000, {0xd1, 0x57, 0x32, 0x98, 0x07, 0x90, 0x49}},
	{ 106500000, {0xd1, 0x58, 0x32, 0x84, 0x03, 0x82, 0x41}},
	{ 107000000, {0xd1, 0x5a, 0x32, 0x89, 0x88, 0x80, 0x40}},
	{ 108108000, {0xd1, 0x5a, 0x32, 0xfd, 0x0c, 0x80, 0x40}},
	{ 118000000, {0xd1, 0x62, 0x34, 0x95, 0x08, 0x80, 0x40}},
	{ 123000000, {0xd1, 0x66, 0x34, 0x82, 0x01, 0x88, 0x45}},
	{ 127000000, {0xd1, 0x69, 0x34, 0x89, 0x08, 0x80, 0x40}},
	{ 135000000, {0xd1, 0x70, 0x34, 0x82, 0x01, 0x80, 0x40}},
	{ 135580000, {0xd1, 0x71, 0x39, 0xe9, 0x82, 0x9c, 0x5b}},
	{ 137520000, {0xd1, 0x72, 0x38, 0x99, 0x10, 0x85, 0x41}},
	{ 138750000, {0xd1, 0x73, 0x35, 0x88, 0x05, 0x90, 0x4d}},
	{ 140000000, {0xd1, 0x75, 0x36, 0xa7, 0x90, 0x80, 0x40}},
	{ 148352000, {0xd1, 0x7b, 0x35, 0xdb, 0x39, 0x90, 0x45}},
	{ 148500000, {0xd1, 0x7b, 0x35, 0x84, 0x03, 0x90, 0x45}},
	{ 154000000, {0xd1, 0x40, 0x18, 0x83, 0x01, 0x00, 0x40}},
	{ 157000000, {0xd1, 0x41, 0x11, 0xa7, 0x14, 0x80, 0x40}},
	{ 160000000, {0xd1, 0x42, 0x12, 0xa1, 0x20, 0x80, 0x40}},
	{ 162000000, {0xd1, 0x43, 0x18, 0x8b, 0x08, 0x96, 0x55}},
	{ 164000000, {0xd1, 0x45, 0x11, 0x83, 0x82, 0x90, 0x4b}},
	{ 165000000, {0xd1, 0x45, 0x11, 0x84, 0x81, 0x90, 0x4b}},
	{ 185625000, {0xd1, 0x4e, 0x12, 0x9a, 0x95, 0x80, 0x40}},
	{ 188000000, {0xd1, 0x4e, 0x12, 0xa7, 0x10, 0x80, 0x40}},
	{ 198000000, {0xd1, 0x52, 0x12, 0x82, 0x01, 0x88, 0x47}},
	{ 205000000, {0xd1, 0x55, 0x12, 0x8c, 0x05, 0x90, 0x4b}},
	{ 209500000, {0xd1, 0x57, 0x12, 0x98, 0x07, 0x90, 0x49}},
	{ 213000000, {0xd1, 0x58, 0x12, 0x84, 0x03, 0x82, 0x41}},
	{ 216216000, {0xd1, 0x5a, 0x12, 0xfd, 0x0c, 0x80, 0x40}},
	{ 254000000, {0xd1, 0x69, 0x14, 0x89, 0x08, 0x80, 0x40}},
	{ 277500000, {0xd1, 0x73, 0x15, 0x88, 0x05, 0x90, 0x4d}},
	{ 297000000, {0xd1, 0x7b, 0x15, 0x84, 0x03, 0x90, 0x45}},
};

struct reg_settings {
	uint8_t reg;
	uint8_t val;
};

static const struct reg_settings common_phy_cfg[] = {
	{PHY_REG(0), 0x00},
	/* PHY_REG(1-7) pix clk specific */
	{PHY_REG(8), 0x4f},
	{PHY_REG(9), 0x30},
	{PHY_REG(10), 0x33},
	{PHY_REG(11), 0x65},
	/* PHY_REG(12) pix clk specific */
	/* REG13 pixclk specific */
	/* REG14 pixclk specific */
	{PHY_REG(15), 0x80},
	{PHY_REG(16), 0x6c},
	{PHY_REG(17), 0xf2},
	{PHY_REG(18), 0x67},
	{PHY_REG(19), 0x00},
	{PHY_REG(20), 0x10},
	/* REG21 pixclk specific */
	{PHY_REG(22), 0x30},
	{PHY_REG(23), 0x32},
	{PHY_REG(24), 0x60},
	{PHY_REG(25), 0x8f},
	{PHY_REG(26), 0x00},
	{PHY_REG(27), 0x00},
	{PHY_REG(28), 0x08},
	{PHY_REG(29), 0x00},
	{PHY_REG(30), 0x00},
	{PHY_REG(31), 0x00},
	{PHY_REG(32), 0x00},
	{PHY_REG(33), 0x80},
	{PHY_REG(34), 0x00},
	{PHY_REG(35), 0x00},
	{PHY_REG(36), 0x00},
	{PHY_REG(37), 0x00},
	{PHY_REG(38), 0x00},
	{PHY_REG(39), 0x00},
	{PHY_REG(40), 0x00},
	{PHY_REG(41), 0xe0},
	{PHY_REG(42), 0x83},
	{PHY_REG(43), 0x0f},
	{PHY_REG(44), 0x3E},
	{PHY_REG(45), 0xf8},
	{PHY_REG(46), 0x00},
	{PHY_REG(47), 0x00}
};

static uint64_t phy_reg;
const struct phy_config* cur_cfg;

static int fsl_samsung_phy_configure_pll_lock_det(const struct phy_config* cfg) {

	uint32_t pclk = cfg->pixclk;
	UARTDebugOut("pclk pll lock det : %d \r\n", pclk);
	uint32_t fld_tg_code;
	uint32_t int_pllclk;
	uint8_t div;

	/* find int_pllclk speed */
	for (div = 0; div < 4; div++) {
		int_pllclk = pclk / (1 << div);
		if (int_pllclk < (50 * MHZ))
			break;
	}

	if (unlikely(div == 4))
		return -1;

	_bordoisila_writeb(BORDOISILA_PREP_FIELD(REG12_CK_DIV_MASK, div), phy_reg + PHY_REG(12));

	fld_tg_code = BORDOISILA_DIV_ROUND_UP(24 * MHZ * 256, int_pllclk);

	fld_tg_code = 166;
	
	_bordoisila_writeb(BORDOISILA_PREP_FIELD(REG13_TG_CODE_LOW_MASK, fld_tg_code),
		phy_reg + PHY_REG(13));
	
	_bordoisila_writeb(BORDOISILA_PREP_FIELD(REG14_TOL_MASK, 2) |
		BORDOISILA_PREP_FIELD(REG14_RP_CODE_MASK, 2) |
		BORDOISILA_PREP_FIELD(REG14_TG_CODE_HIGH_MASK, fld_tg_code >> 8),
		phy_reg + PHY_REG(14));
	
	return 0;
}

static unsigned long fsl_samsung_hdmi_phy_find_pms(unsigned long fout, uint8_t* p, uint16_t* m, uint8_t* s) {
	
	unsigned long best_freq = 0;
	uint32_t min_delta = 0xffffffff;
	uint8_t _p, best_p;
	uint16_t _m, best_m;
	uint8_t _s, best_s;

	/*
	 * Figure 13-78 of the reference manual states the PLL should be TMDS x 5
	 * while the TMDS_CLKO should be the PLL / 5. So to calculate the PLL, 
	 * take the pix clock x 5, then return the value of the PLL / 5/
	 */
	fout *= 5;

	/* The ref manual sttes the values of 'p' range from 1 to 11 */
	for (_p = 1; _p <= 11; ++_p) {
		for (_s = 1; _s <= 16; ++_s) {
			uint64_t tmp;
			uint32_t delta;

			/* s must be one or even */
			if (_s > 1 && (_s & 0x01) == 1)
				_s++;

			if (_s == 14)
				continue;

			tmp = (uint64_t)fout * (_p * _s);
			do_div(tmp, 24 * MHZ);
			if (tmp > 255)
				continue;
			_m = tmp;

			/*
			 * Rev 2 of the Ref Manual states the
			 * VCO can range between 750MHz and 
			 * 3GHz. The VCO is assumed to be 
			 * Fvco = (M * f_ref) / P,
			 * where f_ref is 24MHz.
			 */
			tmp = div64_ul((uint64_t)_m * 24 * MHZ, _p);
			if (tmp < 750 * MHZ ||
				tmp > 3000 * MHZ)
				continue;

			/* final frequency after post-divider*/
			do_div(tmp, _s);

			delta = ABS(fout - tmp);
			if (delta < min_delta) {
				best_p = _p;
				best_s = _s;
				best_m = _m;
				min_delta = delta;
				best_freq = tmp;
			}

			/* if we have an exact match, stop locking for a better value */
			if (!delta)
				goto done;
		}
	}
done:
	if (best_freq) {
		*p = best_p;
		*m = best_m;
		*s = best_s;
	}

	return best_freq / 5;
}

static int fsl_samsung_phy_configure(const struct phy_config* cfg) {
	int i, ret;
	uint8_t val;

	cur_cfg = cfg;

	BPrintK(BORDOISILA_DEBUG, "fsl-samsung-hdmi: using pix clock rate : %u \r\n", cfg->pixclk);

	_bordoisila_writeb(REG33_FIX_DA,phy_reg + PHY_REG(33));

	/* common PHY registers */
	for (i = 0; i < ARRAY_SIZE(common_phy_cfg); i++) {
		UARTDebugOut("Writing val : %x to reg :%x \r\n", common_phy_cfg[i].val, common_phy_cfg[i].reg);
		_bordoisila_writeb(common_phy_cfg[i].val, phy_reg + common_phy_cfg[i].reg);
	}
	
	UARTDebugOut("REG34 Value : %x \r\n", _bordoisila_readb(phy_reg + PHY_REG(34)));
	
	/* set individual PLL registers PHY_REG1 ... PHY_REG7 */
	for (i = 0; i < PHY_PLL_DIV_REGS_NUM; i++) {
		_bordoisila_writeb(cfg->pll_div_regs[i], phy_reg + PHY_REG(1) + i * 4);
		UARTDebugOut("Pixel clock value : %x , reg : %x \r\n", cfg->pll_div_regs[i], (phy_reg + PHY_REG(1) + i * 4));
	}

	UARTDebugOut("REG34 Value : %x \r\n", _bordoisila_readb(phy_reg + PHY_REG(34)));

	/* High nibble of PHY_REG3 and low nibble of PHY_REG21 both contain 'S' */
	UARTDebugOut("PHYREG(21) before : %x \r\n", _bordoisila_readl(phy_reg + PHY_REG(21)));
	_bordoisila_writeb(REG21_SEL_TX_CK_INV | BORDOISILA_PREP_FIELD(REG21_PMS_S_MASK,
		cfg->pll_div_regs[2] >> 4), phy_reg + PHY_REG(21));
	UARTDebugOut("PHYREG(21) after : %x \r\n", _bordoisila_readl(phy_reg + PHY_REG(21)));


	ret = fsl_samsung_phy_configure_pll_lock_det(cfg);
	if (ret) {
		BPrintK(BORDOISILA_ERROR, "fsl-samsung-phy-hdmi : pixclock too large \r\n");
		return ret;
	}

	UARTDebugOut("REG34 Value : %x \r\n", _bordoisila_readb(phy_reg + PHY_REG(34)));

	_bordoisila_writeb(REG33_FIX_DA | REG33_MODE_SET_DONE, phy_reg + PHY_REG(33));

	ret = 1;//_bordoisila_readb_poll_timeout(phy_reg + PHY_REG(34), val, val & REG34_PLL_LOCK, 50, 20000);

	UARTDebugOut("REG33 Value : %x \r\n", _bordoisila_readl(phy_reg + PHY_REG(33)));

	for (volatile int i = 0; i < 10000; i++)
		;

	int timeout = 10000000;
	while (--timeout) {
		uint32_t v = _bordoisila_readl(phy_reg + PHY_REG(34));
		if (v & REG34_PLL_LOCK) {
			BPrintK(BORDOISILA_DEBUG, "REG34 pll lock got \r\n");
			ret = 0;
			break;
		}
	}

	if (ret) 
		BPrintK(BORDOISILA_ERROR, "PLL failed to lock \r\n");
	
	return ret;
}

static unsigned long phy_clk_recalc_rate(unsigned long parent_rate) {
	if (!cur_cfg)
		return 74250000;
	return cur_cfg->pixclk;
}

static const struct phy_config* fsl_samsung_hdmi_phy_lookup_rate(unsigned long rate) {
	int i;

	BPrintK(BORDOISILA_DEBUG, "fsl_samsung_hdmi_phy_lookup rate: ARRAY_SIZE: %d \r\n", ARRAY_SIZE(phy_pll_cfg));

	for (i = ARRAY_SIZE(phy_pll_cfg) - 1; i >= 0; i--)
		if (phy_pll_cfg[i].pixclk <= rate)
			break;

	if (phy_pll_cfg[i].pixclk == rate || i + 1 > ARRAY_SIZE(phy_pll_cfg) - 1)
		return &phy_pll_cfg[i];

	return (ABS((long)rate - (long)phy_pll_cfg[i].pixclk) <
		ABS((long)rate - (long)phy_pll_cfg[i + 1].pixclk) ?
		&phy_pll_cfg[i] : &phy_pll_cfg[i + 1]);
}

static void fsl_samsung_hdmi_calculate_phy(struct phy_config* cal_phy, unsigned long rate, uint8_t p, uint16_t m, uint8_t s) {
	cal_phy->pixclk = rate;
	cal_phy->pll_div_regs[0] = BORDOISILA_PREP_FIELD(REG01_PMS_P_MASK, p);
	cal_phy->pll_div_regs[1] = m;
	cal_phy->pll_div_regs[2] = BORDOISILA_PREP_FIELD(REG03_PMS_S_MASK, s - 1);
	/* pll_div_regs 3-6 are fixed and pre-defined already */
}

static const struct phy_config* fsl_samsung_hdmi_phy_find_settings(unsigned long rate) {

	const struct phy_config* fract_div_phy;
	uint32_t int_div_clk;
	uint16_t m;
	uint8_t p, s;

	/* if the clock is out of range return error instead of searching */
	if (rate > 297000000 || rate < 22250000)
		return NULL;

	fract_div_phy = fsl_samsung_hdmi_phy_lookup_rate(rate);
	if (fract_div_phy->pixclk == rate) {
		BPrintK(BORDOISILA_DEBUG, "fsl-samsung-phy-hdmi: fractional divider match = %u \r\n", fract_div_phy->pixclk);
		return fract_div_phy;
	}

	/* calculate the integer divider */
	int_div_clk = fsl_samsung_hdmi_phy_find_pms(rate, &p, &m, &s);
	fsl_samsung_hdmi_calculate_phy(&calculated_phy_pll_cfg, int_div_clk, p, m, s);
	if (int_div_clk == rate) {
		BPrintK(BORDOISILA_DEBUG, "fsl-samsung-phy-hdmi: integer divider match = %u \r\n", calculated_phy_pll_cfg.pixclk);
		return &calculated_phy_pll_cfg;
	}

	/* Calculate the absolute value of the differences and return whichever is closest */
	if (ABS((long)rate - (long)int_div_clk) < ABS((long)rate - (long)fract_div_phy->pixclk)) {
		BPrintK(BORDOISILA_DEBUG, "fsl-samsung-phy-hdmi: integer divider = %u \r\n", calculated_phy_pll_cfg.pixclk);
		return &calculated_phy_pll_cfg;
	}

	BPrintK(BORDOISILA_DEBUG, "fsl-samsung-phy-hdmi: fractional divider = %u\r\n", cur_cfg->pixclk);

	return fract_div_phy;
}

static int fsl_samsung_hdmi_phy_probe(BordoisilaDriver* driver) {
	BPrintK(BORDOISILA_DEBUG, "fsl-samsung-hdmi-phy initializing ...\r\n");
	
	phy_reg = 0x32FDFF00; //size 0x100

	uint32_t val = _bordoisila_readl(phy_reg + PHY_REG(34));
	UARTDebugOut("PHY_REG offset : %x, value : %x r\n", (phy_reg + PHY_REG(34)), val);

	const phy_config* cfg = fsl_samsung_hdmi_phy_lookup_rate(148500000);
	if (!cfg) {
		BPrintK(BORDOISILA_ERROR, "[[fsl-samsung-phy]: failed to get suitable pll config \r\n");
		return 1;
	}
	int ret = fsl_samsung_phy_configure(cfg);
	BPrintK(BORDOISILA_DEBUG, "fsl-samsung-hdmi-phy initiailized : %u\r\n", ret);
	for (;;);
	return ret;
}

static BordoisilaDriver _samsung_hdmi;


int fsl_samsung_hdmi_phy_init() {
	_samsung_hdmi.name = (char*)kmalloc(strlen("imx8mp-samsung-phy"));
	strcpy((char*)_samsung_hdmi.name, "imx8mp-samsung-phy");
	_samsung_hdmi.type = BORDOISILA_DRIVER_NORMAL;
	_samsung_hdmi.probe = &fsl_samsung_hdmi_phy_probe;
	_samsung_hdmi.remove = 0;
	_samsung_hdmi.resume = 0;
	_samsung_hdmi.suspend = 0;

	if (BordoisilaDriverRegister(&_samsung_hdmi)) {
		BPrintK(BORDOISILA_ERROR,"fsl-samsung-hdmi-phy failed to register itself \r\n");
		return 1;
	}

	/* start the probing process */
	return _samsung_hdmi.probe(&_samsung_hdmi);
}

