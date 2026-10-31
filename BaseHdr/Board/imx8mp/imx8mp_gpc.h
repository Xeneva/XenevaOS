/**
* @file imx8mp_gpc.h
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

#ifndef __IMX8MP_GPC_H__
#define __IMX8MP_GPC_G__

enum _power_domain_id_ {
	IMX8MP_POWER_DOMAIN_MIPI_PHY1,
	IMX8MP_POWER_DOMAIN_PCIE_PHY,
	IMX8MP_POWER_DOMAIN_USB1_PHY,
	IMX8MP_POWER_DOMAIN_USB2_PHY,
	IMX8MP_POWER_DOMAIN_MLMIX,
	IMX8MP_POWER_DOMAIN_AUDIOMIX,
	IMX8MP_POWER_DOMAIN_GPU2D,
	IMX8MP_POWER_DOMAIN_GPUMIX,
	IMX8MP_POWER_DOMAIN_VPUMIX,
	IMX8MP_POWER_DOMAIN_GPU3D,
	IMX8MP_POWER_DOMAIN_MEDIAMIX,
	IMX8MP_POWER_DOMAIN_VPU_G1,
	IMX8MP_POWER_DOMAIN_VPU_G2,
	IMX8MP_POWER_DOMAIN_VPU_VC8000E,
	IMX8MP_POWER_DOMAIN_HDMIMIX,
	IMX8MP_POWER_DOMAIN_HDMI_PHY,
	IMX8MP_POWER_DOMAIN_MIPI_PHY2,
	IMX8MP_POWER_DOMAIN_HSIOMIX,
	IMX8MP_POWER_DOMAIN_MEDIAMIX_ISPDWP
};

/**
 * @brief imx8mp_gpc_init -- register each power domain
 * to the database
 */
extern void imx8mp_gpc_init();
extern int imx8mp_gpc_powerup(uint8_t id);
#endif
