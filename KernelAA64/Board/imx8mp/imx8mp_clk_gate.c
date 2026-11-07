/**
* @file imx8mp_clk_gate.c
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

#include <Board/imx8mp/imx8mp_clk_gate.h>
#include <Board/imx8mp/imx8mp_clk.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <_null.h>
#include <bordoisila_io.h>
#include <aucon.h>
#include <Log/klog.h>
#include <string.h>

static _imx8mp_gate_t _gate_registry[100];

/** we are ignoring GATE4 entries for DDR and other devices */
#define GATE2_SET_ENTRY(n, _id, _offset)                                                           \
	_gate_registry[n].root_id = _id;                                                               \
	_gate_registry[n].base_addr = _offset;                                                         \
	_gate_registry[n]._gate4 = 0;                                                                  \
	_gate_registry[n]._enabled = 0;

#define GATE4_SET_ENTRY(n, _id, _offset)                                                           \
	_gate_registry[n].root_id = _id;                                                               \
	_gate_registry[n].base_addr = _offset;                                                         \
	_gate_registry[n]._gate4 = 1;                                                                  \
	_gate_registry[n]._enabled = 0;

/**
 * @brief imx8mp_gate_init -- initialize gate data registry
 */
void imx8mp_gate_init() {
	for (int i = 0; i < 100; i++)
		memset(&_gate_registry[i], 0, sizeof(_imx8mp_gate_t));

	int n = 0;

	GATE4_SET_ENTRY(n, IMX8MP_CLK_DRAM1_ROOT, 0x4050);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_ECSPI1_ROOT, 0x4070);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_ECSPI3_ROOT, 0x4090);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_ENET1_ROOT, 0x40a0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPIO1_ROOT, 0x40b0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPIO2_ROOT, 0x40c0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPIO3_ROOT, 0x40d0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPIO4_ROOT, 0x40e0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPIO5_ROOT, 0x40f0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPT1_ROOT, 0x4100);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPT2_ROOT, 0x4110);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPT3_ROOT, 0X4120);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPT4_ROOT, 0x4130);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPT5_ROOT, 0x4140);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_I2C1_ROOT, 0x4170);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_I2C2_ROOT, 0x4180);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_I2C3_ROOT, 0x4190);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_I2C4_ROOT, 0x41a0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_MU_ROOT, 0x4210);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_OCOTP_ROOT, 0x4220);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_PCIE_ROOT, 0x4250);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_PWM1_ROOT, 0x4280);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_PWM2_ROOT, 0x4290);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_PWM3_ROOT, 0x42a0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_PWM4_ROOT, 0x42b0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_QOS_ROOT, 0x42c0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_QOS_ENET_ROOT, 0x42e0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_QSPI_ROOT, 0x42f0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_NAND_ROOT, 0x4300); //shared
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_NAND_USDHC_BUS_RAWNAND_CLK, 0x4300); //shared2
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_I2C5_ROOT, 0x4330); //shared
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_I2C6_ROOT, 0x4340); //shared
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_CAN1_ROOT, 0x4350); //shared
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_CAN2_ROOT, 0x4360); //shared
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_SDMA1_ROOT, 0x43a0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_SIM_ENET_ROOT, 0x4400);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_ENET_QOS_ROOT, 0x43b0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPU2D_ROOT, 0x4450);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPU3D_ROOT, 0x4460);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_UART1_ROOT, 0x4490);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_UART2_ROOT, 0x44a0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_UART3_ROOT, 0x44b0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_UART4_ROOT, 0x44c0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_USB_ROOT, 0x44d0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_USB_SUSP, 0x44d0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_USB_PHY_ROOT, 0x44f0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_USDHC1_ROOT, 0x4510);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_USDHC2_ROOT, 0x4520);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_WDOG1_ROOT, 0x4530);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_WDOG2_ROOT, 0x4540);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_WDOG3_ROOT, 0x4550);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_VPU_G1_ROOT, 0x4560);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_GPU_ROOT, 0x4570);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_VPU_VC8KE_ROOT, 0x4590);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_VPU_G2_ROOT, 0x45a0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_NPU_ROOT, 0x45b0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_HSIO_ROOT, 0x45c0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_MEDIA_APB_ROOT, 0x45d0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_MEDIA_AXI_ROOT, 0x45d0);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_MEDIA_CAM1_PIX_ROOT, 0x45d0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_MEDIA_CAM2_PIX_ROOT, 0x45d0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_MEDIA_DISP1_PIX_ROOT, 0x45d0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_MEDIA_DISP2_PIX_ROOT, 0x45d0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_MEDIA_MIPI_PHY1_REF_ROOT, 0x45d0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_MEDIA_LDB_ROOT, 0x45d0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_MEDIA_ISP_ROOT, 0x45d0);
	n++;

	GATE2_SET_ENTRY(n, IMX8MP_CLK_USDHC3_ROOT, 0x45e0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_HDMI_ROOT, 0x45f0);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_TSENSOR_ROOT, 0x4620);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_VPU_ROOT, 0x4630);
	n++;

	GATE2_SET_ENTRY(n, IMX8MP_CLK_AUDIO_AHB_ROOT, 0x4650);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_AUDIO_AXI_ROOT, 0x4650);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_SAI1_ROOT, 0x4650);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_SAI2_ROOT, 0x4650);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_SAI3_ROOT, 0x4650);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_SAI5_ROOT, 0x4650);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_SAI6_ROOT, 0x4650);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_SAI7_ROOT, 0x4650);
	n++;
	GATE2_SET_ENTRY(n, IMX8MP_CLK_PDM_ROOT, 0x4650);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_XTAL_ROOT, 0x4600);
	n++;
	GATE4_SET_ENTRY(n, IMX8MP_CLK_PLL_ROOT, 0x4610);

	BPrintK(BORDOISILA_INFO, "gate registry initialized \r\n");
}

_imx8mp_gate_t* imx8mp_find_gate(uint32_t id) {
	for (int i = 0; i < UINT8_MAX; i++) {
		if (_gate_registry[i].root_id == id) {
			return &_gate_registry[i];
		}
	}
	return NULL;
}

#define CGC_MASK 0x3;

int imx8mp_clk_gate_enable(uint32_t clk_root_id) {
	_imx8mp_gate_t* gate = imx8mp_find_gate(clk_root_id);
	if (!gate) {
		AuTextOut("[imx8mp clk-gate]: didn't find dedicated gate to enable root : %d \r\n",
				  clk_root_id);
		return -1;
	}

	if (gate->_enabled) {
		BPrintK(BORDOISILA_WARN, "imx8mp gate : %d is already enabled \r\n", clk_root_id);
		return 0;
	}
	uint64_t ccm_base = imx8mp_ccm_get_base();

	uintptr_t reg = ccm_base + gate->base_addr;
	uint32_t val = _bordoisila_readl(reg);
	val |= CGC_MASK;
	_bordoisila_writel(val, reg);
	BPrintK(BORDOISILA_INFO, "imx8mp clk gate enabled for root offset : %x \r\n", reg);
	gate->_enabled = 1;
	return 0;
}

int imx8mp_clk_gate_disable(uint32_t clk_root_id) {
	_imx8mp_gate_t* gate = imx8mp_find_gate(clk_root_id);
	if (!gate) {
		AuTextOut("[imx8mp clk-gate]: didn't find dedicated gate to enable root : %d \r\n",
				  clk_root_id);
		return -1;
	}
	uint64_t ccm_base = imx8mp_ccm_get_base();
	uintptr_t reg = ccm_base + gate->base_addr;
	uint32_t val = _bordoisila_readl(reg);
	val &= ~CGC_MASK;
	_bordoisila_writel(val, reg);
	gate->_enabled = 0;
	return 0;
}

#endif