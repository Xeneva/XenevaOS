/**
* @file cmd2d.cpp
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

#include "virtscreen.h"
#include "virtiogpu.h"
#include <Drivers/virtio.h>

/**
 * @brief virtio_cmd2d_create2d -- create a 2d resource 
 * @param resource_id -- user defined resource id value 
 * @param width -- width of the resource in pixel
 * @param height -- height of the resource in pixel
 */
void virtio_cmd2d_create(int resource_id, int width, int height) {
	VirtioCommonCfg* cfg = gpu_get_config_pointer();
	virtio_gpu_resource_create_2d create2d;
	create2d.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	create2d.hdr.flags = 0;
	create2d.hdr.fence_id = 0;
	create2d.hdr.ctx_id = 0;
	create2d.hdr.padding = 0;
	create2d.resource_id = resource_id;
	create2d.format = VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM;
	create2d.width = width;
	create2d.height = height;
	gpu_execute_command(cfg, &create2d, sizeof(virtio_gpu_resource_create_2d));
}