/**
* @file imx8mp_gpc.c
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

#include <Board/imx8mp/imx8mp_clk.h>
#include <Board/imx8mp/imx8mp_gpc.h>
#include <Board/imx8mp/imx8mp_blkctrl.h>
#include <Drivers/uart.h>
#include <Drivers/core.h>
#include <Drivers/res.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <bordoisila_io.h>
#include <bordoisila_bits.h>
#include <aucon.h>
#include <_null.h>
#include <Log/klog.h>
#include <Mm/kmalloc.h>
#include <string.h>

static uint64_t _gpc_base;

#define GPC_BASE					 0x303A0000UL
#define IMX8MP_GPC_PGC_CPU_MAPPING	 0x1CC
#define IMX8MP_GPC_PU_PGC_SW_PUP_REQ 0x0D8 //0x0D8
#define IMX8MP_GPC_PU_PGC_SW_PDN_REQ 0x0E4
#define GPC_PGC_CTRL(n)				 (0x800 + n * 0x40)
#define GPC_PGC_SR(n)				 (GPC_PGC_CTRL(n) + 0x0C)
#define IMX8MP_GPC_PU_PWRHSK		 0x190
#define IMX8MP_MEDIAMIX_A53_DOMAIN	 (1ul << 12)

#define IMX8MP_PGC_NOC			 9
#define IMX8MP_PGC_MIPI1		 12
#define IMX8MP_PGC_PCIE			 13
#define IMX8MP_PGC_USB1			 14
#define IMX8MP_PGC_USB2			 15
#define IMX8MP_PGC_MLMIX		 16
#define IMX8MP_PGC_AUDIOMIX		 17
#define IMX8MP_PGC_GPU2D		 18
#define IMX8MP_PGC_GPUMIX		 19
#define IMX8MP_PGC_VPUMIX		 20
#define IMX8MP_PGC_GPU3D		 21
#define IMX8MP_PGC_MEDIAMIX		 22
#define IMX8MP_PGC_VPU_G1		 23
#define IMX8MP_PGC_VPU_G2		 24
#define IMX8MP_PGC_VPU_VC8000E	 25
#define IMX8MP_PGC_HDMIMIX		 26
#define IMX8MP_PGC_HDMI			 27
#define IMX8MP_PGC_MIPI2		 28
#define IMX8MP_PGC_HSIOMIX		 29
#define IMX8MP_PGC_MEDIA_ISP_DWP 30
#define IMX8MP_PGC_DDRMIX		 31

#define IMX8MP_MEDIA_ISPDWP_A53_DOMAIN BORDOISILA_BIT(20)
#define IMX8MP_HSIOMIX_A53_DOMAIN	   BORDOISILA_BIT(19)
#define IMX8MP_MIPI_PHY2_A53_DOMAIN	   BORDOISILA_BIT(18)
#define IMX8MP_HDMI_PHY_A53_DOMAIN	   BORDOISILA_BIT(17)

/* SW Pup/Pdwn Request bits*/
#define IMX8MP_DDRMIX_PXX_REQ			   BORDOISILA_BIT(19)
#define IMX8MP_MEDIA_ISP_DWP_PXX_REQ	   BORDOISILA_BIT(18)
#define IMX8MP_HSIOMIX_PXX_REQ			   BORDOISILA_BIT(17)
#define IMX8MP_MIPI_PHY2_PXX_REQ		   BORDOISILA_BIT(16)
#define IMX8MP_HDMI_PHY_PXX_REQ			   BORDOISILA_BIT(15)
#define IMX8MP_HDMIMIX_PXX_REQ			   BORDOISILA_BIT(14)
#define IMX8MP_VPU_VC8K_PXX_REQ			   BORDOISILA_BIT(13)
#define IMX8MP_VPU_G2_PXX_REQ			   BORDOISILA_BIT(12)
#define IMX8MP_VPU_G1_PXX_REQ			   BORDOISILA_BIT(11)
#define IMX8MP_MEDIMIX_PXX_REQ			   BORDOISILA_BIT(10)
#define IMX8MP_GPU_3D_PXX_REQ			   BORDOISILA_BIT(9)
#define IMX8MP_VPU_MIX_SHARE_LOGIC_PXX_REQ BORDOISILA_BIT(8)
#define IMX8MP_GPU_SHARE_LOGIC_PXX_REQ	   BORDOISILA_BIT(7)
#define IMX8MP_GPU_2D_PXX_REQ			   BORDOISILA_BIT(6)
#define IMX8MP_AUDIOMIX_PXX_REQ			   BORDOISILA_BIT(5)
#define IMX8MP_MLMIX_PXX_REQ			   BORDOISILA_BIT(4)
#define IMX8MP_USB2_PHY_PXX_REQ			   BORDOISILA_BIT(3)
#define IMX8MP_USB1_PHY_PXX_REQ			   BORDOISILA_BIT(2)
#define IMX8MP_PCIE_PHY_SW_PXX_REQ		   BORDOISILA_BIT(1)
#define IMX8MP_MIPI_PHY1_SW_PXX_REQ		   BORDOISILA_BIT(0)

#define IMX8MP_MEDIAMIX_PWRDNACKN BORDOISILA_BIT(30)
#define IMX8MP_HDMIMIX_PWRDNACKN  BORDOISILA_BIT(29)
#define IMX8MP_HSIOMIX_PWRDNACKN  BORDOISILA_BIT(28)
#define IMX8MP_VPUMIX_PWRDNACKN	  BORDOISILA_BIT(26)
#define IMX8MP_GPUMIX_PWRDNACKN	  BORDOISILA_BIT(25)
#define IMX8MP_MLMIX_PWRDNACKN	  (BORDOISILA_BIT(23) | BORDOISILA_BIT(24))
#define IMX8MP_AUDIOMIX_PWRDNACKN BORDOISILA_BIT(20) | BORDOISILA_BIT(31)
#define IMX8MP_MEDIAMIX_PWRDNREQN BORDOISILA_BIT(14)
#define IMX8MP_HDMIMIX_PWRDNREQN  BORDOISILA_BIT(13)
#define IMX8MP_HSIOMIX_PWRDNREQN  BORDOISILA_BIT(12)
#define IMX8MP_VPUMIX_PWRDNREQN	  BORDOISILA_BIT(10)
#define IMX8MP_GPUMIX_PWRDNREQN	  BORDOISILA_BIT(9)
#define IMX8MP_MLMIX_PWRDNREQN	  (BORDOISILA_BIT(7) | BORDOISILA_BIT(8))
#define IMX8MP_AUDIOMIX_PWRDNREQN (BORDOISILA_BIT(4) | BORDOISILA_BIT(15))

#define IMX8MP_MEDIA_ISPDWP_A53_DOMAIN BORDOISILA_BIT(20)
#define IMX8MP_HSIOMIX_A53_DOMAIN	   BORDOISILA_BIT(19)
#define IMX8MP_MIPI_PHY2_A53_DOMAIN	   BORDOISILA_BIT(18)
#define IMX8MP_HDMI_PHY_A53_DOMAIN	   BORDOISILA_BIT(17)
#define IMX8MP_HDMIMIX_A53_DOMAIN	   BORDOISILA_BIT(16)
#define IMX8MP_VPU_VC8000E_A53_DOMAIN  BORDOISILA_BIT(15)
#define IMX8MP_VPU_G2_A53_DOMAIN	   BORDOISILA_BIT(14)
#define IMX8MP_VPU_G1_A53_DOMAIN	   BORDOISILA_BIT(13)
#define IMX8MP_MEDIAMIX_A53_DOMAIN	   BORDOISILA_BIT(12)
#define IMX8MP_GPU3D_A53_DOMAIN		   BORDOISILA_BIT(11)
#define IMX8MP_VPUMIX_A53_DOMAIN	   BORDOISILA_BIT(10)
#define IMX8MP_GPUMIX_A53_DOMAIN	   BORDOISILA_BIT(9)
#define IMX8MP_GPU2D_A53_DOMAIN		   BORDOISILA_BIT(8)
#define IMX8MP_AUDIOMIX_A53_DOMAIN	   BORDOISILA_BIT(7)
#define IMX8MP_MLMIX_A53_DOMAIN		   BORDOISILA_BIT(6)
#define IMX8MP_USB2_PHY_A53_DOMAIN	   BORDOISILA_BIT(5)
#define IMX8MP_USB1_PHY_A53_DOMAIN	   BORDOISILA_BIT(4)
#define IMX8MP_PCIE_PHY_A53_DOMAIN	   BORDOISILA_BIT(3)
#define IMX8MP_MIPI_PHY1_A53_DOMAIN	   BORDOISILA_BIT(2)

#define GPC_PGC_CTRL_PCR BORDOISILA_BIT(0)

typedef struct _gpc_pdomain_ {
	uint8_t pdomain_id;
	uint32_t pxx_req;
	uint32_t map_mask;
	uint32_t pgc_offset;
	uint32_t hskreq;
	uint32_t hskack;
} _imx8mp_gpc_pdomain_t;

#define GPC_WRITE(base, offset, val) (*(volatile uint32_t*)(base + offset) = val)
#define GPC_READ(base, offset)		 (*(volatile uint32_t*)(base + offset))

static _imx8mp_gpc_pdomain_t _pdomains[50];

#define IMX8MP_PDOMAIN_REGISTER(n, _id, _pxx_req, _map_mask, _pgc_offset, _hskreq, _hskack)        \
	_pdomains[n].pdomain_id = _id;                                                                 \
	_pdomains[n].pxx_req = _pxx_req;                                                               \
	_pdomains[n].map_mask = _map_mask;                                                             \
	_pdomains[n].pgc_offset = _pgc_offset;                                                         \
	_pdomains[n].hskreq = _hskreq;                                                                 \
	_pdomains[n].hskack = _hskack;

/**
 * @brief imx8mp_pwr_on -- power on callback from each power domain
 * @param pwr -- pointer to power domain
 */
int imx8mp_pwr_on(BordoisilaPower* pwr) {
	if (!pwr) {
		BPrintK(BORDOISILA_ERROR, "NULL! power domain ! \r\n");
		return -1;
	}

	if (pwr->res.is_running) {
		BPrintK(BORDOISILA_WARN, "power domain : %s is already running \r\n");
		return 1;
	}

	if (pwr->parent_bus_enable) {
		if (pwr->parent_bus_enable(pwr)) {
			BPrintK(BORDOISILA_WARN,
					"parent bus for %s failed to enable itself, proceeding..\r\n",
					pwr->res.name);
		}
	}

	_imx8mp_gpc_pdomain_t* domain = pwr->res.data;
	if (!domain) {
		BPrintK(BORDOISILA_ERROR, "kernel power domain : %s, no platform data \r\n", pwr->res.name);
		return 1;
	}
	if (imx8mp_gpc_powerup(domain->pdomain_id) == -1) {
		BPrintK(BORDOISILA_ERROR, "kernel power domain powerup failed in imx8mp_gpc \r\n");
		return 1;
	}
	pwr->res.is_running = true;
	return 0;
}

int imx8mp_pwr_down(BordoisilaPower* pwr) {
	BPrintK(BORDOISILA_ERROR, "power domain -power down not implemented for GPCv2 \r\n");
	return 0;
}
/**
 * @brief imx8mp_alloc_kernel_resource -- allocate kernel resource
 * @param name -- name of the resource
 * @param data -- pointer to extra data
 */
BordoisilaDriverResource* imx8mp_gpc_kernel_resource(char* name, void* data) {
	BordoisilaPower* pwr = (BordoisilaPower*)kmalloc(sizeof(BordoisilaPower));
	strcpy(pwr->res.name, name);
	pwr->res.res_type = BORDOISILA_DRIVER_RES_POWER;
	pwr->res.ref_count = 0;
	pwr->res.data = data;
	pwr->res.is_running = false;
	pwr->power_on = imx8mp_pwr_on;
	pwr->power_down = imx8mp_pwr_down;
	if (BordoisilaDriverResourceRegister((BordoisilaDriverResource*)pwr)) {
		BPrintK(BORDOISILA_ERROR, "failed to register kernel power resource : %s \r\n", name);
		return NULL;
	}
	return (BordoisilaDriverResource*)pwr;
}
/**
 * @brief imx8mp_gpc_init -- register each power domain
 * to the database
 */
void imx8mp_gpc_init() {
	_gpc_base = (uint64_t)AuMapMMIO(GPC_BASE, 16);
	for (int i = 0; i < 50; i++)
		memset(&_pdomains[i], 0, sizeof(_imx8mp_gpc_pdomain_t));

	int n = 0;
	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_MIPI_PHY1,
							IMX8MP_MIPI_PHY1_SW_PXX_REQ,
							IMX8MP_MIPI_PHY1_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_MIPI1),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_mipi_phy1", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_PCIE_PHY,
							IMX8MP_PCIE_PHY_SW_PXX_REQ,
							IMX8MP_PCIE_PHY_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_PCIE),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_pcie_phy", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_USB1_PHY,
							IMX8MP_USB1_PHY_PXX_REQ,
							IMX8MP_USB1_PHY_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_USB1),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_usb1_phy", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_USB2_PHY,
							IMX8MP_USB2_PHY_PXX_REQ,
							IMX8MP_USB2_PHY_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_USB2),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_usb2_phy", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_MLMIX,
							IMX8MP_MLMIX_PXX_REQ,
							IMX8MP_MLMIX_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_MLMIX),
							IMX8MP_MLMIX_PWRDNREQN,
							IMX8MP_MLMIX_PWRDNACKN);
	imx8mp_gpc_kernel_resource("power_domain_mlmix", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_AUDIOMIX,
							IMX8MP_AUDIOMIX_PXX_REQ,
							IMX8MP_AUDIOMIX_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_AUDIOMIX),
							IMX8MP_AUDIOMIX_PWRDNREQN,
							IMX8MP_AUDIOMIX_PWRDNACKN);
	imx8mp_gpc_kernel_resource("power_domain_audiomix", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_GPU2D,
							IMX8MP_GPU_2D_PXX_REQ,
							IMX8MP_GPU2D_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_GPU2D),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_gpu2d", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_GPUMIX,
							IMX8MP_GPU_SHARE_LOGIC_PXX_REQ,
							IMX8MP_GPUMIX_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_GPUMIX),
							IMX8MP_GPUMIX_PWRDNREQN,
							IMX8MP_GPUMIX_PWRDNACKN);
	imx8mp_gpc_kernel_resource("power_domain_gpumix", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_VPUMIX,
							IMX8MP_VPU_MIX_SHARE_LOGIC_PXX_REQ,
							IMX8MP_VPUMIX_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_VPUMIX),
							IMX8MP_VPUMIX_PWRDNREQN,
							IMX8MP_VPUMIX_PWRDNACKN);
	imx8mp_gpc_kernel_resource("power_domain_vpumix", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_GPU3D,
							IMX8MP_GPU_3D_PXX_REQ,
							IMX8MP_GPU3D_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_GPU3D),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_gpu3d", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_MEDIAMIX,
							IMX8MP_MEDIMIX_PXX_REQ,
							IMX8MP_MEDIAMIX_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_MEDIAMIX),
							IMX8MP_MEDIAMIX_PWRDNREQN,
							IMX8MP_MEDIAMIX_PWRDNACKN);
	imx8mp_gpc_kernel_resource("power_domain_mediamix", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_VPU_G1,
							IMX8MP_VPU_G1_PXX_REQ,
							IMX8MP_VPU_G1_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_VPU_G1),
							0,
							0); //VPU block not added now
	imx8mp_gpc_kernel_resource("power_domain_vpu_g1", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_VPU_G2,
							IMX8MP_VPU_G2_PXX_REQ,
							IMX8MP_VPU_G2_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_VPU_G2),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_vpu_g2", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_VPU_VC8000E,
							IMX8MP_VPU_VC8K_PXX_REQ,
							IMX8MP_VPU_VC8000E_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_VPU_VC8000E),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_vpu_vc8000e", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_HDMIMIX,
							IMX8MP_HDMIMIX_PXX_REQ,
							IMX8MP_HDMIMIX_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_HDMIMIX),
							IMX8MP_HDMIMIX_PWRDNREQN,
							IMX8MP_HDMIMIX_PWRDNACKN);
	BordoisilaPower* hdmi_pwr =
		(BordoisilaPower*)imx8mp_gpc_kernel_resource("power_domain_hdmimix", &_pdomains[n]);
	/** attach force bus enabling here, because HDMIMIX needs in order to receive
	 * acknowledgement from HDMIMIX power domain
	 */
	hdmi_pwr->parent_bus_enable = hdmi_parent_bus_callback;

	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_HDMI_PHY,
							IMX8MP_HDMI_PHY_PXX_REQ,
							IMX8MP_HDMI_PHY_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_HDMI),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_hdmi_phy", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_MIPI_PHY2,
							IMX8MP_MIPI_PHY2_PXX_REQ,
							IMX8MP_MIPI_PHY2_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_MIPI2),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_mipi_phy2", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_HSIOMIX,
							IMX8MP_HSIOMIX_PXX_REQ,
							IMX8MP_HSIOMIX_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_HSIOMIX),
							IMX8MP_HSIOMIX_PWRDNREQN,
							IMX8MP_HSIOMIX_PWRDNACKN);
	imx8mp_gpc_kernel_resource("power_domain_hsiomix", &_pdomains[n]);
	n++;

	IMX8MP_PDOMAIN_REGISTER(n,
							IMX8MP_POWER_DOMAIN_MEDIAMIX_ISPDWP,
							IMX8MP_MEDIA_ISP_DWP_PXX_REQ,
							IMX8MP_MEDIA_ISPDWP_A53_DOMAIN,
							GPC_PGC_CTRL(IMX8MP_PGC_MEDIA_ISP_DWP),
							0,
							0);
	imx8mp_gpc_kernel_resource("power_domain_mediamix_ispdwp", &_pdomains[n]);
	BPrintK(BORDOISILA_INFO, "gpc registry initialized \r\n");
}

static _imx8mp_gpc_pdomain_t* imx8mp_pdomain_find(uint8_t id) {
	for (int i = 0; i < 32; i++) {
		if (_pdomains[i].pdomain_id == id)
			return &_pdomains[i];
	}

	return NULL;
}

int imx8mp_gpc_powerup(uint8_t id) {
	_imx8mp_gpc_pdomain_t* domain = imx8mp_pdomain_find(id);
	if (!domain)
		return -1;

	/**TODO:  we need to enable parent clock in CCM **/
	UARTDebugOut("GPC Powering up : %d \r\n", domain->pdomain_id);

	//set the cpu mapping
	_bordoisila_update_bits(
		_gpc_base + IMX8MP_GPC_PGC_CPU_MAPPING, domain->map_mask, domain->map_mask);
	_bordoisila_update_bits(
		(uint64_t)_gpc_base + domain->pgc_offset, GPC_PGC_CTRL_PCR, GPC_PGC_CTRL_PCR);
	_bordoisila_update_bits(
		(uint64_t)_gpc_base + IMX8MP_GPC_PU_PGC_SW_PUP_REQ, domain->pxx_req, domain->pxx_req);

	if (domain->pdomain_id == IMX8MP_POWER_DOMAIN_HDMIMIX) {
		BPrintK(BORDOISILA_DEBUG, "GPC HDMIMIX Polling for BIT(9) \r\n");
		while (!(_bordoisila_readl(0x30390000 + 0x94) & BORDOISILA_BIT(8)))
			;
	}

	uint32_t val = _bordoisila_readl((uint64_t)_gpc_base + domain->pgc_offset);
	BPrintK(BORDOISILA_INFO, "imx8mp power domain id, power up requested \r\n", domain->pdomain_id);
	BPrintK(BORDOISILA_INFO,
			"imx8mp pgc offset : %x, value : %d  \r\n",
			(_gpc_base + domain->pgc_offset),
			val);

	int timeout = 100000;
	uint32_t req_val;
	do {
		req_val = _bordoisila_readl((uint64_t)_gpc_base + IMX8MP_GPC_PU_PGC_SW_PUP_REQ);
		if (!(req_val & domain->pxx_req))
			break;
	} while (--timeout);

	if (timeout == 0) {
		BPrintK(BORDOISILA_ERROR,
				"imx8mp domain : %d pxx_req never self -cleared \r\n",
				domain->pdomain_id);
	} else {
		BPrintK(BORDOISILA_INFO,
				"imx8mp domain %d PGC pup cleared after %d iteration \r\n",
				domain->pdomain_id,
				timeout);
	}
	//ADB-400 handshake : yayyyyy
	if (domain->hskreq) {
		uint32_t val = _bordoisila_readl(_gpc_base + IMX8MP_GPC_PU_PWRHSK);
		val |= domain->hskreq;
		_bordoisila_writel(val, _gpc_base + IMX8MP_GPC_PU_PWRHSK);

		timeout = 1000000000;
		while (timeout--) {
			uint32_t curr_hsk = _bordoisila_readl((uint64_t)_gpc_base + IMX8MP_GPC_PU_PWRHSK);
			if (curr_hsk & domain->hskack) {
				BPrintK(BORDOISILA_INFO, "imx8mp power domain - adb ack received \r\n");
				break;
			}
		}

		BPrintK(BORDOISILA_INFO,
				"imx8mp power domain - adb handshake completed at iteration : %d \r\n",
				timeout);
	}

	return 0;
}
#endif