/**
* @file cmd2d.h
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

#ifndef __COMMAND_2D_H__
#define __COMMAND_2D_H__

#include <stdint.h>
#include <Drivers/virtio.h>
#include "virtiogpu.h"



/**
 * @brief virtio_cmd2d_create2d -- create a 2d resource
 * @param resource_id -- user defined resource id value
 * @param width -- width of the resource in pixel
 * @param height -- height of the resource in pixel
 */
extern void virtio_cmd2d_create(int resource_id, int width, int height);
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
 * @brief virt_gpu_flush_rect -- flush graphics from guest to host
 * only with given rect area
 * @param cfg -- pointer to virtio common config
 * @param resource_id -- screen resource id
 */
extern void virt_gpu_flush_rect(VirtioCommonCfg* cfg, int resource_id, int x, int y, int w, int h);



#endif
