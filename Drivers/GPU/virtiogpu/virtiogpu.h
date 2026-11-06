/**
* @file virtgpu.h
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

#ifndef __VIRTIO_GPU_H__
#define __VIRTIO_GPU_H__

#include <stdint.h>
#include <aurora.h>
#include <Drivers/virtio.h>

#define VIRTIO_GPU_FLAG_FENCE (1<<0)
#define VIRTIO_GPU_FLAG_INFO_RING_IDX (1<<1)

#define VIRTIO_GPU_CREATE_RESOURCE_2D 0x200
#define VIRTIO_GPU_ATTACH_BACKING 0x201
#define VIRTIO_GPU_TRANSFER_TO_HOST2D 0x202
#define VIRTIO_GPU_FLUSH 0x203
#define VIRTIO_GPU_GET_SCREEN_RSRC_ID 0x204


#pragma pack(push,1)
struct virtio_gpu_ctrl_hdr {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	uint32_t ctx_id;
	uint32_t padding;
};
#pragma pack(pop)

#pragma pack(push,1)
struct virtio_gpu_resource_create_2d {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
};
#pragma pack(pop)

#pragma pack(push,1)
struct virtio_gpu_resource_attach_backing {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t nr_entries;
	struct {
		uint64_t addr;
		uint32_t length;
		uint32_t padding;
	}entries[1];
};
#pragma pack(pop)


struct virtio_gpu_rect {
	uint32_t x, y, width, height;
};

#pragma pack(push,1)
struct virtio_gpu_set_scanout {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect rect;
	uint32_t scanout_id;
	uint32_t resource_id;
};
#pragma pack(pop)

#pragma pack(push,1)
struct virtio_gpu_transfer_to_host_2d {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect rect;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t padding;
};
#pragma pack(pop)


#pragma pack(push,1)
struct virtio_gpu_resource_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect rect;
	uint32_t resource_id;
	uint32_t padding;
};
#pragma pack(pop)


#pragma pack(push,1)
struct virtio_gpu_config {
	uint32_t events_read;
	uint32_t events_clear;
	uint32_t num_scanouts;
	uint32_t num_capsets;
};
#pragma pack(pop)

#pragma pack(push,1)
struct virtio_gpu_mem_entry {
	uint64_t addr;
	uint32_t length;
	uint32_t padding;
};
#pragma pack(pop)


enum virtio_gpu_ctrl_type {
	/* 2D commands */
	VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
	VIRTIO_GPU_CMD_RESOURCE_UNREF,
	VIRTIO_GPU_CMD_SET_SCANOUT,
	VIRTIO_GPU_CMD_RESOURCE_FLUSH,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST2D,
	VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
	VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
	VIRTIO_GPU_CMD_GET_CAPSET_INFO,
	VIRTIO_GPU_CMD_GET_CAPSET,
	VIRTIO_GPU_CMD_GET_EDID,
	VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
	VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

	/*3D commands */
	VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
	VIRTIO_GPU_CMD_CTX_DESTROY,
	VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
	VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
	VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
	VIRTIO_GPU_CMD_SUBMIT_3D,
	VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
	VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

	/* cursor commands */
	VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
	VIRTIO_GPU_CMD_MOVE_CURSOR,

	/* success responses */
	VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
	VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET,
	VIRTIO_GPU_RESP_OK_EDID,
	VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
	VIRTIO_GPU_RESP_OK_MAP_INFO,

	/* error responses */
	VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
	VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
	VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

enum virtio_gpu_formats {
	VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM = 1,
	VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM = 2,
	VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM = 3,
	VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM = 4,

	VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM = 67,
	VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM = 68,

	VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM = 121,
	VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM = 134,
};
/**
 * @brief gpu_is_virgl_supported -- returns if
 * virgl is supported by host
 * @return true if supported, false if not supported
 */
extern bool gpu_is_virgl_supported();

/**
 * @brief gpu_is_edid_supported -- returns if
 * edid blob is supported by host
 * @return true if supported, false if not supported
 */
extern bool gpu_is_edid_supported();


/**
 * @brief gpu_execute_command -- submit a command to gpu
 * @param cfg -- pointer to virtio common config
 * @param cmd -- Pointer to command struct
 * @param len -- total length of the command
 */
extern void gpu_execute_command(VirtioCommonCfg* cfg, void* cmd, size_t len);

/**
 * @brief gpu_allocate_resource_id -- allocates
 * resource for commands
 * @return newly allocated resource id
 */
extern int gpu_allocate_resource_id();

/**
 * @brief gpu_attach_back_cmd -- attach backing stroe command needs two descriptor
 * previous gpu_execute_command won't satisfy this
 * @param cfg -- pointer to virtio common config
 * @param req -- pointer to request command one
 * @param len1 -- request command one length
 * @param req2 -- pointer to request command two
 * @param len2 -- request command two length
 */
extern void gpu_attach_back_cmd(VirtioCommonCfg* cfg, void* req, uint32_t len1, void* req2, uint32_t len2);

/**
 * @brief gpu_get_config_pointer -- return the pointer
 * virtio common config descriptor from pcie
 * @return system config descriptor from pcie config space
 */
extern VirtioCommonCfg* gpu_get_config_pointer();

/**
 * @brief virt_gpu_default_resource_id -- resource id of the scanout
 * created at driver init (the one the console mirror presents to)
 */
extern int virt_gpu_default_resource_id();

/**
 * @brief VirtGpuConsolePresent -- copy a console damage rect into the
 * scanout backing and transfer+flush exactly that rect. Installed as the
 * kernel console present hook; aucon only calls it while no compositor
 * owns the display. Exported: under this toolchain AU_EXPORT is the only
 * thing lld honors for COFF exports, and the kernel resolves the hook by
 * plain name from our export table -- an unexported symbol silently never
 * matches (C linkage alone is not enough). --axiss
 */
AU_EXTERN AU_EXPORT void VirtGpuConsolePresent(uint32_t* src, uint32_t src_pitch,
	int x, int y, int w, int h);




#endif