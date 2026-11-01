/**
* @file virtscreen.cpp
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
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

#include "virtiogpu.h"
#include <stdint.h>
#include <Drivers/virtio.h>
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <Drivers/uart.h>
#include <aucon.h>
#include <aurora.h>
#include <aucon.h>

static uint32_t virt_display_width;
static uint32_t virt_display_height;
static uint32_t* fb;
static uint64_t fb_phys;
static uint32_t fb_pages;

/**
 * @brief virt_gpu_screen_init -- initialize screen
 * @param cfg --  pointer to virtio common config
 * @param width -- width of the screen
 * @param height -- height of the screen
 * @return resource id allocated for this screen
 */
int virt_gpu_screen_init(VirtioCommonCfg* cfg, uint32_t width, uint32_t height) {
	virt_display_width = width;
	virt_display_height = height;
	int resource_id = gpu_allocate_resource_id();

	virtio_gpu_resource_create_2d cmd;
	cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	cmd.hdr.flags = 0;
	cmd.hdr.fence_id = 0;
	cmd.hdr.ctx_id = 0;
	cmd.hdr.padding = 0;
	cmd.resource_id = resource_id;
	cmd.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
	cmd.height = height;
	cmd.width = width;
	gpu_execute_command(cfg, &cmd, sizeof(virtio_gpu_resource_create_2d));
	UARTDebugOut("[virtio-gpu]: screen initialized id : %d\r\n", resource_id);
	return resource_id;
}

#define GPU_FB_BUFFER 0xFFFFD00000700000

/**
 * @brief virt_gpu_alloc_fb -- allocate framebuffer for scanout
 * @param cfg -- pointer to virtio common config
 * @param resource_id -- resource id number
 */
void virt_gpu_alloc_fb(VirtioCommonCfg* cfg, int resource_id) {
	size_t len = virt_display_height * virt_display_width * sizeof(uint32_t);
	size_t fb_sz = (len + PAGE_SIZE - 1) / PAGE_SIZE;
	/* ATTACH_BACKING below reports one entry spanning the whole buffer, so
	 * the backing pages must actually be contiguous, not just individually
	 * allocated. --axiss */
	uint64_t allocated = (uint64_t)AuPmmngrAllocPages((uint32_t)fb_sz, 1, 0, AURORA_PAGE_NORMAL);
	if (!allocated || allocated == UINT64_MAX)
		return;
	fb_phys = allocated;
	fb_pages = (uint32_t)fb_sz;
	fb = (uint32_t*)P2V(fb_phys);
	for (int i = 0; i < fb_sz; i++)
		AuMapPage(fb_phys + i * PAGE_SIZE, GPU_FB_BUFFER + i * PAGE_SIZE, PTE_NORMAL_NON_CACHEABLE);

	virtio_gpu_resource_attach_backing attach;
	attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
	attach.resource_id = resource_id;
	attach.nr_entries = 1;
	attach.entries[0].addr = fb_phys;
	attach.entries[0].length = len;
	gpu_attach_back_cmd(cfg,
						&attach,
						(uint32_t)OFFSETOF(virtio_gpu_resource_attach_backing, entries),
						&attach.entries[0],
						(uint32_t)sizeof(attach.entries[0]));
	KERNEL_BOOT_INFO* binfo = AuGetBootInfoStruc();
	if (binfo && (uint64_t)binfo >= KERNEL_BASE_ADDRESS) {
		binfo->graphics_framebuffer = (uint32_t*)fb_phys;
		binfo->fb_size = len;
		binfo->X_Resolution = virt_display_width;
		binfo->Y_Resolution = virt_display_height;
		binfo->pixels_per_line = binfo->X_Resolution;
	}
	AuConsoleSetConInfo(fb_phys, GPU_FB_BUFFER, virt_display_width, virt_display_height);
	UARTDebugOut(
		"[virtio-gpu]: framebuffer attached to screen id : %d %d\r\n", resource_id, (int)len);
}

void virt_gpu_release_fb(void) {
	if (!fb_phys || !fb_pages)
		return;
	AuFreePages(GPU_FB_BUFFER, false, (size_t)fb_pages * PAGE_SIZE);
	AuPmmngrReleasePages(fb_phys);
	fb = NULL;
	fb_phys = 0;
	fb_pages = 0;
}

/**
 * @brief virt_gpu_set_scanout -- initialize the scanout to the given
 * screen
 * @param cfg -- Pointer to virtio common config
 * @param resource_id -- screen resource id
 */
void virt_gpu_set_scanout(VirtioCommonCfg* cfg, int resource_id, int scanout_id) {
	virtio_gpu_set_scanout cmd;
	cmd.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
	cmd.hdr.flags = 0;
	cmd.hdr.fence_id = 0;
	cmd.hdr.ctx_id = 0;
	cmd.hdr.padding = 0;
	cmd.resource_id = resource_id;
	cmd.scanout_id = scanout_id;
	cmd.rect.height = virt_display_height;
	cmd.rect.width = virt_display_width;
	cmd.rect.x = 0;
	cmd.rect.y = 0;
	gpu_execute_command(cfg, &cmd, sizeof(virtio_gpu_set_scanout));
	UARTDebugOut(
		"[virtio-gpu]: scanout : %d attached to screen id : %d \r\n", scanout_id, resource_id);
}

/**
 * @brief virt_gpu_transfer_to_host2d -- transfer buffers from guest to
 * host
 * @param cfg -- Pointer to virtio common config
 * @param resource_id -- screen resource id
 * @param x -- X coord of the rect
 * @param y -- Y coord of the rect
 * @param w -- Width of the rect
 * @param h -- Height of the rect
 */
void virt_gpu_transfer_to_host2d(
	VirtioCommonCfg* cfg, int resource_id, int x, int y, int w, int h) {
	virtio_gpu_transfer_to_host_2d host2d;
	host2d.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST2D;
	host2d.rect.x = x;
	host2d.rect.y = y;
	host2d.rect.width = w;
	host2d.rect.height = h;
	host2d.offset = y * (virt_display_width * 4) + x * 4;
	host2d.resource_id = resource_id;
	gpu_execute_command(cfg, &host2d, sizeof(virtio_gpu_transfer_to_host_2d));
}

/**
 * @brief virt_gpu_flush -- flush graphics from guest to host
 * @param cfg -- pointer to virtio common config
 * @param resource_id -- screen resource id
 */
void virt_gpu_flush(VirtioCommonCfg* cfg, int resource_id) {
	virtio_gpu_resource_flush flush;
	flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	flush.resource_id = resource_id;
	flush.rect.x = 0;
	flush.rect.y = 0;
	flush.rect.width = virt_display_width;
	flush.rect.height = virt_display_height;
	gpu_execute_command(cfg, &flush, sizeof(virtio_gpu_resource_flush));
}

/**
 * @brief virt_gpu_flush_rect -- flush graphics from guest to host
 * only with given rect area
 * @param cfg -- pointer to virtio common config
 * @param resource_id -- screen resource id
 */
void virt_gpu_flush_rect(VirtioCommonCfg* cfg, int resource_id, int x, int y, int w, int h) {
	virtio_gpu_resource_flush flush;
	flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	flush.resource_id = resource_id;
	flush.rect.x = x;
	flush.rect.y = y;
	flush.rect.width = w;
	flush.rect.height = h;

	gpu_execute_command(cfg, &flush, sizeof(virtio_gpu_resource_flush));
}

static void virt_gpu_put_pxl(uint32_t x, uint32_t y, uint32_t color) {
	uint32_t* lfb = (uint32_t*)GPU_FB_BUFFER;
	lfb[static_cast<uint64_t>(y) * virt_display_width + x] = color;
}

/**
 * @brief VirtGpuConsolePresent -- mirror console damage onto the scanout
 * Copies rows from the console framebuffer (arbitrary pitch) into our
 * tightly-packed backing, then transfers and flushes exactly that rect.
 * Runs inline in the writer's context; the submit path only spin-polls,
 * never sleeps. Clipped to the resource. --axiss
 */
void VirtGpuConsolePresent(uint32_t* src, uint32_t src_pitch, int x, int y, int w, int h) {
	if (!src || src_pitch < 4)
		return;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x >= (int)virt_display_width || y >= (int)virt_display_height)
		return;
	if (w > (int)virt_display_width - x)
		w = (int)virt_display_width - x;
	if (h > (int)virt_display_height - y)
		h = (int)virt_display_height - y;
	if (w <= 0 || h <= 0)
		return;
	uint32_t* dst = (uint32_t*)GPU_FB_BUFFER;
	for (int j = 0; j < h; j++) {
		uint32_t* s = (uint32_t*)((uint8_t*)src + (uint64_t)(y + j) * src_pitch) + x;
		uint32_t* d = dst + (uint64_t)(y + j) * virt_display_width + x;
		for (int i = 0; i < w; i++)
			d[i] = s[i];
	}
	VirtioCommonCfg* cfg = gpu_get_config_pointer();
	int id = virt_gpu_default_resource_id();
	if (!cfg || !id)
		return;
	virt_gpu_transfer_to_host2d(cfg, id, x, y, w, h);
	virt_gpu_flush_rect(cfg, id, x, y, w, h);
}

/**
 * @brief virt_gpu_fill_screen -- fill the screen with specific color
 * @param width -- width of the screen
 * @param height -- height of the screen
 * @param color -- color to fill with
 */
void virt_gpu_fill_screen(uint32_t width, uint32_t height, uint32_t color) {
	for (int i = 0; i < height; i++)
		for (int j = 0; j < width; j++)
			virt_gpu_put_pxl(j, i, color);
}
