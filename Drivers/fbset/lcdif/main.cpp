/**
* @file lcdif.cpp
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

#include <aurora.h>
#include "imx8mp_lcdif_reg.h"
#include <Hal/AA64/aa64lowlevel.h>
#include <bordoisila_bits.h>
#include <bordoisila_io.h>
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Log/klog.h>
#include <Drivers/core.h>
#include <Drivers/res.h>
#include <string.h>
#include <audrv.h>
#include <Mm/kmalloc.h>
#include <Strings/export_imx8mp.h>


static uint64_t _base;
static uint64_t fb_phys;
static uint32_t hact;
static uint32_t vact;
static uint32_t hfp;
static uint32_t hbp;
static uint32_t hsw;
static uint32_t vfp;
static uint32_t vbp;
static uint32_t vsw;
static bool inv_hs;
static bool inv_vs;

static BordoisilaDriver _lcdif;


void lcdif_write(uint64_t base, uint32_t reg, uint32_t value) {
	(*(volatile uint32_t*)(base + reg) = value);
	dsb_ish();
	isb_flush();
}

uint32_t lcdif_read(uint64_t base, uint32_t reg) {
	volatile uint32_t* mmio = (volatile uint32_t*)(base + reg);
	return *mmio;
}

/*
* AuDriverUnload -- deattach the driver from
* aurora system
*/
AU_EXTERN AU_EXPORT int AuDriverUnload() {

	return 0;
}

#define H_ACTIVE 1920
#define V_ACTIVE 1080
#define H_FRONT 88
#define H_SYNC 44
#define H_BACK 148
#define V_FRONT 4
#define V_SYNC 5
#define V_BACK 36

void lcdif_wait() {
	for (int i = 0; i < 100000; i++)
		;
}

void lcdif_setmode() {
	/** display size setup **/
	lcdif_write(_base, LCDIF_DISP_SIZE, DISP_SIZE_DELTA_X(hact) | DISP_SIZE_DELTA_Y(vact));
	/** horizontal timing **/
	lcdif_write(_base, LCDIF_HSYN_PARA, HSYN_PARA_FP_H(hfp) | HSYN_PARA_BP_H(hbp));
	/** vertical timing **/
	lcdif_write(_base, LCDIF_VSYN_PARA, VSYN_PARA_FP_V(vfp) | VSYN_PARA_BP_V(vbp));
	/** sync pulse widths **/
	lcdif_write(_base, LCDIF_VSYN_HSYN_WIDTH, VSYN_HSYN_PW_H(hsw) | VSYN_HSYN_PW_V(vsw));


	if (inv_hs) lcdif_write(_base, LCDIF_CTRL_SET, CTRL_INV_HS);
	else        lcdif_write(_base, LCDIF_CTRL_CLR, CTRL_INV_HS);
	if (inv_vs) lcdif_write(_base, LCDIF_CTRL_SET, CTRL_INV_VS);
	else        lcdif_write(_base, LCDIF_CTRL_CLR, CTRL_INV_VS);

	/* Layer descriptor */
	lcdif_write(_base, LCDIF_CTRLDESCL0_1, CTRLDESCL0_1_WIDTH(hact) | CTRLDESCL0_1_HEIGHT(vact));
	lcdif_write(_base, LCDIF_CTRLDESCL0_3, CTRLDESCL0_3_PITCH(hact * 4));
	lcdif_write(_base, LCDIF_CTRLDESCL_LOW0_4, (uint32_t)(fb_phys & 0xFFFFFFFF));
	lcdif_write(_base, LCDIF_CTRLDESCL_HIGH0_4, (uint32_t)(fb_phys >> 32));

	/** pixel format: ARGB8888 */
	//uint32_t reg = lcdif_read(_base, LCDIF_CTRLDESCL0_5);
	uint32_t reg = 0;
	reg &= ~(CTRLDESCL0_5_BPP(0xf));
	reg |= CTRLDESCL0_5_BPP(BPP32_ARGB8888);
	lcdif_write(_base, LCDIF_CTRLDESCL0_5, reg);

	
	/** enable display + layer */
	lcdif_write(_base, LCDIF_DISP_PARA, DISP_PARA_DISP_ON);
	reg = 0; // lcdif_read(_base, LCDIF_CTRLDESCL0_5);
	reg |= CTRLDESCL0_5_EN;
	lcdif_write(_base, LCDIF_CTRLDESCL0_5, reg);

	/** trigger shadow load **/
	reg = 0; // lcdif_read(_base, LCDIF_CTRLDESCL0_5);
	reg |= CTRLDESCL0_5_SHADOW_LOAD_EN;
	lcdif_write(_base, LCDIF_CTRLDESCL0_5, reg);

}

/**
 * @brief lcdif_check_upstream -- check all upstream requirements
 * @param driver -- pointer to self
 */
int lcdif_check_upstream(BordoisilaDriver* driver) {
	BordoisilaClk* media_axi = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_MEDIA_AXI_NAME, BORDOISILA_DRIVER_RES_CLK);
	if (!media_axi) {
		UARTDebugOut("upstream %s clock not available \r\n", IMX8MP_MEDIA_AXI_NAME);
		return 1;
	}

	BordoisilaClk* media_abp = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_MEDIA_APB_NAME, BORDOISILA_DRIVER_RES_CLK);
	if (!media_abp) {
		UARTDebugOut("upstream %s clock not available \r\n", IMX8MP_MEDIA_APB_NAME);
		return 1;
	}

	if (!media_axi->res.is_running) {
		UARTDebugOut("upstream %s clock is not running \r\n", IMX8MP_MEDIA_AXI_NAME);
		return 1;
	}

	if (!media_abp->res.is_running) {
		UARTDebugOut("upstream %s clock is not running \r\n", IMX8MP_MEDIA_APB_NAME);
		return 1;
	}

	BordoisilaClk* hdmi_axi = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_HDMI_AXI_NAME, BORDOISILA_DRIVER_RES_CLK);
	if (!hdmi_axi) {
		BPrintK(BORDOISILA_ERROR, "clock %s is not available \r\n", IMX8MP_HDMI_AXI_NAME);
		return 1;
	}

	if (!hdmi_axi->res.is_running) {
		BPrintK(BORDOISILA_ERROR, "upstream %s clock is not running \r\n", IMX8MP_HDMI_AXI_NAME);
		return 1;
	}

	BordoisilaClk* hdmi_apb = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_HDMI_APB_NAME, BORDOISILA_DRIVER_RES_CLK);
	if (!hdmi_apb) {
		BPrintK(BORDOISILA_ERROR, "clock %s is not available \r\n", IMX8MP_HDMI_APB_NAME);
		return 1;
	}

	if (!hdmi_apb->res.is_running) {
		BPrintK(BORDOISILA_ERROR, "upstream %s is not running \r\n", hdmi_apb->res.name);
		return 1;
	}

	BordoisilaClk* hdmi_24m = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_HDMI_24M_NAME, BORDOISILA_DRIVER_RES_CLK);
	if (!hdmi_24m) {
		BPrintK(BORDOISILA_ERROR, "clock %s is not available \r\n", IMX8MP_HDMI_24M_NAME);
		return 1;
	}


	if (!hdmi_24m->res.is_running) {
		BPrintK(BORDOISILA_ERROR, "upstream %s is not running \r\n", IMX8MP_HDMI_24M_NAME);
		return 1;
	}

	BordoisilaClk* hdmi_266m = (BordoisilaClk*)BordoisilaGetDriverResource(IMX8MP_HDMI_266M_NAME, BORDOISILA_DRIVER_RES_CLK);
	if (!hdmi_266m) {
		BPrintK(BORDOISILA_ERROR, "clock %s is not available \r\n", IMX8MP_HDMI_266M_NAME);
		return 1;
	}

	if (!hdmi_266m->res.is_running) {
		BPrintK(BORDOISILA_ERROR, "upstream %s is not running \r\n", IMX8MP_HDMI_266M_NAME);
		return 1;
	}


	BordoisilaPower* mediamix = (BordoisilaPower*)BordoisilaGetDriverResource(IMX8MP_POWER_MEDIAMIX_NAME, BORDOISILA_DRIVER_RES_POWER);
	if (!mediamix) {
		UARTDebugOut("upstream %s power is not available \r\n", IMX8MP_POWER_MEDIAMIX_NAME);
		return 1;
	}

	if (!mediamix->res.is_running) {
		UARTDebugOut("upstream %s power is not running \r\n", IMX8MP_POWER_MEDIAMIX_NAME);
		return 1;
	}

	UARTDebugOut("upstream all checked [OK] \r\n");
	return 0;
}

/**
 * @brief lcdif_probe -- start probing lcdif
 * @param driver -- pointer to self
 */
int lcdif_probe(BordoisilaDriver* driver) {
	
	BPrintK(BORDOISILA_DEBUG, "lcdif probing ..\r\n");
	if (lcdif_check_upstream(driver)) 
		return 1;
	

	uint32_t val = (1ul << 31) | (1ul << 30); //soft reset + clk_gate
	_base = (uint64_t)AuMapMMIO(IMX8MP_LCDIF1_BASE, 4);

	uint32_t ctrl_reg = _bordoisila_readl(IMX8MP_LCDIF1_BASE);
	BPrintK(BORDOISILA_DEBUG, "[imx8mp lcdif]: ctrl val : %x \r\n", ctrl_reg);


	uint32_t ctrl2_reg = _bordoisila_readl(IMX8MP_LCDIF3_BASE);
	UARTDebugOut("[imx8mp lcdif]: lcdif3 ctrl val : %x \r\n", ctrl2_reg);

	lcdif_write(_base, LCDIF_CTRL, val);
	lcdif_wait();

	lcdif_write(_base, LCDIF_CTRL, 0x0);

	UARTDebugOut("[imx8mp lcdif]: software reset completed \r\n");

	
	BPrintK(BORDOISILA_DEBUG, "[imx8mp_lcdif]: lcdif initialized \r\n");
	return 0;
}

int lcdif_remove(BordoisilaDriver* driver) {
	UARTDebugOut("lcdif removing ... \r\n");
	return 0;
}

int lcdif_resume(BordoisilaDriver* driver) {
	UARTDebugOut("lcdif resuming ... \r\n");
	return 0;
}

int lcdif_suspend(BordoisilaDriver* driver) {
	UARTDebugOut("lcdif suspending ... \r\n");
	return 0;
}

/*
* AuDriverMain -- Main entry for LCD interface driver
*/
AU_EXTERN AU_EXPORT int AuDriverMain(AuDriver* drv) {
	UARTDebugOut("initializing lcdif .. \r\n");

	_lcdif.name = (char*)kmalloc(strlen("imx8mp-lcdif"));
	strcpy((char*)_lcdif.name, "imx8mp-lcdif");
	_lcdif.type = BORDOISILA_DRIVER_NORMAL;
	_lcdif.probe = &lcdif_probe;
	_lcdif.remove = &lcdif_remove;
	_lcdif.resume = &lcdif_resume;
	_lcdif.suspend = &lcdif_suspend;

	if (BordoisilaDriverRegister(&_lcdif)) {
		UARTDebugOut("imx8mp-lcdif failed to register itself \r\n");
		return 1;
	}

	/* start the probing process */
	return _lcdif.probe(&_lcdif);
}