/**
* @file dw_hdmi.h
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
#include "dw_hdmi.h"
#include <stdint.h>
#include <bordoisila_bits.h>
#include <bordoisila_io.h>

static dw_hdmi _hdmi;

static inline void hdmi_writeb(dw_hdmi* hdmi, uint8_t val, int offset) {
	_bordoisila_writeb(val, hdmi->hdmi_base + (offset << hdmi->reg_shift));
}

static inline uint8_t hdmi_readb(dw_hdmi* hdmi, int offset) {
	unsigned int val = _bordoisila_readb(hdmi->hdmi_base + offset << hdmi->reg_shift);
	return val & UINT8_MAX;
}

static void dw_hdmi_i2c_init(dw_hdmi* hdmi) {
	hdmi_writeb(hdmi, HDMI_PHY_I2CM_INT_ADDR_DONE_POL, HDMI_PHY_I2CM_INT_ADDR);
	hdmi_writeb(hdmi, HDMI_PHY_I2CM_CTLINT_ADDR_NAC_POL |
		HDMI_PHY_I2CM_CTLINT_ADDR_ARBITRATION_POL, HDMI_PHY_I2CM_CTLINT_ADDR);

	/* software reset */
	hdmi_writeb(hdmi, 0x00, HDMI_I2CM_SOFTRSTZ);

	/* set standard mode speed (determined to be 100khz on imx6)*/
	hdmi_writeb(hdmi, 0x00, HDMI_I2CM_DIV);

	/* set done, not acknowledged and arbitration interrupt polarities */
	hdmi_writeb(hdmi, HDMI_I2CM_INT_DONE_POL, HDMI_I2CM_INT);
	hdmi_writeb(hdmi, HDMI_I2CM_CTLINT_NAC_POL | HDMI_I2CM_CTLINT_ARB_POL,
		HDMI_I2CM_CTLINT);

	/* clear DONE and ERROR interrupts */
	hdmi_writeb(hdmi, HDMI_IH_I2CM_STAT0_ERROR | HDMI_IH_I2CM_STAT0_DONE,
		HDMI_IH_I2CM_STAT0);

	/* mute DONE and ERROR interrupts */
	hdmi_writeb(hdmi, HDMI_IH_I2CM_STAT0_ERROR | HDMI_IH_I2CM_STAT0_DONE,
		HDMI_IH_MUTE_I2CM_STAT0);
}


void dw_hdmi_probe() {
	uint8_t prod_id0;
	uint8_t prod_id1;
	uint8_t config0;
	uint8_t config3;
	_hdmi.version = (hdmi_readb(&_hdmi, HDMI_DESIGN_ID) << 8) |
		(hdmi_readb(&_hdmi, HDMI_REVISION_ID) << 0);

	prod_id0 = hdmi_readb(&_hdmi, HDMI_PRODUCT_ID0);
	prod_id1 = hdmi_readb(&_hdmi, HDMI_PRODUCT_ID1);
}