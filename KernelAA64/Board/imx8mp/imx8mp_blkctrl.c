/**
* @file imx8mp_blkctrl.c
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

#if defined(__TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__) || defined(__TARGET_BOARD_IMX8MP_SOC__)

#include <Board/imx8mp/imx8mp_blkctrl.h>
#include <bordoisila_bits.h>
#include <bordoisila_io.h>
#include <_null.h>
#include <Log/klog.h>
#include <Drivers/core.h>
#include <Drivers/res.h>
#include <Mm/kmalloc.h>
#include <string.h>
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>

#define IMX8MP_BLK_CTRL_DDR	  0x3D000000
#define IMX8MP_BLK_CTRL_HSIO  0x32F10000
#define IMX8MP_BLK_CTRL_MEDIA 0x32EC0000
#define IMX8MP_BLK_CTRLAUDIO  0x30E20000
#define IMX8MP_BLK_CTRL_VPU	  0x38330000
#define IMX8MP_BLK_CTRL_HDMI  0x32FC0000

#define BLK_SFT_RSTN	   0x0
#define BLK_CLK_EN		   0x4
#define BLK_MIPI_RESET_DIV 0x8

static uint64_t blk_ctl_hdmi;

typedef struct _imx8mp_blk_map_ {
	int blk_id;
	uint64_t reg;
	uint32_t rst_mask;
	uint32_t clk_mask;
} imx8mp_blk_map;

static imx8mp_blk_map _map[20];

#define BLK_MAP_ENTRY(n, _id, _reg, _rst_mask, _clk_mask)                                          \
	_map[n].blk_id = _id;                                                                          \
	_map[n].reg = _reg;                                                                            \
	_map[n].rst_mask = _rst_mask;                                                                  \
	_map[n].clk_mask = _clk_mask;

static int _blkctrl_poweron(BordoisilaPower* pwr) {
	if (!pwr)
		return 1;

	if (!pwr->res.data)
		return 1;

	if (pwr->res.is_running)
		return 1;

	imx8mp_blk_map* blk = (imx8mp_blk_map*)pwr->res.data;
	if (imx8mp_blkctl_powerup(blk->blk_id))
		return 1;

	if (imx8mp_blkctrl_release_reset(blk->blk_id))
		return 1;

	pwr->res.is_running = 1;
	return 0;
}

static int _blkctrl_powerdn(BordoisilaPower* pwr) {
	BPrintK(BORDOISILA_WARN, "blkctrl imx8mp power down not implemented \r\n");
	return 0;
}

static void imx8mp_blk_create_ke_resource(char* name, void* data) {
	BordoisilaPower* pwr = (BordoisilaPower*)kmalloc(sizeof(BordoisilaPower));
	strcpy(pwr->res.name, name);
	pwr->res.is_running = false;
	pwr->res.res_type = BORDOISILA_DRIVER_RES_POWER;
	pwr->res.data = data;
	pwr->power_on = _blkctrl_poweron;
	pwr->power_down = _blkctrl_powerdn;
	if (BordoisilaDriverResourceRegister((BordoisilaDriverResource*)pwr)) {
		BPrintK(BORDOISILA_ERROR, "failed to register kernel power resource : %s \r\n", name);
	}
}

/**
 * @brief imx8mp_blkctl_init -- register the block control
 * registry, for now, only MEDIA domain is entered
 */
void imx8mp_blkctrl_init() {
	for (int i = 0; i < 20; i++)
		memset(&_map[i], 0, sizeof(imx8mp_blk_map));

	uint64_t blk_ctl_ddr = (uint64_t)AuMapMMIO(IMX8MP_BLK_CTRL_DDR, 1);
	uint64_t blk_ctl_audio = (uint64_t)AuMapMMIO(IMX8MP_BLK_CTRLAUDIO, 1);
	blk_ctl_hdmi = (uint64_t)AuMapMMIO(IMX8MP_BLK_CTRL_HDMI, 1);
	uint64_t blk_ctl_hsio = (uint64_t)AuMapMMIO(IMX8MP_BLK_CTRL_HSIO, 1);
	uint64_t blk_ctl_media = (uint64_t)AuMapMMIO(IMX8MP_BLK_CTRL_MEDIA, 1);
	uint64_t blk_ctl_vpu = (uint64_t)AuMapMMIO(IMX8MP_BLK_CTRL_VPU, 1);

	UARTDebugOut("BLK_CTL_DDR : %x \r\n", blk_ctl_ddr);
	UARTDebugOut("BLK_CTL_AUDIO : %x \r\n", blk_ctl_audio);
	UARTDebugOut("BLK_CTL_HDMI : %x \r\n", blk_ctl_hdmi);
	UARTDebugOut("BLK_CTL_HSIO : %x \r\n", blk_ctl_hsio);
	UARTDebugOut("BLK_CTL_MEDIA : %x \r\n", blk_ctl_media);
	UARTDebugOut("BLK_CTL_VPU : %x \r\n", blk_ctl_vpu);

	int n = 0;

	BLK_MAP_ENTRY(n,
				  IMX8MP_MEDIABLK_PD_MIPI_DSI_1,
				  blk_ctl_media,
				  BORDOISILA_BIT(0) | BORDOISILA_BIT(1),
				  BORDOISILA_BIT(0) | BORDOISILA_BIT(1));

	imx8mp_blk_create_ke_resource("mediablk_pd_mipi_dsi_1", &_map[n]);
	n++;

	BLK_MAP_ENTRY(n,
				  IMX8MP_MEDIABLK_PD_MIPI_CSI2_1,
				  blk_ctl_media,
				  BORDOISILA_BIT(2) | BORDOISILA_BIT(3),
				  BORDOISILA_BIT(2) | BORDOISILA_BIT(3));
	imx8mp_blk_create_ke_resource("mediablk_pd_mipi_csi2_1", &_map[n]);
	n++;

	BLK_MAP_ENTRY(n,
				  IMX8MP_MEDIABLK_PD_LCDIF_1,
				  blk_ctl_media,
				  BORDOISILA_BIT(4) | BORDOISILA_BIT(5) | BORDOISILA_BIT(23),
				  BORDOISILA_BIT(4) | BORDOISILA_BIT(5) | BORDOISILA_BIT(23));

	imx8mp_blk_create_ke_resource("mediablk_pd_lcdif1", &_map[n]);
	n++;

	BLK_MAP_ENTRY(n,
				  IMX8MP_MEDIABLK_PD_ISI,
				  blk_ctl_media,
				  BORDOISILA_BIT(6) | BORDOISILA_BIT(7),
				  BORDOISILA_BIT(6) | BORDOISILA_BIT(7));

	imx8mp_blk_create_ke_resource("mediablk_pd_isi", &_map[n]);
	n++;

	BLK_MAP_ENTRY(n,
				  IMX8MP_MEDIABLK_PD_MIPI_CSI2_2,
				  blk_ctl_media,
				  BORDOISILA_BIT(9) | BORDOISILA_BIT(10),
				  BORDOISILA_BIT(9) | BORDOISILA_BIT(10));

	imx8mp_blk_create_ke_resource("mediablk_pd_mipi_csi2_2", &_map[n]);
	n++;

	BLK_MAP_ENTRY(n,
				  IMX8MP_MEDIABLK_PD_LCDIF_2,
				  blk_ctl_media,
				  BORDOISILA_BIT(11) | BORDOISILA_BIT(12) | BORDOISILA_BIT(24),
				  BORDOISILA_BIT(11) | BORDOISILA_BIT(12) | BORDOISILA_BIT(24));
	imx8mp_blk_create_ke_resource("mediablk_pd_lcdif_2", &_map[n]);
	n++;

	BLK_MAP_ENTRY(n,
				  IMX8MP_MEDIABLK_PD_ISP,
				  blk_ctl_media,
				  BORDOISILA_BIT(16) | BORDOISILA_BIT(17) | BORDOISILA_BIT(18),
				  BORDOISILA_BIT(16) | BORDOISILA_BIT(17) | BORDOISILA_BIT(18));
	imx8mp_blk_create_ke_resource("mediablk_pd_isp", &_map[n]);
	n++;

	BLK_MAP_ENTRY(n,
				  IMX8MP_MEDIABLK_PD_DWE,
				  blk_ctl_media,
				  BORDOISILA_BIT(19) | BORDOISILA_BIT(20) | BORDOISILA_BIT(21),
				  BORDOISILA_BIT(19) | BORDOISILA_BIT(20) | BORDOISILA_BIT(21));
	imx8mp_blk_create_ke_resource("mediablk_pd_dwe", &_map[n]);
	n++;

	BLK_MAP_ENTRY(
		n, IMX8MP_MEDIABLK_PD_MIPI_DSI_2, blk_ctl_media, BORDOISILA_BIT(22), BORDOISILA_BIT(22));
	imx8mp_blk_create_ke_resource("mediablk_pd_mipi_dsi_2", &_map[n]);

	n++;
	BLK_MAP_ENTRY(n, IMX8MP_HDMIBLK_PD_IRQSTEER, blk_ctl_hdmi, 0, 0);
	imx8mp_blk_create_ke_resource("hdmiblk_pd_irqsteer", &_map[n]);

	n++;

	BLK_MAP_ENTRY(n, IMX8MP_HDMIBLK_PD_LCDIF, blk_ctl_hdmi, 0, 0);
	imx8mp_blk_create_ke_resource("hdmiblk_pd_lcdif", &_map[n]);

	n++;

	BLK_MAP_ENTRY(n, IMX8MP_HDMIBLK_PD_PAI, blk_ctl_hdmi, 0, 0);
	imx8mp_blk_create_ke_resource("hdmiblk_pd_pai", &_map[n]);

	n++;

	BLK_MAP_ENTRY(n, IMX8MP_HDMIBLK_PD_PVI, blk_ctl_hdmi, 0, 0);
	imx8mp_blk_create_ke_resource("hdmiblk_pd_pvi", &_map[n]);

	n++;

	BLK_MAP_ENTRY(n, IMX8MP_HDMIBLK_PD_TRNG, blk_ctl_hdmi, 0, 0);
	imx8mp_blk_create_ke_resource("hdmiblk_pd_trng", &_map[n]);

	n++;

	BLK_MAP_ENTRY(n, IMX8MP_HDMIBLK_PD_HDMI_TX, blk_ctl_hdmi, 0, 0);
	imx8mp_blk_create_ke_resource("hdmiblk_pd_hdmi_tx", &_map[n]);

	n++;

	BLK_MAP_ENTRY(n, IMX8MP_HDMIBLK_PD_HDMI_TX_PHY, blk_ctl_hdmi, 0, 0);
	imx8mp_blk_create_ke_resource("hdmiblk_pd_hdmi_tx_phy", &_map[n]);

	n++;

	BLK_MAP_ENTRY(n, IMX8MP_HDMIBLK_PD_HRV, blk_ctl_hdmi, 0, 0);
	imx8mp_blk_create_ke_resource("hdmiblk_pd_hrv", &_map[n]);
}

static int imx8mp_hdmi_poweron(imx8mp_blk_map* domain, uint32_t id);

static imx8mp_blk_map* imx8mp_blkctl_find(uint32_t id) {
	for (int i = 0; i < 20; i++) {
		if (_map[i].blk_id == id) {
			return &_map[i];
		}
	}

	return NULL;
}

int imx8mp_blkctl_powerup(uint32_t id) {
	imx8mp_blk_map* domain = imx8mp_blkctl_find(id);
	if (!domain) {
		BPrintK(BORDOISILA_ERROR, "imx8mp-blk-ctl id : %d, not found \r\n", id);
		return 1;
	}

	/** TODO: take care of the reg if it's mapped using AuMapMMIO **/
	if (domain->reg == blk_ctl_hdmi) {
		return imx8mp_hdmi_poweron(domain, id);
	}

	BPrintK(BORDOISILA_DEBUG, "BLK CTRL power up : %x \r\n", domain->reg);
	uint32_t bus_clk = _bordoisila_readl((uint64_t)domain->reg + BLK_CLK_EN);
	BPrintK(BORDOISILA_DEBUG, "blkctl bus val : %x, addr : %x \r\n", (domain->reg + BLK_CLK_EN));
	bus_clk |= BORDOISILA_BIT(8);
	_bordoisila_writel(bus_clk, (uint64_t)domain->reg + BLK_CLK_EN);

	uint32_t bus_rstn = _bordoisila_readl((uint64_t)domain->reg + BLK_SFT_RSTN);
	BPrintK(
		BORDOISILA_DEBUG, "bus rstn : %x , addr : %x \r\n", bus_rstn, (domain->reg + BLK_SFT_RSTN));
	bus_rstn |= BORDOISILA_BIT(8);
	_bordoisila_writel(bus_rstn, (uint64_t)domain->reg + BLK_SFT_RSTN);

	/** device reset assert **/
	uint32_t rstn = _bordoisila_readl((uint64_t)domain->reg + BLK_SFT_RSTN);
	BPrintK(BORDOISILA_DEBUG, "rstn : %x , addr : %x \r\n", rstn, (domain->reg + BLK_SFT_RSTN));
	rstn &= ~domain->rst_mask;
	_bordoisila_writel(rstn, (uint64_t)domain->reg + BLK_SFT_RSTN);

	uint32_t clken = _bordoisila_readl((uint64_t)domain->reg + BLK_CLK_EN);
	clken |= domain->clk_mask;
	_bordoisila_writel(clken, (uint64_t)domain->reg + BLK_CLK_EN);

	for (int i = 0; i < 100000; i++)
		;

	BPrintK(BORDOISILA_INFO, "imx8mp-blkctrl: id : %d powered properly \r\n", domain->blk_id);
	return 0;
}

int imx8mp_blkctrl_release_reset(uint32_t id) {
	imx8mp_blk_map* domain = imx8mp_blkctl_find(id);
	if (!domain) {
		BPrintK(BORDOISILA_ERROR, "imx8mp-blk-ctl id : %d, not found \r\n", id);
		return 1;
	}

	if (domain->reg == IMX8MP_BLK_CTRL_HDMI)
		return 0;

	uint32_t rstn = _bordoisila_readl((uint64_t)domain->reg + BLK_SFT_RSTN);
	rstn |= domain->rst_mask;
	_bordoisila_writel(rstn, (uint64_t)domain->reg + BLK_SFT_RSTN);

	BPrintK(BORDOISILA_INFO, "imx8mp-blkctrl: id : %d reset deasserted \r\n", domain->blk_id);
	return 0;
}

/**
 * =======================================================================
 * HDMI BLK CONTROL Management
 *========================================================================
 */

#define HDMI_RTX_RESET_CTL0		  0x20
#define HDMI_RTX_CLK_CTL0		  0x40
#define HDMI_RTX_CLK_CTL1		  0x50
#define HDMI_RTX_CLK_CTL2		  0x60
#define HDMI_RTX_CLK_CTL3		  0x70
#define HDMI_RTX_CLK_CTL4		  0x80
#define HDMI_TX_CONTROL0		  0x200
#define HDMI_LCDIF_NOC_HURRY_MASK BORDOISILA_GENMASK(14, 12)

static void imx8mp_hdmi_bus_init(BordoisilaPower* power) {
	if (!power)
		return;
	imx8mp_blk_map* domain = (imx8mp_blk_map*)power->res.data;
	if (!domain)
		return;
	UARTDebugOut("HDMI domain reg : %x power name : %s\r\n", domain->reg, power->res.name);
	_bordoisila_writel(0, blk_ctl_hdmi + HDMI_RTX_RESET_CTL0);
	UARTDebugOut("First line written\r\n");
	_bordoisila_writel(0, blk_ctl_hdmi + HDMI_RTX_CLK_CTL0);
	UARTDebugOut("Second line written \r\n");
	_bordoisila_writel(0, blk_ctl_hdmi + HDMI_RTX_CLK_CTL1);
	UARTDebugOut("Third line written \r\n");

	uint32_t v = _bordoisila_readl(blk_ctl_hdmi + HDMI_RTX_CLK_CTL0);
	UARTDebugOut("V read : %x \r\n");
	v |= BORDOISILA_BIT(0) | BORDOISILA_BIT(1) | BORDOISILA_BIT(10) | BORDOISILA_BIT(11);
	_bordoisila_writel(v, blk_ctl_hdmi + HDMI_RTX_CLK_CTL0);
	UARTDebugOut("V written \r\n");

	uint32_t r = _bordoisila_readl(blk_ctl_hdmi + HDMI_RTX_RESET_CTL0);
	UARTDebugOut("R read \r\n");
	r |= BORDOISILA_BIT(0);
	_bordoisila_writel(r, blk_ctl_hdmi + HDMI_RTX_RESET_CTL0);
	UARTDebugOut("RESET CTL written \r\n");
	for (int i = 0; i < 100000; i++)
		;
}

/**
 * @brief hdmi_parent_bus_callback -- before ADB-400 handshake could 
 * received from HDMIMIX power, force bus is required or else it wouldn't
 * power up properly
 */
int hdmi_parent_bus_callback(BordoisilaPower* power) {
	BPrintK(BORDOISILA_INFO, "imx8mp enabling hdmi parent bus \r\n");
	imx8mp_hdmi_bus_init(power);
	return 0;
}

static int imx8mp_hdmi_poweron(imx8mp_blk_map* domain, uint32_t id) {
	switch (id) {
	case IMX8MP_HDMIBLK_PD_IRQSTEER: {
		BPrintK(BORDOISILA_DEBUG, "imx8mp hdmi irqsteer powering up ...\r\n");
		uint32_t ctl0 = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);
		ctl0 |= BORDOISILA_BIT(9);
		_bordoisila_writel(ctl0, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);

		uint32_t rst = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		rst |= BORDOISILA_BIT(16);
		_bordoisila_writel(rst, (uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		BPrintK(BORDOISILA_DEBUG, "imx8mp hdmi irqsteer powered up \r\n");
		break;
	}
	case IMX8MP_HDMIBLK_PD_LCDIF: {
		BPrintK(BORDOISILA_DEBUG, "imx8mp hdmi lcdif powering up ... %x\r\n", domain->reg);
		uint32_t bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);
		bit |= BORDOISILA_BIT(4) | BORDOISILA_BIT(16) | BORDOISILA_BIT(17) | BORDOISILA_BIT(18) |
			   BORDOISILA_BIT(19) | BORDOISILA_BIT(20);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);
		bit |= BORDOISILA_BIT(11);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);

		for (int i = 0; i < 10000; i++)
			;

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		bit |= BORDOISILA_BIT(4) | BORDOISILA_BIT(5) | BORDOISILA_BIT(6);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_TX_CONTROL0);
		bit |= BORDOISILA_PREP_FIELD(HDMI_LCDIF_NOC_HURRY_MASK, 7);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_TX_CONTROL0);

		for (int i = 0; i < 10000; i++)
			;

		BPrintK(BORDOISILA_DEBUG, "imx8mp hdmi lcdif powered up \r\n");
		break;
	}
	case IMX8MP_HDMIBLK_PD_PAI: {
		uint32_t bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);
		bit |= BORDOISILA_BIT(17);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		bit |= BORDOISILA_BIT(18);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		break;
	}
	case IMX8MP_HDMIBLK_PD_PVI: {
		uint32_t bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);
		bit |= BORDOISILA_BIT(28);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		bit |= BORDOISILA_BIT(22);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		break;
	}
	case IMX8MP_HDMIBLK_PD_TRNG:
		break;

	case IMX8MP_HDMIBLK_PD_HDMI_TX: {
		BPrintK(BORDOISILA_DEBUG, "imx8mp hdmi tx powering on ... %x\r\n", domain->reg);
		uint32_t bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);
		bit |= BORDOISILA_BIT(2) | BORDOISILA_BIT(4) | BORDOISILA_BIT(5);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);
		bit |= BORDOISILA_BIT(12) | BORDOISILA_BIT(13) | BORDOISILA_BIT(14) | BORDOISILA_BIT(15) |
			   BORDOISILA_BIT(16) | BORDOISILA_BIT(18) | BORDOISILA_BIT(19) | BORDOISILA_BIT(20) |
			   BORDOISILA_BIT(21);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		bit |= BORDOISILA_BIT(7) | BORDOISILA_BIT(10) | BORDOISILA_BIT(11);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_TX_CONTROL0);
		bit |= BORDOISILA_BIT(1);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_TX_CONTROL0);
		BPrintK(BORDOISILA_DEBUG, "imx8mp hdmi tx powered up \r\n");
		break;
	}

	case IMX8MP_HDMIBLK_PD_HDMI_TX_PHY: {
		uint32_t bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);
		bit |= BORDOISILA_BIT(7);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL0);
		UARTDebugOut("HDMI_RTX_CLK_CTL0 final : %x \r\n", bit);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);
		bit |= BORDOISILA_BIT(22) | BORDOISILA_BIT(24);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_CLK_CTL1);
		UARTDebugOut("HDMI_RTX_CLK_CTL1 final : %x \r\n", bit);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		bit |= BORDOISILA_BIT(12);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_RTX_RESET_CTL0);
		UARTDebugOut("HDMI_RTX_RESET_CTL0 final : %x \r\n", bit);

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_TX_CONTROL0);
		bit &= ~BORDOISILA_BIT(3);
		bit |= BORDOISILA_BIT(2);
		bit |= BORDOISILA_BIT(1);
		_bordoisila_writel(bit, (uint64_t)domain->reg + HDMI_TX_CONTROL0);
		for (volatile int i = 0; i < 10000; i++)
			;

		bit = _bordoisila_readl((uint64_t)domain->reg + HDMI_TX_CONTROL0);
		UARTDebugOut("HDMI_TX_CONTROL0 final : %x \r\n", bit);

		BPrintK(BORDOISILA_DEBUG, "imx8mp hdmi tx phy powered up \r\n");
		break;
	}
	case IMX8MP_HDMIBLK_PD_HRV:
		break;
	}
	return 0;
}

#endif