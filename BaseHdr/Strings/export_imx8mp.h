/**
* @file export_imx8mp.h
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

#ifndef __EXPORT_IMX8MP_H__
#define __EXPORT_IMX8MP_H__

/** CLOCK Names **/
#define IMX8MP_MEDIA_AXI_NAME  "media_axi"
#define IMX8MP_MEDIA_APB_NAME  "media_apb"
#define IMX8MP_GPU3D_CORE_NAME "gpu3d_core"
#define IMX8MP_GPU3D_SHADER_NAME "gpu3d_shader"
#define IMX8MP_AUDIO_AXI_NAME    "audio_axi_clk"
#define IMX8MP_HSIO_AXI_NAME     "hsio_axi_clk"
#define IMX8MP_MEDIA_ISP_NAME    "media_isp_clk"
#define IMX8MP_MEDIA_DISP1_PIX_NAME "media_disp1_pix_clk"
#define IMX8MP_MEDIA_DISP2_PIX_NAME "media_disp2_pix_clk"
#define IMX8MP_HDMI_APB_NAME     "hdmi_apb"
#define IMX8MP_HDMI_AXI_NAME     "hdmi_axi"
#define IMX8MP_HDMI_24M_NAME     "hdmi_24m"
#define IMX8MP_HDMI_266M_NAME    "hdmi_ref_266m"



/** POWER Domain Names **/
#define IMX8MP_POWER_MIPI_PHY1_NAME "power_domain_mipi_phy1"
#define IMX8MP_POWER_PCIE_PHY_NAME  "power_domain_pcie_phy"
#define IMX8MP_POWER_USB1_PHY_NAME  "power_domain_usb1_phy"
#define IMX8MP_POWER_USB2_PHY_NAME  "power_domain_usb2_phy"
#define IMX8MP_POWER_MLMIX_NAME     "power_domain_mlmix"
#define IMX8MP_POWER_AUDIOMIX_NAME  "power_domain_audiomix"
#define IMX8MP_POWER_GPU2D_NAME     "power_domain_gpu2d"
#define IMX8MP_POWER_GPUMIX_NAME    "power_domain_gpumix"
#define IMX8MP_POWER_VPUMIX_NAME    "power_domain_vpumix"
#define IMX8MP_POWER_GPU3D_NAME     "power_domain_gpu3d"
#define IMX8MP_POWER_MEDIAMIX_NAME  "power_domain_mediamix"
#define IMX8MP_POWER_VPU_G1_NAME    "power_domain_vpu_g1"
#define IMX8MP_POWER_VPU_G2_NAME    "power_domain_vpu_g2"
#define IMX8MP_POWER_VPU_VC8000_NAME "power_domain_vpu_vc8000e"
#define IMX8MP_POWER_HDMI_PHY_NAME   "power_domain_hdmi_phy"
#define IMX8MP_POWER_HDMIMIX_NAME   "power_domain_hdmimix"
#define IMX8MP_POWER_MIPI_PHY2_NAME "power_domain_mipi_phy2"
#define IMX8MP_POWER_HSIOMIX_NAME   "power_domain_hsiomix"
#define IMX8MP_POWER_MEDIAMIX_ISPDWP_NAME    "power_domain_mediamix_ispdwp"
#define IMX8MP_POWER_MIPI_DSI_1_NAME      "mediablk_pd_mipi_dsi_1"
#define IMX8MP_POWER_MIPI_CSI2_1_NAME     "mediablk_pd_mipi_csi2_1"
#define IMX8MP_POWER_LCDIF1_NAME          "mediablk_pd_lcdif1"
#define IMX8MP_POWER_ISI_NAME             "mediablk_pd_isi"
#define IMX8MP_POWER_MIPI_CSI2_2_NAME     "mediablk_pd_mipi_csi2_2"
#define IMX8MP_POWER_LCDIF_2_NAME         "mediablk_pd_lcdif_2"
#define IMX8MP_POWER_ISP_NAME            "mediablk_pd_isp"
#define IMX8MP_POWER_DWE_NAME             "mediablk_pd_dwe"
#define IMX8MP_POWER_MIPI_DSI_2_NAME      "mediablk_pd_mipi_dsi_2"
#define IMX8MP_POWER_HDMI_IRQSTEER_NAME   "hdmiblk_pd_irqsteer"
#define IMX8MP_POWER_HDMI_LCDIF_NAME      "hdmiblk_pd_lcdif"
#define IMX8MP_POWER_HDMI_PD_PAI_NAME     "hdmiblk_pd_pai"
#define IMX8MP_POWER_HDMI_PD_PVI_NAME     "hdmiblk_pd_pvi"
#define IMX8MP_POWER_HDMI_PD_TRNG_NAME    "hdmiblk_pd_trng"
#define IMX8MP_POWER_HDMI_TX_NAME         "hdmiblk_pd_hdmi_tx"
#define IMX8MP_POWER_HDMI_TX_PHY_NAME     "hdmiblk_pd_hdmi_tx_phy"
#define IMX8MP_POWER_HDMI_HRV_NAME        "hdmiblk_pd_hrv"

#endif
