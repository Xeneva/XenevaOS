/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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


#ifndef __EXT_2_H__
#define __EXT_2_H__

#include <stdint.h>
#include <Fs/vdisk.h>

#define EXT2_SUPER_BLOCK_MAGIC 0xEF53
#define EXT2_DIRECT_BLOCKS 12

#pragma pack(push,1)
typedef struct _ext2_sb_ {
	uint32_t inodes_count;
	uint32_t blocks_count;
	uint32_t r_blocks_count;
	uint32_t free_blocks_count;
	uint32_t free_inodes_count;
	uint32_t free_data_block;
	uint32_t log_block_size;
	uint32_t log_frag_size;
	uint32_t blocks_per_group;
	uint32_t frags_per_group;
	uint32_t inodes_per_group;
	uint32_t mtime;
	uint32_t wtime;

	uint16_t mnt_count;
	uint16_t max_mnt_count;
	uint16_t magic;
	uint16_t state;
	uint16_t errors;
	uint16_t minor_rev_level;

	uint32_t lastcheck;
	uint32_t checkinterval;
	uint32_t creator_os;
	uint32_t rev_level;

	uint16_t def_resuid;
	uint16_t def_resgid;

	uint32_t first_ino;
	uint16_t inode_size;
	uint16_t block_group_nr;
	uint32_t feature_compat;
	uint32_t feature_incompat;
	uint32_t feature_ro_compat;

	uint8_t uuid[16];
	uint8_t volume_name[16];

	uint8_t last_mounted;
	uint32_t algo_bitmap;

	uint8_t prealloc_blocks;
	uint8_t prealloc_dir_blocks;
	uint16_t padding;

	uint8_t journal_uuid[16];
	uint32_t journal_inum;
	uint32_t journal_dev;
	uint32_t last_orphan;

	uint32_t hash_seed[4];
	uint8_t def_hash_version;
	uint16_t padding_a;
	uint8_t padding_b;

	uint32_t default_mount_options;
	uint32_t first_meta_bg;
	uint8_t unused[760];
}Ext2Superblock;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _ext2bgdesc_ {
	uint32_t block_bitmap;
	uint32_t inode_bitmap;
	uint32_t inode_table;
	uint16_t free_blocks_count;
	uint16_t free_inodes_count;
	uint16_t used_dirs_count;
	uint16_t pad;
	uint8_t reserved[12];
}Ext2BlockDescriptor;
#pragma pack(pop)

/* File Types */
#define EXT2_S_IFSOCK  0xC000
#define EXT2_S_IFLNK   0xA000
#define EXT2_S_IFREG   0x8000
#define EXT2_S_IFBLK   0x6000
#define EXT2_S_IFDIR   0x4000
#define EXT2_S_IFCHR   0x2000
#define EXT2_S_IFIFO   0x1000

/* setuid */
#define EXT2_S_ISUID   0x0800
#define EXT2_S_ISGID   0x0400
#define EXT2_S_ISVTX   0x0200

/* rights */
#define EXT2_S_IRUSR   0x0100
#define EXT2_S_IWUSR   0x0080
#define EXT2_S_IXUSR   0x0040
#define EXT2_S_IRGRP   0x0020
#define EXT2_S_IWGRP   0x0010
#define EXT2_S_IXGRP   0x0008
#define EXT2_S_IROTH   0x0004
#define EXT2_S_IWOTH   0x0002
#define EXT2_S_IXOTH   0x0001


#pragma pack(push,1)
typedef struct _ext2_inode_ {
	uint16_t mode;
	uint16_t uid;
	uint32_t size;
	uint32_t atime;
	uint32_t ctime;
	uint32_t mtime;
	uint32_t dtime;
	uint16_t gid;
	uint16_t links_count;
	uint32_t blocks;
	uint32_t flags;
	uint32_t osd1;
	uint32_t block[15];
	uint32_t generation;
	uint32_t file_acl;
	uint32_t dir_acl;
	uint32_t faddr;
	uint8_t osd2[12];
}Ext2Inode;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _ext2_dir_ {
	uint32_t inode;
	uint16_t rec_len;
	uint8_t name_len;
	uint8_t file_type;
	char name[]; //255 bytes
}Ext2Dir;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _ext2_fs_ {
	Ext2Superblock* superblock;
	Ext2BlockDescriptor* block_desc;
	AuVFSNode* root_node;

	AuVDisk* vdisk;

	uint32_t block_size;
	uint32_t pointers_per_block;
	uint32_t inodes_per_group;
	uint32_t block_group_count;

	uint8_t bgd_block_span;
	uint8_t bgd_offset;
	uint32_t inode_size;

	uint8_t* cache_data;

	int flags;

}Ext2Fs;
#pragma pack(pop)

#define EXT2_BGD_BLOCK 2

#define E_SUCCESS 0
#define E_BADBLOCK 1
#define E_NOSPACE 2
#define E_BADPARENT 3

#define EXT2_FLAG_READWRITE  0x00002
#define EXT2_FLAG_LOUD  0x0004

int Ext2FreeBlock(Ext2Fs* fs, uint32_t block_num);

int Ext2FreeInode(Ext2Fs* fs, uint32_t inode_num);

/*
 * Ext2Initialise -- mount the file system
 */
AuVFSNode* Ext2Initialise(AuVDisk* vdisk, char* mountname);

uint32_t Ext2ReadBlockIndex(Ext2Fs* fs, uint32_t block_id, uint32_t index);

uint32_t Ext2FindEntry(Ext2Fs* fs, Ext2Inode* dir_inode, const char* name);

AuVFSNode* Ext2Open(AuVFSNode* fsys, char* path);

int Ext2ReadInode(Ext2Fs* fs, uint32_t inode_num, Ext2Inode* out_inode);

#endif