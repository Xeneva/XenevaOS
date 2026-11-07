/**
* @file imx8mp_clk.c
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
#include <Drivers/uart.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Log/klog.h>
#include <aucon.h>
#include <_null.h>
#include <Board/imx8mp/imx8mp_pll.h>
#include <Board/imx8mp/imx8mp_clk_gate.h>
#include <Hal/AA64/aa64cpu.h>
#include <dtb.h>
#include <Drivers/res.h>
#include <Drivers/core.h>
#include <Mm/kmalloc.h>
#include <string.h>

static uint64_t _ccm_base;

typedef struct _clk_node_ {
	char* name;
	uint32_t (*recalc_rate)(struct _clk_node_* self);
	int num_parent;
	struct _clk_node_** parent;
	uint32_t reg_offset;
	uint32_t clk_slice;
	uint32_t gate_slice;
	uint32_t pre_podf;
	uint32_t post_podf;
	int is_composite;
	uint32_t anatop_base;
	bool _pll_read;
	uint64_t rate;
	int current_parent_idx;
} imx8mp_clk;

typedef struct _dt_clk_bindings_ {
	uint32_t clk_id;
	int has_parent;
	uint32_t parent_clk_id;
	uint32_t rate_hz;
} imx8mp_dt_clk;

static imx8mp_clk _clk_node[100];
static int _clk_node_count = 0;

static void imx8mp_write_target_root(uint32_t clk_root_idx,
									 uint32_t offset,
									 uint32_t mux_val,
									 uint32_t pre_podf,
									 uint32_t post_podf);

static imx8mp_clk* _imx8mp_clk_alloc(const char* name) {
	if (_clk_node_count == 100) {
		BPrintK(BORDOISILA_ERROR, "imx8mp failed to allocate clock, max clock exceeds \r\n");
		return NULL;
	}
	imx8mp_clk* n = &_clk_node[_clk_node_count++];
	n->name = name;
	n->parent = NULL;
	n->current_parent_idx = 0;
	n->is_composite = 0;
	return n;
}

static uint32_t _imx8mp_fixed_recalc(imx8mp_clk* clk) {
	return clk->pre_podf;
}

static imx8mp_clk* imx8mp_clk_fixed(const char* name, uint32_t fixed_hz) {
	imx8mp_clk* n = _imx8mp_clk_alloc(name);
	n->recalc_rate = _imx8mp_fixed_recalc;
	n->pre_podf = fixed_hz;
	n->rate = fixed_hz;
	return n;
}

static uint32_t imx8mp_composite_recalc(imx8mp_clk* self) {
	imx8mp_clk* parent = self->parent[self->current_parent_idx];
	uint32_t parent_rate = parent->recalc_rate(parent);
	uint32_t total_div = (self->pre_podf + 1) * (self->post_podf + 1);
	return parent_rate / total_div;
}

static imx8mp_clk* imx8mp_clk_composite(const char* name, imx8mp_clk** parent, int n_parents) {
	imx8mp_clk* n = _imx8mp_clk_alloc(name);
	n->recalc_rate = imx8mp_composite_recalc;
	n->num_parent = n_parents;
	n->parent = parent;
	n->is_composite = 1;
	n->pre_podf = 0;
	n->post_podf = 0;
	n->anatop_base = 0;
	n->_pll_read = 0;
	return n;
}

static imx8mp_clk*
imx8mp_clk_pll(const char* name, imx8mp_clk** parent, int n_parents, uint32_t anatop) {
	imx8mp_clk* n = _imx8mp_clk_alloc(name);
	n->recalc_rate = 0;
	n->num_parent = n_parents;
	n->parent = parent;
	n->is_composite = 1;
	n->pre_podf = 0;
	n->post_podf = 0;
	n->anatop_base = anatop;
	n->_pll_read = 1;
	return n;
}

static uint32_t AuDeviceTreeGetFixedClockRate(const char* node_name) {
	uint32_t* node = AuDeviceTreeGetNode(node_name);
	if (!node) {
		BPrintK(BORDOISILA_ERROR, "imx8mp dtp node: %s not found \r\n", node_name);
		return 0;
	}

	uint32_t rate = AuDeviceTreeGetU32Property(node, "clock-frequency", 0);
	BPrintK(BORDOISILA_INFO, "clock rate for node : %s is %u \r\n", node_name, rate);
	return rate;
}

static void imx8mp_clk_set_rate(imx8mp_clk* self, uint32_t target_hz);

/*
 * @brief kernel_res_clk_set_rate -- set rate of a clock, for this
 * enable the entire clock
 * @param clk -- pointer to kernel clock data structure
 * @param rate -- rate to set in Hz
 */
int kernel_res_clk_set_rate(BordoisilaClk* clk, uint64_t rate) {
	imx8mp_clk* sys_clk = (imx8mp_clk*)clk->res.data;
	if (!sys_clk) {
		BPrintK(BORDOISILA_ERROR, "failed to start clock : %s \r\n", clk->res.name);
		return 1;
	}
	if (clk->res.is_running) {
		BPrintK(BORDOISILA_WARN, "clk : %s is already running \r\n", clk->res.name);
		return 0;
	}
	imx8mp_clk_set_rate(sys_clk, rate); //500000000UL
	if (sys_clk->gate_slice) {
		imx8mp_clk_gate_enable(sys_clk->gate_slice);
	}
	clk->rate_hz = sys_clk->rate;
	clk->res.is_running = true;
	clk->rate_hz = rate;
}
/**
 * @brief imx8mp_alloc_kernel_resource -- allocate kernel resource
 * @param name -- name of the resource
 * @param data -- pointer to extra data
 */
BordoisilaDriverResource* imx8mp_alloc_kernel_resource(char* name, void* data) {
	BordoisilaClk* clk = (BordoisilaClk*)kmalloc(sizeof(BordoisilaClk));
	if (!clk) {
		BPrintK(BORDOISILA_ERROR, "imx8mp-clk failed to allocate kernel resource \r\n");
		return NULL;
	}
	strcpy(clk->res.name, name);
	clk->res.res_type = BORDOISILA_DRIVER_RES_CLK;
	clk->res.ref_count = 0;
	clk->res.data = data;
	clk->res.is_running = false;
	clk->enable = &kernel_res_clk_set_rate;
	clk->rate_hz = 0;
	clk->disable = 0;
	if (BordoisilaDriverResourceRegister((BordoisilaDriverResource*)clk)) {
		BPrintK(BORDOISILA_ERROR, "failed to register kernel clock resource : %s \r\n", name);
		return NULL;
	}
	return (BordoisilaDriverResource*)clk;
}

#define IMX8MP_SYS_PLL1_RATE_HZ 800000000UL
#define IMX8MP_SYS_PLL2_RATE_HZ 1000000000UL

static imx8mp_clk* g_sys_pll1_40m;
static imx8mp_clk* g_sys_pll1_80m;
static imx8mp_clk* g_sys_pll1_100m;
static imx8mp_clk* g_sys_pll1_133m;
static imx8mp_clk* g_sys_pll1_160m;
static imx8mp_clk* g_sys_pll1_200m;
static imx8mp_clk* g_sys_pll1_266m;
static imx8mp_clk* g_sys_pll1_400m;
static imx8mp_clk* g_sys_pll1_800m;

static imx8mp_clk* g_sys_pll2_50m;
static imx8mp_clk* g_sys_pll2_100m;
static imx8mp_clk* g_sys_pll2_125m;
static imx8mp_clk* g_sys_pll2_166m;
static imx8mp_clk* g_sys_pll2_200m;
static imx8mp_clk* g_sys_pll2_250m;
static imx8mp_clk* g_sys_pll2_333m;
static imx8mp_clk* g_sys_pll2_500m;
static imx8mp_clk* g_sys_pll2_1000m;
static imx8mp_clk* g_osc_24m;

static imx8mp_clk* g_clk_ext1;
static imx8mp_clk* g_clk_ext2;
static imx8mp_clk* g_clk_ext3;
static imx8mp_clk* g_clk_ext4;

static imx8mp_clk* g_video_pll1_out;
static imx8mp_clk* g_audio_pll1_out;
static imx8mp_clk* g_gpu_pll_out;

static imx8mp_clk* g_audio_pll2_out;
static imx8mp_clk* g_sys_pll3_out;

static void imx8mp_config_fixed_clock() {
	uint64_t parent = imx8mp_pll_get_parent_rate(__IMX8MP_SYS_PLL1_GEN_CTRL);

	uint64_t pll1_rate = imx8mp_pll_recalc_rate(__IMX8MP_SYS_PLL1_GEN_CTRL, parent);
	parent = imx8mp_pll_get_parent_rate(__IMX8MP_SYS_PLL2_GEN_CTRL);
	uint64_t pll2_rate = imx8mp_pll_recalc_rate(__IMX8MP_SYS_PLL2_GEN_CTRL, parent);

	parent = imx8mp_pll_get_parent_rate(__IMX8MP_SYS_PLL3_GEN_CTRL);
	uint64_t pll3_rate = imx8mp_pll_recalc_rate(__IMX8MP_SYS_PLL3_GEN_CTRL, parent);
	BPrintK(BORDOISILA_INFO,
			"imx8mp: configuring clock rate database, pll1 rate: %u, pll3 rate: %u \r\n",
			pll1_rate,
			pll3_rate);

	/**
	 * DO NOTE: video pll1 rate and audio pll1 rate are in P-O-R (Power on Reset) values
	 * maybe, we need to configure that using CCM analog base
	 */
	parent = imx8mp_pll_get_parent_rate(__IMX8MP_VIDEO_PLL1_GEN_CTRL);
	uint64_t video_pll1_rate = imx8mp_pll_recalc_rate(__IMX8MP_VIDEO_PLL1_GEN_CTRL, parent);

	parent = imx8mp_pll_get_parent_rate(__IMX8MP_AUDIO_PLL1_GEN_CTRL);
	uint64_t audio_pll1_rate = imx8mp_pll_recalc_rate(__IMX8MP_AUDIO_PLL1_GEN_CTRL, parent);

	parent = imx8mp_pll_get_parent_rate(__IMX8MP_AUDIO_PLL2_GEN_CTRL);
	uint64_t audio_pll2_rate = imx8mp_pll_recalc_rate(__IMX8MP_AUDIO_PLL2_GEN_CTRL, parent);

	parent = imx8mp_pll_get_parent_rate(__IMX8MP_GPU_PLL_GEN_CTRL);
	uint64_t gpu_pll_rate = imx8mp_pll_recalc_rate(__IMX8MP_GPU_PLL_GEN_CTRL, parent);

	BPrintK(BORDOISILA_INFO, "imx8mp gpu pll rate : %u \r\n", gpu_pll_rate);

	g_osc_24m = imx8mp_clk_fixed("osc_24m", 24000000);
	g_sys_pll1_800m = imx8mp_clk_fixed("sys_pll1_800m", pll1_rate);
	g_sys_pll1_400m = imx8mp_clk_fixed("sys_pll1_400m", pll1_rate / 2);
	g_sys_pll1_266m = imx8mp_clk_fixed("sys_pll1_266m", pll1_rate / 3);
	g_sys_pll1_200m = imx8mp_clk_fixed("sys_pll1_200m", pll1_rate / 4);
	g_sys_pll1_160m = imx8mp_clk_fixed("sys_pll1_160m", pll1_rate / 5);
	g_sys_pll1_133m = imx8mp_clk_fixed("sys_pll1_133m", pll1_rate / 6);
	g_sys_pll1_100m = imx8mp_clk_fixed("sys_pll1_100m", pll1_rate / 8);
	g_sys_pll1_80m = imx8mp_clk_fixed("sys_pll1_80m", pll1_rate / 10);
	g_sys_pll1_40m = imx8mp_clk_fixed("sys_pll1_40m", pll1_rate / 20);

	g_sys_pll2_1000m = imx8mp_clk_fixed("sys_pll2_1000m", pll2_rate / 1);
	g_sys_pll2_500m = imx8mp_clk_fixed("sys_pll2_500m", pll2_rate / 2);
	g_sys_pll2_333m = imx8mp_clk_fixed("sys_pll2_333m", pll2_rate / 3);
	g_sys_pll2_250m = imx8mp_clk_fixed("sys_pll2_250m", pll2_rate / 4);
	g_sys_pll2_200m = imx8mp_clk_fixed("sys_pll2_200m", pll2_rate / 5);
	g_sys_pll2_166m = imx8mp_clk_fixed("sys_pll2_166m", pll2_rate / 6);
	g_sys_pll2_125m = imx8mp_clk_fixed("sys_pll_125m", pll2_rate / 8);
	g_sys_pll2_100m = imx8mp_clk_fixed("sys_pll2_100m", pll2_rate / 10);
	g_sys_pll2_50m = imx8mp_clk_fixed("sys_pll2_50m", pll2_rate / 20);

	uint32_t ext1_hz = AuDeviceTreeGetFixedClockRate("clock-ext1");
	uint32_t ext2_hz = AuDeviceTreeGetFixedClockRate("clock-ext2");
	uint32_t ext3_hz = AuDeviceTreeGetFixedClockRate("clock-ext3");
	uint32_t ext4_hz = AuDeviceTreeGetFixedClockRate("clock-ext4");

	g_clk_ext1 = imx8mp_clk_fixed("clk_ext1", ext1_hz);
	g_clk_ext2 = imx8mp_clk_fixed("clk_ext2", ext2_hz);
	g_clk_ext3 = imx8mp_clk_fixed("clk_ext3", ext3_hz);
	g_clk_ext4 = imx8mp_clk_fixed("clk_ext4", ext4_hz);

	g_audio_pll1_out = imx8mp_clk_fixed("audio_pll1_out", audio_pll1_rate);
	g_video_pll1_out = imx8mp_clk_fixed("video_pll1_out", video_pll1_rate);
	g_audio_pll2_out = imx8mp_clk_fixed("audio_pll2_out", audio_pll2_rate);
	g_sys_pll3_out = imx8mp_clk_fixed("sys_pll3_out", pll3_rate);
	g_gpu_pll_out = imx8mp_clk_fixed("gpu_pll_out", gpu_pll_rate);
}

static void imx8mp_clk_set_rate(imx8mp_clk* self, uint32_t target_hz) {
	uint32_t best_err = UINT32_MAX;
	int best_parent = 0, best_pre = 0, best_post = 0;

	for (int p = 0; p < self->num_parent; p++) {
		uint32_t parent_rate = self->parent[p]->recalc_rate(self->parent[p]);
		for (uint32_t div = 1; div <= 512; div++) {
			uint32_t rate = parent_rate / div;
			uint32_t err = (rate > target_hz) ? rate - target_hz : target_hz - rate;
			if (err < best_err) {
				best_err = err;
				best_parent = p;

				for (uint32_t pre = 1; pre <= 8; pre++) {
					if (div % pre == 0 && div / pre <= 64) {
						best_pre = pre - 1;
						best_post = (div / pre) - 1;
						break;
					}
				}
				if (err == 0)
					goto done;
			}
		}
	}
done:
	self->current_parent_idx = best_parent;
	if (self->current_parent_idx == 6)
		self->current_parent_idx = 0;

	self->pre_podf = best_pre;
	self->post_podf = best_post;

	/** hard coding it right now **/
	/*if (self->clk_slice == MEDIA_DISP2_CLK_ROOT)
		self->current_parent_idx = 0;

	if (self->clk_slice == HDMI_APB_CLK_ROOT) {
		self->current_parent_idx = 0;
		self->post_podf = 0;
		self->pre_podf = 0;
	}

	if (self->clk_slice == HDMI_AXI_CLK_ROOT) {
		self->current_parent_idx = 1;
		self->pre_podf = 0;
		self->post_podf = 1;
	}

	if (self->clk_slice == HDMI_REF_266M_ROOT) {
		self->current_parent_idx = 4;
		self->pre_podf = 0;
		self->post_podf = 0;
	}

	if (self->clk_slice == MEDIA_DISP2_CLK_ROOT) {
		self->current_parent_idx = 0;
		self->pre_podf = 0;
		self->post_podf = 3;*/
	//}

	//if (self->clk_slice == HDMI_24M_ROOT) {
	//	self->current_parent_idx = 0;
	/*	self->pre_podf = 0;
		self->post_podf = 0;*/
	//}
	self->rate = self->parent[self->current_parent_idx]->rate;
	BPrintK(BORDOISILA_INFO,
			"using parent index : %d for clock : %s  \r\n",
			self->current_parent_idx,
			self->name);
	UARTDebugOut("rate %d \r\n", self->rate);
	BPrintK(BORDOISILA_INFO, "pre podf: %d, post podf : %d \r\n", self->pre_podf, self->post_podf);
	imx8mp_write_target_root(
		self->clk_slice, 0x0, self->current_parent_idx, self->pre_podf, self->post_podf);
}

static bool is_imx8mp_clk_enabled(uint32_t clk_idx) {
	volatile uint32_t* root = (volatile uint32_t*)CCM_ROOT_REG(_ccm_base, clk_idx);
	bool is_true = false;
	uint32_t rval = *root;
	if ((rval & (1u << 28)) != 0)
		is_true = true;

	return is_true;
}

static uint32_t imx8mp_clk_get_mux(uint32_t clk_idx) {
	volatile uint32_t* root = (volatile uint32_t*)CCM_ROOT_REG(_ccm_base, clk_idx);
	uint32_t rval = *root;
	return (rval >> 24) & 0x7u;
}

#define FORM_CLK_COMPOSITE(name, parent, n_parent) imx8mp_clk_composite(name, parent, n_parent)

/**
 *imx8mp_void_ccm_init -- map the ccm module
 */
void imx8mp_ccm_init() {
	AuTextOut("[imx8mp_board]: initializing clock control module (ccm) \r\n");
	_ccm_base = (uint64_t)AuMapMMIO(CCM_BASE, 16);

	for (int i = 0; i < 100; i++) {
		memset(&_clk_node[i], 0, sizeof(imx8mp_clk));
		//memset(&_assigned_clk[i], 0, sizeof(imx8mp_clk));
	}

	imx8mp_config_fixed_clock();

	/**
	 * Reference: Linux source code, clk-imx8mp.c
	 */

	static imx8mp_clk* media_axi_parents[8];
	media_axi_parents[0] = g_osc_24m;
	media_axi_parents[1] = g_sys_pll2_1000m;
	media_axi_parents[2] = g_sys_pll1_800m;
	media_axi_parents[3] = g_sys_pll3_out;
	media_axi_parents[4] = g_sys_pll1_40m;
	media_axi_parents[5] = g_audio_pll2_out;
	media_axi_parents[6] = g_clk_ext1;
	media_axi_parents[7] = g_sys_pll2_500m;
	imx8mp_clk* media_axi_clk = FORM_CLK_COMPOSITE("media_axi_axi", media_axi_parents, 8);
	media_axi_clk->clk_slice = MEDIA_AXI_CLK_ROOT;
	media_axi_clk->gate_slice = IMX8MP_CLK_MEDIA_AXI_ROOT;
	imx8mp_alloc_kernel_resource("media_axi", media_axi_clk);

	static imx8mp_clk* media_apb_parents[8];
	media_apb_parents[0] = g_osc_24m;
	media_apb_parents[1] = g_sys_pll2_125m;
	media_apb_parents[2] = g_sys_pll1_800m;
	media_apb_parents[3] = g_sys_pll3_out;
	media_apb_parents[4] = g_sys_pll1_40m;
	media_apb_parents[5] = g_audio_pll2_out;
	media_apb_parents[6] = g_clk_ext1;
	media_apb_parents[7] = g_sys_pll1_133m;
	imx8mp_clk* media_apb_clk = FORM_CLK_COMPOSITE("media_apb_axi", media_apb_parents, 8);
	media_apb_clk->clk_slice = MEDIA_APB_CLK_ROOT;
	media_apb_clk->gate_slice = IMX8MP_CLK_MEDIA_APB_ROOT;
	imx8mp_alloc_kernel_resource("media_apb", media_apb_clk);

	static imx8mp_clk* gpu3d_sels[8];
	gpu3d_sels[0] = g_osc_24m;
	gpu3d_sels[1] = g_gpu_pll_out;
	gpu3d_sels[2] = g_sys_pll1_800m;
	gpu3d_sels[3] = g_sys_pll3_out;
	gpu3d_sels[4] = g_sys_pll2_1000m;
	gpu3d_sels[5] = g_audio_pll1_out;
	gpu3d_sels[6] = g_video_pll1_out;
	gpu3d_sels[7] = g_audio_pll2_out;
	imx8mp_clk* gpu3d_clk = FORM_CLK_COMPOSITE("gpu3d_core", gpu3d_sels, 8);
	gpu3d_clk->clk_slice = GPU3D_CORE_CLK_ROOT;
	gpu3d_clk->gate_slice = IMX8MP_CLK_GPU3D_ROOT;
	imx8mp_alloc_kernel_resource("gpu3d_core", gpu3d_clk);

	static imx8mp_clk* gpu3d_shader[8];
	gpu3d_shader[0] = g_osc_24m;
	gpu3d_shader[1] = g_gpu_pll_out;
	gpu3d_shader[2] = g_sys_pll1_800m;
	gpu3d_shader[3] = g_sys_pll3_out;
	gpu3d_shader[4] = g_sys_pll2_1000m;
	gpu3d_shader[5] = g_audio_pll1_out;
	gpu3d_shader[6] = g_video_pll1_out;
	gpu3d_shader[7] = g_audio_pll2_out;
	imx8mp_clk* gpu3d_sel = FORM_CLK_COMPOSITE("gpu3d_shader", gpu3d_shader, 8);
	gpu3d_sel->clk_slice = GPU3D_SHADER_CLK_ROOT;
	gpu3d_sel->gate_slice = IMX8MP_CLK_GPU3D_ROOT;
	imx8mp_alloc_kernel_resource("gpu3d_shader", gpu3d_sel);

	//TODO: add more composite clocks
	static imx8mp_clk* gpu2d_sels[8];
	gpu2d_sels[0] = g_osc_24m;
	gpu2d_sels[1] = g_gpu_pll_out;
	gpu2d_sels[2] = g_sys_pll1_800m;
	gpu2d_sels[3] = g_sys_pll3_out;
	gpu2d_sels[4] = g_sys_pll2_1000m;
	gpu2d_sels[5] = g_audio_pll1_out;
	gpu2d_sels[6] = g_video_pll1_out;
	gpu2d_sels[7] = g_audio_pll2_out;
	imx8mp_clk* gpu2d_clk = FORM_CLK_COMPOSITE("gpu2d_clk", gpu2d_sels, 8);
	gpu3d_sel->clk_slice = GPU2D_CLK_ROOT;
	gpu3d_sel->gate_slice = IMX8MP_CLK_GPU3D_ROOT;
	imx8mp_alloc_kernel_resource("gpu2d_clk", gpu2d_clk);

	static imx8mp_clk* audio_axi_sels[8];
	audio_axi_sels[0] = g_osc_24m;
	audio_axi_sels[1] = g_gpu_pll_out;
	audio_axi_sels[2] = g_sys_pll1_800m;
	audio_axi_sels[3] = g_sys_pll3_out;
	audio_axi_sels[4] = g_sys_pll2_1000m;
	audio_axi_sels[5] = g_audio_pll1_out;
	audio_axi_sels[6] = g_video_pll1_out;
	audio_axi_sels[7] = g_audio_pll2_out;
	imx8mp_clk* audio_axi_clk = FORM_CLK_COMPOSITE("audio_axi_clk", audio_axi_sels, 8);
	audio_axi_clk->clk_slice = AUDIO_AXI_CLK_ROOT;
	audio_axi_clk->gate_slice = 0;
	imx8mp_alloc_kernel_resource("audio_axi_clk", audio_axi_clk);

	static imx8mp_clk* hsio_axi_sels[8];
	hsio_axi_sels[0] = g_osc_24m;
	hsio_axi_sels[1] = g_sys_pll2_500m;
	hsio_axi_sels[2] = g_sys_pll1_800m;
	hsio_axi_sels[3] = g_sys_pll2_100m;
	hsio_axi_sels[4] = g_sys_pll2_200m;
	hsio_axi_sels[5] = g_clk_ext2;
	hsio_axi_sels[6] = g_clk_ext4;
	hsio_axi_sels[7] = g_audio_pll2_out;
	imx8mp_clk* hsio_axi_clk = FORM_CLK_COMPOSITE("hsio_axi_clk", hsio_axi_sels, 8);
	hsio_axi_clk->clk_slice = HSIO_AXI_CLK_ROOT;
	hsio_axi_clk->gate_slice = 0;
	imx8mp_alloc_kernel_resource("hsio_axi_clk", hsio_axi_clk);

	static imx8mp_clk* media_isp_sels[8];
	media_isp_sels[0] = g_osc_24m;
	media_isp_sels[1] = g_sys_pll2_1000m;
	media_isp_sels[2] = g_sys_pll1_800m;
	media_isp_sels[3] = g_sys_pll3_out;
	media_isp_sels[4] = g_sys_pll1_400m;
	media_isp_sels[5] = g_audio_pll2_out;
	media_isp_sels[6] = g_clk_ext1;
	media_isp_sels[7] = g_sys_pll2_500m;
	imx8mp_clk* media_isp_clk = FORM_CLK_COMPOSITE("media_isp_clk", media_isp_sels, 8);
	media_isp_clk->clk_slice = MEDIA_ISP_CLK_ROOT;
	media_isp_clk->gate_slice = 0;
	imx8mp_alloc_kernel_resource("media_isp_clk", media_isp_clk);

	static imx8mp_clk* media_disp_pix_sels[8];
	media_disp_pix_sels[0] = g_osc_24m;
	media_disp_pix_sels[1] = g_video_pll1_out;
	media_disp_pix_sels[2] = g_audio_pll2_out;
	media_disp_pix_sels[3] = g_audio_pll1_out;
	media_disp_pix_sels[4] = g_sys_pll1_800m;
	media_disp_pix_sels[5] = g_sys_pll2_1000m;
	media_disp_pix_sels[6] = g_sys_pll3_out;
	media_disp_pix_sels[7] = g_clk_ext4;
	imx8mp_clk* media_disp2_clk = FORM_CLK_COMPOSITE("media_disp2_pix_clk", media_disp_pix_sels, 8);
	media_disp2_clk->clk_slice = MEDIA_DISP2_CLK_ROOT; //0x9300
	media_disp2_clk->gate_slice = IMX8MP_CLK_MEDIA_DISP2_PIX_ROOT;
	imx8mp_alloc_kernel_resource("media_disp2_pix_clk", media_disp2_clk);

	imx8mp_clk* media_disp1_clk = FORM_CLK_COMPOSITE("media_disp1_pix_clk", media_disp_pix_sels, 8);
	media_disp1_clk->clk_slice = MEDIA_DISP1_PIX_CLK_ROOT; //0xbe00
	media_disp1_clk->gate_slice = IMX8MP_CLK_MEDIA_DISP1_PIX_ROOT;
	imx8mp_alloc_kernel_resource("media_disp1_pix_clk", media_disp1_clk);

	imx8mp_clk* hdmi_apb = FORM_CLK_COMPOSITE("hdmi_apb", media_apb_parents, 8);
	hdmi_apb->clk_slice = HDMI_APB_CLK_ROOT;
	hdmi_apb->gate_slice = IMX8MP_CLK_HDMI_ROOT;
	imx8mp_alloc_kernel_resource("hdmi_apb", hdmi_apb);

	imx8mp_clk* hdmi_axi = FORM_CLK_COMPOSITE("hdmi_axi", media_axi_parents, 8);
	hdmi_axi->clk_slice = HDMI_AXI_CLK_ROOT;
	hdmi_axi->gate_slice = IMX8MP_CLK_HDMI_ROOT;
	imx8mp_alloc_kernel_resource("hdmi_axi", hdmi_axi);

	static imx8mp_clk* hdmi_24m_sels[8];
	hdmi_24m_sels[0] = g_osc_24m;
	hdmi_24m_sels[1] = g_sys_pll1_160m;
	hdmi_24m_sels[2] = g_sys_pll2_50m;
	hdmi_24m_sels[3] = g_sys_pll3_out;
	hdmi_24m_sels[4] = g_audio_pll1_out;
	hdmi_24m_sels[5] = g_video_pll1_out;
	hdmi_24m_sels[6] = g_audio_pll2_out;
	hdmi_24m_sels[7] = g_sys_pll1_133m;
	imx8mp_clk* hdmi_24m = FORM_CLK_COMPOSITE("hdmi_24m", hdmi_24m_sels, 8);
	hdmi_24m->clk_slice = HDMI_24M_ROOT;
	hdmi_24m->gate_slice = IMX8MP_CLK_XTAL_ROOT;
	imx8mp_alloc_kernel_resource("hdmi_24m", hdmi_24m);

	static imx8mp_clk* hdmi_ref_266m_sels[8];
	hdmi_ref_266m_sels[0] = g_osc_24m;
	hdmi_ref_266m_sels[1] = g_sys_pll1_400m;
	hdmi_ref_266m_sels[2] = g_sys_pll3_out;
	hdmi_ref_266m_sels[3] = g_sys_pll2_333m;
	hdmi_ref_266m_sels[4] = g_sys_pll1_266m;
	hdmi_ref_266m_sels[5] = g_sys_pll2_200m;
	hdmi_ref_266m_sels[6] = g_audio_pll1_out;
	hdmi_ref_266m_sels[7] = g_video_pll1_out;
	imx8mp_clk* hdmi_266m = FORM_CLK_COMPOSITE("hdmi_266m", hdmi_ref_266m_sels, 8);
	hdmi_266m->clk_slice = HDMI_REF_266M_ROOT;
	hdmi_266m->gate_slice = IMX8MP_CLK_PLL_ROOT;
	imx8mp_alloc_kernel_resource("hdmi_ref_266m", hdmi_266m);
}

static void imx8mp_write_target_root(uint32_t clk_root_idx,
									 uint32_t offset,
									 uint32_t mux_val,
									 uint32_t pre_podf,
									 uint32_t post_podf) {
	volatile uint32_t* root = (volatile uint32_t*)(CCM_ROOT_REG(_ccm_base, clk_root_idx) + offset);

	uint32_t val = *root;

	val &= ~(1U << 28);
	*root = val;

	dsb_ish();
	isb_flush();

	val &= ~(0x7u << 24);
	val |= (mux_val & 0x7u) << 24;
	val &= ~(0x7u << 16);
	val |= (pre_podf & 0x7u) << 16;
	val &= ~(0x3Fu << 0);
	val |= (post_podf & 0x3Fu) << 0;

	// enable the clock
	val |= (1u << 28);
	*root = val;

	dsb_ish();
	isb_flush();

	BPrintK(BORDOISILA_WARN, "imx8mp target root written successfully address : %x \r\n", root);
	//for safety :-) hihi
	for (int i = 0; i < 100; i++)
		;

	uint32_t confirm = *root;
	if (((confirm >> 24) & 0x7u) != mux_val || ((confirm >> 28) & 0x1u) != 1u) {
		BPrintK(BORDOISILA_WARN,
				"imx8mp target root write mismatch at offset : %x, wanted mux = %x got = %x \r\n",
				root,
				mux_val,
				((confirm >> 24) & 0x7u));
	}
}
/**
 * imx8mp_ccm_write -- write value to clock indexed register
 */
void imx8mp_ccm_write(uint32_t clk_root_idx, int offset, uint32_t value) {
	(*(volatile uint32_t*)(CCM_ROOT_REG(_ccm_base, clk_root_idx) + offset)) = value;
	dsb_sy_barrier();
	isb_flush();
}

uint64_t imx8mp_ccm_get_base() {
	return _ccm_base;
}

#endif
