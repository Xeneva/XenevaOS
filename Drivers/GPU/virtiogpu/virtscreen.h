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

#ifndef __VIRT_SCREEN_H__
#define __VIRT_SCREEN_H__

#include <Drivers/virtio.h>
#include <stdint.h>

/**
 * @brief virt_gpu_screen_init -- initialize screen
 * @param cfg --  pointer to virtio common config
 * @param width -- width of the screen
 * @param height -- height of the screen
 * @return resource id allocated for this screen
 */
extern int virt_gpu_screen_init(VirtioCommonCfg* cfg, uint32_t width, uint32_t height);

/**
 * @brief virt_gpu_alloc_fb -- allocate framebuffer for scanout
 * @param cfg -- pointer to virtio common config
 * @param resource_id -- resource id number
 */
extern void virt_gpu_alloc_fb(VirtioCommonCfg* cfg, int resource_id);

/** Release the fallback driver's scanout backing after the device is reset. */
extern void virt_gpu_release_fb(void);


/**
 * @brief virt_gpu_set_scanout -- initialize the scanout to the given
 * screen
 * @param cfg -- Pointer to virtio common config
 * @param resource_id -- screen resource id
 */
extern void virt_gpu_set_scanout(VirtioCommonCfg* cfg, int resource_id, int scanout_id);

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
extern void virt_gpu_transfer_to_host2d(VirtioCommonCfg* cfg, int resource_id, int x, int y, int w, int h);

/**
 * @brief virt_gpu_flush -- flush graphics from guest to host
 * @param cfg -- pointer to virtio common config
 * @param resource_id -- screen resource id
 */
extern void virt_gpu_flush(VirtioCommonCfg* cfg, int resource_id);

/**
 * @brief virt_gpu_fill_screen -- fill the screen with specific color
 * @param width -- width of the screen
 * @param height -- height of the screen
 * @param color -- color to fill with
 */
extern void virt_gpu_fill_screen(uint32_t width, uint32_t height, uint32_t color);


#endif
