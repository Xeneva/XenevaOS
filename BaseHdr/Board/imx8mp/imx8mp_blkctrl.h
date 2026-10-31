/**
* @file imx8mp_blkctrl.h
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

#ifndef __IMX8MP_BLKCTRL_H__
#define __IMX8MP_BLKCTRL_H__

#include <stdint.h>
#include <Drivers/res.h>

enum blk_map_id {
	IMX8MP_MEDIABLK_PD_MIPI_DSI_1,
	IMX8MP_MEDIABLK_PD_MIPI_CSI2_1,
	IMX8MP_MEDIABLK_PD_LCDIF_1,
	IMX8MP_MEDIABLK_PD_ISI,
	IMX8MP_MEDIABLK_PD_MIPI_CSI2_2,
	IMX8MP_MEDIABLK_PD_LCDIF_2,
	IMX8MP_MEDIABLK_PD_ISP,
	IMX8MP_MEDIABLK_PD_DWE,
	IMX8MP_MEDIABLK_PD_MIPI_DSI_2,
	IMX8MP_HDMIBLK_PD_IRQSTEER,
	IMX8MP_HDMIBLK_PD_LCDIF,
	IMX8MP_HDMIBLK_PD_PAI,
	IMX8MP_HDMIBLK_PD_PVI,
	IMX8MP_HDMIBLK_PD_TRNG,
	IMX8MP_HDMIBLK_PD_HDMI_TX,
	IMX8MP_HDMIBLK_PD_HDMI_TX_PHY,
	IMX8MP_HDMIBLK_PD_HRV
};


/**
 * @brief imx8mp_blkctl_init -- register the block control
 * registry, for now, only MEDIA domain is entered
 */
extern void imx8mp_blkctrl_init();

extern int imx8mp_blkctl_powerup(uint32_t id);

extern int imx8mp_blkctrl_release_reset(uint32_t id);

extern int hdmi_parent_bus_callback(BordoisilaPower* power);
#endif
