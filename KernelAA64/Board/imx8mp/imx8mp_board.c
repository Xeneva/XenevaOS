/**
* @file imx8mp_board.c
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

#include <Board/imx8mp/imx8mp_clk.h>
#include <Board/imx8mp/imx8mp_pll.h>
#include <Board/imx8mp/imx8mp_clk_gate.h>
#include <Board/imx8mp/imx8mp_blkctrl.h>
#include <Board/imx8mp/imx8mp_gpc.h>
#include <Strings/export_imx8mp.h>
#include <Drivers/uart.h>
#include <Board/board.h>
#include <stdint.h>
#include <aucon.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Log/klog.h>
#include <bordoisila_io.h>
#include <Drivers/res.h>

#if defined(__TARGET_BOARD_IMX8MP_VERDIN_DAHLIA__) || defined(__TARGET_BOARD_IMX8MP_SOC__)

/**
 * @brief imx8mp_board_init_defaults -- initialize default
 * expected kernel resources
 */
static void imx8mp_board_init_defaults() {
	BordoisilaClk* axi = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_MEDIA_AXI_NAME,
																	 BORDOISILA_DRIVER_RES_CLK);
	BordoisilaClk* abp = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_MEDIA_APB_NAME,
																	 BORDOISILA_DRIVER_RES_CLK);

	axi->enable(axi, 500000000UL);
	abp->enable(abp, 200000000UL);

	BordoisilaClk* hdmi_axi = (BordoisilaClk*)BordoisilaGetDriverResource(
		IMX8MP_HDMI_AXI_NAME, BORDOISILA_DRIVER_RES_CLK);
	hdmi_axi->enable(hdmi_axi, 500000000UL);

	BordoisilaClk* hdmi_apb = (BordoisilaClk*)BordoisilaGetDriverResource(
		IMX8MP_HDMI_APB_NAME, BORDOISILA_DRIVER_RES_CLK);
	hdmi_apb->enable(hdmi_apb, 133000000UL);

	//BordoisilaClk* disp2_pxl = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_MEDIA_DISP2_PIX_NAME, BORDOISILA_DRIVER_RES_CLK);
	//disp2_pxl->enable(disp2_pxl, 148500000UL);

	BordoisilaClk* hdmi_266 = (BordoisilaClk*)BordoisilaGetDriverResource(
		IMX8MP_HDMI_266M_NAME, BORDOISILA_DRIVER_RES_CLK);
	hdmi_266->enable(hdmi_266, 266666666UL);

	BordoisilaClk* hdmi_24m = (BordoisilaClk*)BordoisilaGetDriverResource(
		IMX8MP_HDMI_24M_NAME, BORDOISILA_DRIVER_RES_CLK);
	hdmi_24m->enable(hdmi_24m, 24000000UL);

	BordoisilaPower* pwr1 = (BordoisilaPower*)BordoisilaGetDriverResource(
		IMX8MP_POWER_MEDIAMIX_NAME, BORDOISILA_DRIVER_RES_POWER);
	pwr1->power_on(pwr1);

	BordoisilaPower* pwr2 = (BordoisilaPower*)BordoisilaGetDriverResource(
		IMX8MP_POWER_LCDIF1_NAME, BORDOISILA_DRIVER_RES_POWER);
	pwr2->power_on(pwr2);

	BordoisilaPower* pwr3 = (BordoisilaPower*)BordoisilaGetDriverResource(
		IMX8MP_POWER_HDMIMIX_NAME, BORDOISILA_DRIVER_RES_POWER);
	pwr3->power_on(pwr3);

	BordoisilaPower* hdmi = (BordoisilaPower*)BordoisilaGetDriverResource(
		IMX8MP_POWER_HDMI_PHY_NAME, BORDOISILA_DRIVER_RES_POWER);
	hdmi->power_on(hdmi);

	BordoisilaPower* pwr5 = (BordoisilaPower*)BordoisilaGetDriverResource(
		IMX8MP_POWER_HDMI_TX_NAME, BORDOISILA_DRIVER_RES_POWER);
	pwr5->power_on(pwr5);

	BordoisilaPower* pwr4 = (BordoisilaPower*)BordoisilaGetDriverResource(
		IMX8MP_POWER_HDMI_LCDIF_NAME, BORDOISILA_DRIVER_RES_POWER);
	pwr4->power_on(pwr4);

	BordoisilaPower* pwr6 = (BordoisilaPower*)BordoisilaGetDriverResource(
		IMX8MP_POWER_HDMI_TX_PHY_NAME, BORDOISILA_DRIVER_RES_POWER);
	pwr6->power_on(pwr6);
}

/**
 * @brief imx8mp_board_initiailze -- initialize required 
 * subsystems of board
 */
void imx8mp_board_initialize() {
	/** initialize the ccm module **/
	imx8mp_gpc_init();
	imx8mp_pll_init();
	imx8mp_blkctrl_init();
	imx8mp_ccm_init();
	imx8mp_gate_init();

	imx8mp_board_init_defaults();
}

#endif