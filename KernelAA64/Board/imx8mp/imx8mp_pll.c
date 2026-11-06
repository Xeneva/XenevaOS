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

#include <Board/imx8mp/imx8mp_pll.h>
#include <aucon.h>
#include <bordoisila_io.h>
#include <_null.h>
#include <Log/klog.h>

#if defined(__TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__) || defined(__TARGET_BOARD_IMX8MP_SOC__)

#define PLL_1443X_RATE(_rate, _m, _p, _d, _k) {(_rate), (_m), (_p), (_s), (_k)}

#define PLL_1416X_RATE(_rate, _m, _p, _s) {(_rate), (_m), (_p), (_s), 0}

#define DIV_CTL0 0x4
#define DIV_CTL1 0x8
#define GNRL_CTL 0x0
#define KDIV_MIN INT16_MIN
#define KDIV_MAX INT16_MAX

/* hardcoded clock source */
#define IMX8MP_CLK_OSC_24M 24000000UL
/* low powered devices use this oscillation */
#define IMX8MP_CLK_OSC_32K 32768UL

#define LOCK_TIMEOUT_US 10000

enum plltype { IMX_1416_PLL, IMX_1443_PLL, IMX_1443_DRAM_PLL };

struct imx_pll14xx_rate_table _imx_pll1416x_tbl[14];
struct imx_pll14xx_rate_table _imx_pll1443x_tbl[4];

#define _IMX_PLL1416X_FILLUP(n, _rate, _pdiv, _mdiv, _sdiv, _kdiv)                                 \
	_imx_pll1416x_tbl[n].rate = _rate;                                                             \
	_imx_pll1416x_tbl[n].pdiv = _pdiv;                                                             \
	_imx_pll1416x_tbl[n].mdiv = _mdiv;                                                             \
	_imx_pll1416x_tbl[n].sdiv = _sdiv;                                                             \
	_imx_pll1416x_tbl[n].kdiv = _kdiv;

#define _IMX_PLL1443X_FILLUP(n, _rate, _pdiv, _mdiv, _sdiv, _kdiv)                                 \
	_imx_pll1443x_tbl[n].rate = _rate;                                                             \
	_imx_pll1443x_tbl[n].pdiv = _pdiv;                                                             \
	_imx_pll1443x_tbl[n].mdiv = _mdiv;                                                             \
	_imx_pll1443x_tbl[n].sdiv = _sdiv;                                                             \
	_imx_pll1443x_tbl[n].kdiv = _kdiv;

/**
 * @brief _imx_pll14xx_fill_table -- fill up predefined
 * values, @reference uboot, linux
 */
void _imx_pll14xx_fill_table() {
	_IMX_PLL1416X_FILLUP(0, 1800000000U, 225, 3, 0, 0);
	_IMX_PLL1416X_FILLUP(1, 1600000000U, 200, 3, 0, 0);
	_IMX_PLL1416X_FILLUP(2, 1500000000U, 375, 3, 1, 0);
	_IMX_PLL1416X_FILLUP(3, 1400000000U, 350, 3, 1, 0);
	_IMX_PLL1416X_FILLUP(4, 1200000000U, 300, 3, 1, 0);
	_IMX_PLL1416X_FILLUP(5, 1000000000U, 250, 3, 1, 0);
	_IMX_PLL1416X_FILLUP(6, 800000000U, 200, 3, 1, 0);
	_IMX_PLL1416X_FILLUP(7, 750000000U, 250, 2, 2, 0);
	_IMX_PLL1416X_FILLUP(8, 700000000U, 350, 3, 2, 0);
	_IMX_PLL1416X_FILLUP(9, 640000000U, 320, 3, 2, 0);
	_IMX_PLL1416X_FILLUP(10, 600000000U, 300, 3, 2, 0);
	_IMX_PLL1416X_FILLUP(11, 416000000U, 208, 3, 2, 0);
	_IMX_PLL1416X_FILLUP(12, 320000000U, 160, 3, 2, 0);
	_IMX_PLL1416X_FILLUP(13, 208000000U, 208, 3, 3, 0);

	_IMX_PLL1443X_FILLUP(0, 1039500000U, 173, 2, 1, 16384);
	_IMX_PLL1443X_FILLUP(1, 650000000U, 325, 3, 2, 0);
	_IMX_PLL1443X_FILLUP(2, 594000000U, 198, 2, 2, 0);
	_IMX_PLL1443X_FILLUP(3, 519750000U, 13, 2, 2, 16384);
}

void imx8mp_pll_init() {
	_imx_pll14xx_fill_table();
	BPrintK(BORDOISILA_INFO,
			"PLL registry initialized can be used for manual configuration of PLLs \r\n");
}

/**
 * @brief imx8mp_get_pll_type -- hard coded pll type values,
 * nxp imx8mp soc uses two type of PLL for precised value.
 * this function take the pll entry and return which type
 * of pll it uses.
 * @reference : linux source tree
 */
uint8_t imx8mp_get_pll_type(uint64_t pllbase) {
	switch (pllbase) {
	case __IMX8MP_AUDIO_PLL1_GEN_CTRL:
		return IMX_1443_PLL;
	case __IMX8MP_AUDIO_PLL2_GEN_CTRL:
		return IMX_1443_PLL;
	case __IMX8MP_VIDEO_PLL1_GEN_CTRL:
		return IMX_1443_PLL;
	case __IMX8MP_DRAM_PLL_GEN_CTRL:
		return IMX_1443_DRAM_PLL;
	case __IMX8MP_GPU_PLL_GEN_CTRL:
		return IMX_1416_PLL;
	case __IMX8MP_VPU_PLL_GEN_CTRL:
		return IMX_1416_PLL;
	case __IMX8MP_SYS_PLL1_GEN_CTRL:
		return IMX_1416_PLL;
	case __IMX8MP_SYS_PLL2_GEN_CTRL:
		return IMX_1416_PLL;
	case __IMX8MP_SYS_PLL3_GEN_CTRL:
		return IMX_1416_PLL;
	}
	return 0;
}

static struct imx_pll14xx_rate_table* imx_get_pll_settings(uint64_t base, unsigned long rate) {
	uint8_t type = imx8mp_get_pll_type(base);
	if (type == IMX_1416_PLL) {
		for (int i = 0; i < 14; i++) {
			if (rate == _imx_pll1416x_tbl[i].rate)
				return &_imx_pll1416x_tbl[i];
		}
	} else {
		for (int i = 0; i < 4; i++) {
			if (rate == _imx_pll1443x_tbl[i].rate)
				return &_imx_pll1443x_tbl[i];
		}
	}
	return NULL;
}

static long imx8mp_pll14xx_calc_rate(int mdiv, int pdiv, int sdiv, int kdiv, unsigned long prate) {
	uint64_t fout = prate;

	fout *= (mdiv * 65536 + kdiv);
	pdiv *= 65536;

	do_div(fout, pdiv << sdiv);

	return fout;
}

static long
imx8mp_pll1443x_calc_kdiv(int mdiv, int pdiv, int sdiv, unsigned long rate, unsigned long prate) {
	long kdiv;
	kdiv = ((rate * ((pdiv * 65536) << sdiv) + prate / 2) / prate) - (mdiv * 65536);
	return clamp_t(short, kdiv, KDIV_MIN, KDIV_MAX);
}

/**
 * @brief imx8mp_pll_recalc_rate -- recalculate pll rate
 * @param pllbase -- pll base source or entry
 * @param parent_rate -- clock rate, if the target root is high powered
 * device, it uses osc_24m sel otherwise osc_32k is used for low powered
 * device
 */
unsigned long imx8mp_pll_recalc_rate(uint64_t pllbase, unsigned long parent_rate) {
	uint32_t mdiv, pdiv, sdiv, plldiv, plldiv_ctl1, kdiv;

	plldiv = _bordoisila_readl(pllbase + DIV_CTL0);
	mdiv = BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_MDIV_MASK, plldiv);
	pdiv = BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_PDIV_MASK, plldiv);
	sdiv = BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_SDIV_MASK, plldiv);

	uint8_t type = imx8mp_get_pll_type(pllbase);
	if (type == IMX_1443_PLL) {
		plldiv_ctl1 = _bordoisila_readl(pllbase + DIV_CTL1);
		kdiv = (int16_t)BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_KDIV_MASK, plldiv_ctl1);
	} else
		kdiv = 0;

	return imx8mp_pll14xx_calc_rate(mdiv, pdiv, sdiv, kdiv, parent_rate);
}

static inline bool imx8mp_pll14xx_mp_change(const struct imx_pll14xx_rate_table* rate,
											uint32_t plldiv) {
	uint32_t old_mdiv, old_pdiv;

	old_mdiv = BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_MDIV_MASK, plldiv);
	old_pdiv = BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_PDIV_MASK, plldiv);

	return rate->mdiv != old_mdiv || rate->pdiv != old_pdiv;
}

static void imx_pll14xx_calc_settings(uint64_t base,
									  unsigned long rate,
									  unsigned long prate,
									  struct imx_pll14xx_rate_table* t) {
	uint32_t pll_div_ctl0, pll_div_ctl1;
	int mdiv, pdiv, sdiv, kdiv;

	long fout, rate_min, rate_max, dist, best = LONG_MAX;

	const struct imx_pll14xx_rate_table* tt;
	tt = imx_get_pll_settings(base, rate);
	if (tt) {
		AuTextOut(
			"[imx_pll14xx_calc_settings]: in=%d, want = %d, using pll settings from table \r\n",
			prate,
			rate);
		t->rate = tt->rate;
		t->mdiv = tt->mdiv;
		t->pdiv = tt->pdiv;
		t->sdiv = tt->sdiv;
		t->kdiv = tt->kdiv;
		return;
	}

	pll_div_ctl0 = _bordoisila_readl(base + DIV_CTL0);
	mdiv = BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_MDIV_MASK, pll_div_ctl0);
	pdiv = BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_PDIV_MASK, pll_div_ctl0);
	sdiv = BORDOISILA_GET_FIELD(IMX8MP_PLL14XX_SDIV_MASK, pll_div_ctl0);
	pll_div_ctl1 = _bordoisila_readl(base + DIV_CTL1);

	rate_min = imx8mp_pll14xx_calc_rate(mdiv, pdiv, sdiv, KDIV_MIN, prate);
	rate_max = imx8mp_pll14xx_calc_rate(mdiv, pdiv, sdiv, KDIV_MAX, prate);

	if (rate >= rate_min && rate <= rate_max) {
		kdiv = imx8mp_pll1443x_calc_kdiv(mdiv, pdiv, sdiv, rate, prate);
		fout = imx8mp_pll14xx_calc_rate(mdiv, pdiv, sdiv, kdiv, prate);
		t->rate = (unsigned int)fout;
		t->mdiv = mdiv;
		t->pdiv = pdiv;
		t->sdiv = sdiv;
		t->kdiv = kdiv;
		return;
	}

	/* finally calculate best values */
	for (pdiv = 1; pdiv <= 63; pdiv++) {
		for (sdiv = 0; sdiv <= 6; sdiv++) {
			mdiv = DIV_ROUND_CLOSEST(rate * (pdiv << sdiv), prate);
			mdiv = CLAMP(mdiv, 64, 1023);

			kdiv = imx8mp_pll1443x_calc_kdiv(mdiv, pdiv, sdiv, rate, prate);
			fout = imx8mp_pll14xx_calc_rate(mdiv, pdiv, sdiv, kdiv, prate);

			dist = ABS((long)rate - (long)fout);
			if (dist < best) {
				best = dist;
				t->rate = (unsigned int)fout;
				t->mdiv = mdiv;
				t->pdiv = pdiv;
				t->sdiv = sdiv;
				t->kdiv = kdiv;

				if (!dist)
					goto found;
			}
		}
	}
found:
	AuTextOut("imx8mp_pll1416x rate found \r\n");
}

int imx8mp_pll1416x_determine_rate(uint64_t base, unsigned long rate) {
	uint8_t type = imx8mp_get_pll_type(base);
	if (type == IMX_1416_PLL) {
		for (int i = 0; i < 14; i++) {
			if (rate >= _imx_pll1416x_tbl[i].rate) {
				rate = _imx_pll1416x_tbl[i].rate;
				return rate;
			}
		}
		rate = _imx_pll1416x_tbl[14 - 1].rate;
		return rate;
	} else {
		for (int i = 0; i < 4; i++) {
			if (rate >= _imx_pll1443x_tbl[i].rate) {
				rate = _imx_pll1443x_tbl[i].rate;
				return 0;
			}
		}

		rate = _imx_pll1443x_tbl[14 - 1].rate;
		return rate;
	}
	return 0;
}

int imx8mp_pll1416x_set_rate(uint64_t base, unsigned long drate, unsigned long prate) {
	uint32_t tmp, div_val;

	struct imx_pll14xx_rate_table* rate = imx_get_pll_settings(base, drate);
	if (!rate) {
		AuTextOut("[bordoisila]: imx8mp pll invalid rate %d \r\n", drate);
		return 0;
	}

	tmp = _bordoisila_readl(base + DIV_CTL0);

	if (!imx8mp_pll14xx_mp_change(rate, tmp)) {
		tmp &= ~IMX8MP_PLL14XX_SDIV_MASK;
		tmp |= BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_SDIV_MASK, rate->sdiv);
		_bordoisila_writel(tmp, base + DIV_CTL0);
		return 0;
	}

	/* bypass clock and set lock to pll output lock*/
	tmp = _bordoisila_readl(base + GNRL_CTL);
	tmp |= LOCK_SEL_MASK;
	_bordoisila_writel(tmp, base + GNRL_CTL);

	/* enable RST */
	tmp &= ~RST_MASK;
	_bordoisila_writel(tmp, base + GNRL_CTL);

	/* enable bypass*/
	tmp |= BYPASS_MASK;
	_bordoisila_writel(tmp, base + GNRL_CTL);

	div_val = BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_MDIV_MASK, rate->mdiv) |
			  BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_PDIV_MASK, rate->pdiv) |
			  BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_SDIV_MASK, rate->sdiv);
	_bordoisila_writel(div_val, base + DIV_CTL0);

	AuAA64BoardSleepUS(3);

	/* disable RST */
	tmp |= RST_MASK;
	_bordoisila_writel(tmp, base + GNRL_CTL);

	int ret = imx8mp_wait_lock(base);
	if (ret)
		return ret;

	tmp &= ~BYPASS_MASK;
	_bordoisila_writel(tmp, base + GNRL_CTL);

	return 0;
}

int imx8mp_pll1443x_set_rate(uint64_t base, unsigned long drate, unsigned long prate) {
	struct imx_pll14xx_rate_table rate;
	uint32_t gnrl_ctl, div_ctl0;
	imx_pll14xx_calc_settings(base, drate, prate, &rate);

	div_ctl0 = _bordoisila_readl(base + DIV_CTL0);

	if (!imx8mp_pll14xx_mp_change(&rate, div_ctl0)) {
		div_ctl0 &= ~IMX8MP_PLL14XX_SDIV_MASK;
		div_ctl0 |= BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_SDIV_MASK, rate.sdiv);
		_bordoisila_writel(div_ctl0, base + DIV_CTL0);
		_bordoisila_writel(BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_KDIV_MASK, rate.kdiv),
						   base + DIV_CTL1);
		return 0;
	}

	/* enable rst */
	gnrl_ctl = _bordoisila_readl(base + GNRL_CTL);
	gnrl_ctl &= ~RST_MASK;
	_bordoisila_writel(gnrl_ctl, base + GNRL_CTL);

	/* enable bypass */
	gnrl_ctl |= BYPASS_MASK;
	_bordoisila_writel(gnrl_ctl, base + GNRL_CTL);

	div_ctl0 = BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_MDIV_MASK, rate.mdiv) |
			   BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_PDIV_MASK, rate.pdiv) |
			   BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_SDIV_MASK, rate.sdiv);
	_bordoisila_writel(div_ctl0, base + DIV_CTL0);

	_bordoisila_writel(BORDOISILA_PREP_FIELD(IMX8MP_PLL14XX_KDIV_MASK, rate.kdiv), base + DIV_CTL0);

	AuAA64BoardSleepUS(3);

	/* disable rst*/
	gnrl_ctl |= RST_MASK;
	_bordoisila_writel(gnrl_ctl, base + GNRL_CTL);

	int ret = imx8mp_wait_lock(base);
	if (ret)
		return ret;

	/* bypass */
	gnrl_ctl &= ~BYPASS_MASK;
	_bordoisila_writel(gnrl_ctl, base + GNRL_CTL);

	return 0;
}

#define PLL_REF_SEL_MASK  0x3
#define PLL_REF_SEL_SHIFT 0

uint32_t imx8mp_pll_get_parent_rate(uint64_t pll_base) {
	uint32_t gnrl_ctl = _bordoisila_readl(pll_base + GNRL_CTL);
	uint32_t ref_sel = BORDOISILA_GET_FIELD(PLL_REF_SEL_MASK, gnrl_ctl);

	switch (ref_sel) {
	case 0:
		return IMX8MP_CLK_OSC_24M;
	default:
		BPrintK(BORDOISILA_WARN,
				"imx8mp pll_ref selection unexpected value : %u for base : %x \r\n",
				ref_sel,
				pll_base);
		return IMX8MP_CLK_OSC_24M;
	}
}

int imx8mp_wait_lock(uint64_t base) {
	uint32_t val;
	return _bordoisila_readl_poll_timeout(base + GNRL_CTL, val & LOCK_STATUS, 0, LOCK_TIMEOUT_US);
}

void imx8mp_pll_prepare(uint64_t base) {
	uint32_t val;

	val = _bordoisila_readl(base + GNRL_CTL);
	if (val & RST_MASK) {
		AuTextOut("[aurora]: imx8mp_pll_prepare: reset mask is already set for base : %x \r\n",
				  base);
		return;
	}
	val |= BYPASS_MASK;
	_bordoisila_writel(val, base + GNRL_CTL);
	val |= RST_MASK;
	_bordoisila_writel(val, base + GNRL_CTL);

	// wait lock
	imx8mp_wait_lock(base);

	val &= ~BYPASS_MASK;
	_bordoisila_writel(val, base + GNRL_CTL);
}

#endif