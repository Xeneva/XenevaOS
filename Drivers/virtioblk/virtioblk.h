/**
* @file virtioblk.h
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

#ifndef __VIRTIO_BLK_H__
#define __VIRTIO_BLK_H__

#include <aurora.h>

#define VIRTIO_BLK_BLOCK_SZ 512

#define VIRTIO_BLK_OP_IN           0
#define VIRTIO_BLK_OP_OUT          1
#define VIRTIO_BLK_T_FLUSH         4
#define VIRTIO_BLK_T_GET_ID        8
#define VIRTIO_BLK_T_GET_LIFETIME 10
#define VIRTIO_BLK_T_DISCARD      11
#define VIRTIO_BLK_T_WRITE_ZEROES 13
#define VIRTIO_BLK_T_SECURE_ERASE 14



enum {
	VIRTIO_BLK_S_OK,
	VIRTIO_BLK_S_IOERR,
	VIRTIO_BLK_S_UNSUPP
};

#define RQ_BUFFERS 32
#define VIRTIO_BLK_F_RO  (1u << 5)

typedef struct {
	uint32_t type;
	uint32_t reserved;
	uint64_t sector;
}virtio_blk_req_header_t;

typedef struct {
	uint8_t status;
}virtio_blk_req_footer_t;

typedef struct {
	uint64_t capacity;
}virtio_blk_cfg_t;

struct virtio_blk_discard_write_zeroes {
	uint64_t sector;
	uint32_t num_sectors;
	struct {
		uint32_t unmap : 1;
		uint32_t reserved : 31;
	}flags;
};

struct virtio_blk_lifetime {
	uint16_t pre_eol_info;
	uint64_t device_lifetime_est_type_a;
	uint64_t device_lifetime_est_type_b;
};

struct virtioblk_dev_config {
	uint64_t capacity;
	uint32_t size_max;
	uint32_t seg_max;
	struct virtio_blk_geometry {
		uint16_t cylinders;
		uint8_t heads;
		uint8_t sectors;
	}geometry;
	uint32_t blk_sz;
	struct virtio_blk_topology {
		uint8_t physical_block_exp;
		uint8_t alignment_offset;
		uint16_t min_io_size;
		uint32_t opt_io_size;
	}topology;
	uint8_t writeback;
	uint8_t unused0;
	uint16_t num_queues;
	uint32_t max_discard_sectors;
	uint32_t max_discard_seg;
	uint32_t discard_sector_alignment;
	uint32_t max_write_zeroes_sectors;
	uint32_t max_write_zeroes_seg;
	uint8_t write_zeroes_may_unmap;
	uint8_t unused[3];
	uint32_t max_secure_erase_sectors;
	uint32_t max_secure_erase_seg;
	uint32_t secure_erase_sector_alignment;
	struct virtio_blk_zoned_characteristics {
		uint32_t zone_sectors;
		uint32_t max_open_zones;
		uint32_t max_active_zones;
		uint32_t max_append_sectors;
		uint32_t write_granularity;
		uint8_t model;
		uint8_t unused2[3];
	}zoned;
};
#endif
